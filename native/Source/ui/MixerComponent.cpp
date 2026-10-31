#include "MixerComponent.h"

#include <algorithm>
#include <cmath>

using juce::Colour;
using juce::Graphics;
using juce::Rectangle;
using namespace os8::colour;

// --- MixerStrip: the fader's dB scale -----------------------------------------

namespace {
// The fader taper. Position across the travel on the left, the dB it means on the
// right; between the breakpoints the mapping is linear in dB, which is what a
// log-taper fader pot does and why hardware faders use one.
//
// Splitting -60..+6 dB EVENLY over the travel is the mapping that is hard to mix
// on: unity lands at 91 % of the throw, so everything above it is the last handful
// of pixels, while nearly a third of the trough goes on -60 to -40 dB, where
// nothing is audible either way. The taper buys that throw back for the range
// actually worked in, and puts unity at 80 % — exactly where Mixer-channel 509:9257
// draws its 0 dB tick.
struct TaperPoint { float pos, db; };
constexpr TaperPoint kTaper[] = {
    { 0.00f, -60.0f },
    { 0.25f, -30.0f },
    { 0.50f, -12.0f },
    { 0.80f,   0.0f },
    { 1.00f,   6.0f },
};
constexpr int kTaperN = (int) (sizeof (kTaper) / sizeof (kTaper[0]));

float lerp (float a, float b, float t) noexcept { return a + (b - a) * t; }

// Mixer-channel-strip 870:16438, in strip coordinates (the content starts 2 px down).
constexpr int kContentY = 2, kContentH = 42;
constexpr int kNameX = 26, kNameY = 6, kNameW = 203;
constexpr int kMuteX = 234, kSoloX = 250, kBoxY = 7;
constexpr int kLX = 26, kRX = 99, kLRY = 25;
constexpr int kPanX = 34, kPanW = 63, kVolumeX = 118, kVolumeW = 142, kSliderY = 21;
const juce::Rectangle<float> kNumberBox { 2.0f, 18.5f, 13.0f, 9.0f };

const Colour kBevelDark { 0xff808080 };   // the strip's and the bar's lower-right bevel
}

float MixerStrip::posToDb (float pos) noexcept
{
    if (pos <= 0.005f) return -96.0f;
    pos = juce::jlimit (0.0f, 1.0f, pos);

    for (int i = 1; i < kTaperN; ++i)
        if (pos <= kTaper[i].pos)
            return lerp (kTaper[i - 1].db, kTaper[i].db,
                         (pos - kTaper[i - 1].pos) / (kTaper[i].pos - kTaper[i - 1].pos));
    return kMaxDb;
}

float MixerStrip::dbToPos (float db) noexcept
{
    if (db <= kMinDb) return 0.0f;
    if (db >= kMaxDb) return 1.0f;

    for (int i = 1; i < kTaperN; ++i)
        if (db <= kTaper[i].db)
            return lerp (kTaper[i - 1].pos, kTaper[i].pos,
                         (db - kTaper[i - 1].db) / (kTaper[i].db - kTaper[i - 1].db));
    return 1.0f;
}

namespace {
/** Linear amplitude (gain, or a meter peak) -> position on the fader's scale. */
float ampToPos (float amp) noexcept
{
    if (amp <= 0.0f) return 0.0f;
    return MixerStrip::dbToPos (20.0f * std::log10 (amp));
}

/** `text` cut to fit `width` with an ellipsis, measured in Figma's widths. */
juce::String fitted (const juce::String& text, os8::TextStyle style, int width)
{
    if (os8::widthOf (text, style) <= width) return text;
    const juce::String dots = juce::String::fromUTF8 ("\xE2\x80\xA6");
    for (int n = text.length() - 1; n > 0; --n)
    {
        const auto cut = text.substring (0, n).trimEnd() + dots;
        if (os8::widthOf (cut, style) <= width) return cut;
    }
    return dots;
}

/** A slot number, centred on the handle and snapped to whole pixels. */
void drawSlotNumber (Graphics& g, int slot, juce::Point<int> inset)
{
    const juce::String n (slot + 1);
    const auto box = kNumberBox.translated ((float) inset.x, (float) inset.y);
    const float w = (float) os8::widthOf (n, os8::TextStyle::xSmallLabel);
    const float x = std::round (box.getCentreX() - w * 0.5f);
    os8::drawText (g, n, os8::TextStyle::xSmallLabel, { x, box.getY(), w, box.getHeight() });
}
}

// --- MixerStrip -----------------------------------------------------------------

MixerStrip::MixerStrip (se::Engine& e, uint64_t wid) : engine (e), id (wid)
{
    pan.setRange (-1.0, 1.0);
    pan.setDoubleClickReturnValue (true, 0.0);
    volume.setRange (0.0, 1.0);
    volume.setDoubleClickReturnValue (true, (double) dbToPos (0.0f));
    volume.setMarkerValue ((double) dbToPos (0.0f));

    if (auto* w = window())
    {
        pan.setValue (w->balance.load (std::memory_order_relaxed), juce::dontSendNotification);
        volume.setValue (ampToPos (w->gain.load (std::memory_order_relaxed)), juce::dontSendNotification);
    }

    // Touching a fader is working on that Window: raise it first.
    pan.onDragStart = volume.onDragStart = [this] { if (onFocusRequested) onFocusRequested (id); };

    pan.onValueChange = [this]
    {
        if (auto* w = window()) w->balance.store ((float) pan.getValue(), std::memory_order_relaxed);
        repaint();   // the name cell shows the balance while the slider moves
    };
    pan.onDragEnd = [this] { repaint(); };
    volume.onValueChange = [this]
    {
        if (auto* w = window())
            w->gain.store (se::Engine::dbToGain (posToDb ((float) volume.getValue())), std::memory_order_relaxed);
        repaint();   // the name cell shows the dB readout while the fader moves
    };
    volume.onDragEnd = [this] { repaint(); };

    for (auto* s : { &pan, &volume })
    {
        addAndMakeVisible (*s);
        s->addMouseListener (&hoverWatch, false);
    }
}

MixerStrip::~MixerStrip()
{
    pan.removeMouseListener (&hoverWatch);
    volume.removeMouseListener (&hoverWatch);
}

// The boxes are right-aligned: on the Active strip they move down, not across.
Rectangle<int> MixerStrip::muteBox() const { return { kMuteX, kBoxY + inset().y, 11, 11 }; }
Rectangle<int> MixerStrip::soloBox() const { return { kSoloX, kBoxY + inset().y, 11, 11 }; }

bool MixerStrip::inDragArea (juce::Point<int> p) const
{
    // Everything that is not a control: the handle, the name, the margins. The
    // sliders are children and never send their presses here.
    return ! muteBox().expanded (2).contains (p) && ! soloBox().expanded (2).contains (p);
}

juce::Colour MixerStrip::themeFill() const
{
    // themeColour(-1) is Placeholder-gray-100 — the design's own untinted strip.
    auto* w = window();
    return os8::themeColour (w != nullptr ? w->tint : -1);
}

bool MixerStrip::muted() const
{
    auto* w = window();
    return w == nullptr || w->muted.load (std::memory_order_relaxed);
}

bool MixerStrip::silenced() const
{
    auto* w = window();
    if (w == nullptr) return true;
    return w->muted.load (std::memory_order_relaxed)
           || (soloFilter && ! w->soloed.load (std::memory_order_relaxed));
}

void MixerStrip::updateSliderLooks()
{
    // The troughs are MULTIPLY layers on whatever is under them: the strip's theme,
    // or the veiled theme (white @ 80 % over it) when the strip is silenced.
    const bool off = silenced();
    const auto under = off ? themeFill().interpolatedWith (white, 0.8f) : themeFill();
    for (auto* s : { &pan, &volume })
    {
        s->setMuted (off);
        s->setUnder (under);
    }
}

void MixerStrip::resized()
{
    const auto in = inset();
    pan.setBounds (Rectangle<int> (kPanX, kSliderY, kPanW, os8::kSliderHeight) + in);
    volume.setBounds (Rectangle<int> (kVolumeX, kSliderY, kVolumeW, os8::kSliderHeight) + in);
}

void MixerStrip::refreshState (bool soloFilterOn)
{
    auto* w = window();
    if (w == nullptr) return;

    bool dirty = false;

    // The name is edited on the Window's title bar, and nothing tells this strip
    // when that happens — so poll it, like the theme, which a new Window only
    // gets once the canvas has placed it.
    const juce::String name = w->uiDoc != nullptr ? juce::String (w->uiDoc->name) : juce::String();
    if (name != lastName) { lastName = name; dirty = true; }

    if (w->tint != lastTint) { lastTint = w->tint; dirty = true; }

    // M and S can be pressed on the Window's title bar or in the menus, and the
    // solo filter is somebody ELSE's solo: all three change how this strip looks.
    const bool m = w->muted.load (std::memory_order_relaxed);
    const bool s = w->soloed.load (std::memory_order_relaxed);
    soloFilter = soloFilterOn;
    const bool off = silenced();
    if (m != lastMuted || s != lastSoloed || off != lastSilenced)
    {
        lastMuted = m; lastSoloed = s; lastSilenced = off;
        dirty = true;
    }

    if (dirty)
    {
        updateSliderLooks();
        repaint();
    }

    // Gain and balance can change under the faders (a loaded canvas); follow them
    // unless the user is holding the fader.
    if (! volume.isMouseButtonDown())
    {
        const double pos = ampToPos (w->gain.load (std::memory_order_relaxed));
        if (std::abs (pos - volume.getValue()) > 1.0e-4) volume.setValue (pos, juce::dontSendNotification);
    }
    if (! pan.isMouseButtonDown())
    {
        const double bal = w->balance.load (std::memory_order_relaxed);
        if (std::abs (bal - pan.getValue()) > 1.0e-4) pan.setValue (bal, juce::dontSendNotification);
    }

    // The VU bars end where the thumb's centre would sit at that level, so a peak
    // at unity reaches the 0 dB tick exactly.
    const auto level = [this] (float amp)
    {
        if (amp <= 0.0f) return 0.0f;
        const float wdt = (float) volume.getWidth();
        const float half = (float) os8::kThumbWidth * 0.5f;
        return (half + ampToPos (amp) * (wdt - 2.0f * half)) / wdt;
    };
    volume.setLevels (level (w->meterLeft.load (std::memory_order_relaxed)),
                      level (w->meterRight.load (std::memory_order_relaxed)));
}

void MixerStrip::setSelected (bool shouldBeSelected)
{
    if (selected == shouldBeSelected) return;
    selected = shouldBeSelected;
    resized();   // the Active strip's content sits 1 px in
    repaint();
}

void MixerStrip::paint (Graphics& g)
{
    auto* w = window();
    if (w == nullptr) return;

    const auto in = inset();
    const bool off = silenced();
    const Colour ink = off ? gray400 : black;
    const auto bounds = getLocalBounds();
    const int h = getHeight();

    g.setColour (themeFill());
    g.fillRect (bounds);

    // The "content" frame: a white veil over a silenced strip, under its controls.
    if (off)
    {
        g.setColour (white.withAlpha (0.8f));
        // The Active strip's content frame is 1 px narrower on each side.
        const int cx = selected ? 1 : 0;
        g.fillRect (cx, kContentY, kWidth - 2 * cx, kContentH);
    }

    // The handle: a black rule at its right, the slot number, and on the Active
    // strip the two stacks of drag bars.
    g.setColour (black);
    g.fillRect (kHandleWidth - 1, kContentY, 1, kContentH);

    if (selected)
    {
        for (const float top : { 3.5f, 30.5f })
            for (int i = 0; i < 6; ++i)
            {
                const juce::Rectangle<float> r ((float) (3 + 2 * i), top, 1.0f, 12.0f);
                juce::Path bar;
                bar.addRectangle (r);
                {
                    // Figma does not show a drop shadow through the half-transparent
                    // bar above it: knock the bar's whole pixels out of its shadows.
                    juce::Graphics::ScopedSaveState save (g);
                    const float y0 = std::ceil (r.getY()), y1 = std::floor (r.getBottom());
                    g.excludeClipRegion (juce::Rectangle<float> (r.getX(), y0, 1.0f, y1 - y0).toNearestInt());
                    os8::dropShadow (g, bar,  1.0f,  0.0f, white.withAlpha (0.5f));
                    os8::dropShadow (g, bar,  0.0f,  1.0f, black.withAlpha (0.35f));
                    os8::dropShadow (g, bar,  0.0f, -1.0f, black.withAlpha (0.35f));
                }
                g.setColour (black.withAlpha (0.5f));
                g.fillPath (bar);
            }
    }

    g.setColour (selected && ! off ? ink.withAlpha (0.9f) : ink);
    drawSlotNumber (g, w->slot, in);

    // Name — or the fader's dB readout, which takes the cell over while the fader
    // is held, so the strip needs no permanent numeric column.
    juce::String label;
    if (pan.isMouseButtonDown())
    {
        // "50L … C … 50R": the -1..+1 balance as 0..50 a side.
        const int n = juce::roundToInt (std::abs (pan.getValue()) * 50.0);
        label = n == 0 ? juce::String ("C") : juce::String (n) + (pan.getValue() < 0.0 ? "L" : "R");
    }
    else if (volume.isMouseButtonDown())
    {
        const float db = posToDb ((float) volume.getValue());
        label = db <= kMinDb ? juce::String::fromUTF8 ("-\xE2\x88\x9E dB")
                             : (db >= 0.0f ? "+" : "") + juce::String (db, 1) + " dB";
    }
    else
    {
        label = fitted (lastName, os8::TextStyle::headline, kNameW);
    }
    g.setColour (ink);
    os8::drawText (g, label, os8::TextStyle::headline,
                   Rectangle<float> ((float) kNameX, (float) kNameY, (float) kNameW, 12.0f) + in.toFloat());

    os8::drawText (g, "L", os8::TextStyle::smallLabel, Rectangle<float> ((float) kLX, (float) kLRY, 6.0f, 10.0f) + in.toFloat());
    os8::drawText (g, "R", os8::TextStyle::smallLabel, Rectangle<float> ((float) kRX, (float) kLRY, 7.0f, 10.0f) + in.toFloat());

    os8::paintTitleBarBox (g, muteBox().getPosition(), lastMuted,  os8::TitleBox::mute);
    os8::paintTitleBarBox (g, soloBox().getPosition(), lastSoloed, os8::TitleBox::solo);

    // The strip's own bevel (inner shadows, painted over its content as Figma does),
    // then its stroke. At rest and on hover: a white inner shadow (1, 1) only; hover
    // adds a black rule above and below. Active: white (2, 3) and #808080 (-2, -3)
    // under a black frame 2 px top and bottom, 1 px at the sides.
    juce::Path shape;
    shape.addRectangle (bounds.toFloat());
    const bool hover = isMouseOver (true) || draggingStrip || forcedHover;
    if (selected)
    {
        os8::innerShadow (g, shape,  2.0f,  3.0f, 0, white);
        os8::innerShadow (g, shape, -2.0f, -3.0f, 0, kBevelDark);
        g.setColour (black);
        g.fillRect (0, 0, kWidth, 2);
        g.fillRect (0, h - 2, kWidth, 2);
        g.fillRect (0, 0, 1, h);
        g.fillRect (kWidth - 1, 0, 1, h);
    }
    else
    {
        os8::innerShadow (g, shape, 1.0f, 1.0f, 0, white);
        if (hover)
        {
            g.setColour (black);
            g.fillRect (0, 0, kWidth, 1);
            g.fillRect (0, h - 1, kWidth, 1);
        }
    }
}

void MixerStrip::mouseEnter (const juce::MouseEvent&) { repaint(); }
void MixerStrip::mouseExit (const juce::MouseEvent&)  { repaint(); }

void MixerStrip::mouseDown (const juce::MouseEvent& e)
{
    // Touching a channel at all means you are working on that Window: bring it to
    // the front of the canvas first, whatever the click then turns out to do.
    if (onFocusRequested) onFocusRequested (id);

    pressInDragArea = false;
    auto* w = window();
    if (w == nullptr) return;

    // ⌘-click on a lit box clears that box on every Window.
    const bool clearAll = e.mods.isCommandDown();
    if (muteBox().expanded (2).contains (e.getPosition()))
    {
        if (clearAll && w->muted.load (std::memory_order_relaxed)) engine.unmuteAll();
        else w->muted.store (! w->muted.load (std::memory_order_relaxed), std::memory_order_relaxed);
        refreshState (engine.anyWindowSoloed());
        return;
    }
    if (soloBox().expanded (2).contains (e.getPosition()))
    {
        if (clearAll && w->soloed.load (std::memory_order_relaxed)) engine.clearAllSolos();
        else w->soloed.store (! w->soloed.load (std::memory_order_relaxed), std::memory_order_relaxed);
        refreshState (engine.anyWindowSoloed());   // the OTHER strips catch up on the next tick
        return;
    }

    pressInDragArea = inDragArea (e.getPosition());
}

void MixerStrip::mouseDrag (const juce::MouseEvent& e)
{
    if (! pressInDragArea) return;

    // Screen coordinates: the strip itself moves under the mouse.
    const int dy = e.getScreenY() - e.getMouseDownScreenY();
    if (! draggingStrip)
    {
        if (std::abs (dy) < 3) return;
        draggingStrip = true;
        dragStartTop = getY();
        toFront (false);
    }
    if (onDragMove) onDragMove (*this, dragStartTop + dy);
}

void MixerStrip::mouseUp (const juce::MouseEvent&)
{
    pressInDragArea = false;
    if (! draggingStrip) return;
    draggingStrip = false;
    if (onDragEnd) onDragEnd (*this);
    repaint();
}

// --- MixerComponent -------------------------------------------------------------

MixerComponent::MixerComponent (se::Engine& e) : engine (e)
{
    newButton.onClick  = [this] { if (onNew)  onNew(); };
    openButton.onClick = [this] { if (onOpen) onOpen(); };
    addAndMakeVisible (newButton);
    addAndMakeVisible (openButton);

    // No scroll bar: it would sit on the M/S boxes. The wheel still scrolls.
    viewport.setViewedComponent (&list, false);
    viewport.setScrollBarsShown (false, false, true, false);
    addAndMakeVisible (viewport);
}

MixerComponent::~MixerComponent() = default;

MixerStrip* MixerComponent::stripInSlot (int slot) const
{
    for (auto& s : strips)
        if (auto* w = engine.windowById (s->windowId()); w != nullptr && w->slot == slot)
            return s.get();
    return nullptr;
}

void MixerComponent::refresh()
{
    const auto& ids = engine.windowIds();
    const size_t before = strips.size();

    // Drop strips whose Window is gone (Close frees the slot).
    strips.erase (std::remove_if (strips.begin(), strips.end(),
                    [&ids] (const std::unique_ptr<MixerStrip>& s)
                    { return std::find (ids.begin(), ids.end(), s->windowId()) == ids.end(); }),
                  strips.end());

    bool changed = strips.size() != before;

    for (uint64_t id : ids)
    {
        const bool have = std::any_of (strips.begin(), strips.end(),
                                       [id] (const std::unique_ptr<MixerStrip>& s)
                                       { return s->windowId() == id; });
        if (have) continue;

        auto strip = std::make_unique<MixerStrip> (engine, id);
        strip->onFocusRequested = [this] (uint64_t wid)
        {
            setSelectedWindow (wid);                          // Active here immediately...
            if (onChannelClicked) onChannelClicked (wid);     // ...and raised there
        };
        strip->onDragMove = [this] (MixerStrip& s, int top)
        {
            if (auto* w = engine.windowById (s.windowId()); w != nullptr && dragFrom < 0) dragFrom = w->slot;
            top = juce::jlimit (0, juce::jmax (0, list.getHeight() - s.getHeight()), top);
            s.setTopLeftPosition (0, top);
            dropGap = gapAt (top);
            list.repaint();
        };
        strip->onDragEnd = [this] (MixerStrip& s)
        {
            // Insert between two strips (no empty rows to drop on): the gap counted
            // among ALL rows, minus one when it lies below the strip's own row.
            const int gap = dropGap, from = dragFrom;
            dropGap = dragFrom = -1;
            if (gap >= 0 && from >= 0)
                engine.moveToSlot (s.windowId(), gap > from ? gap - 1 : gap);   // the canvas is untouched
            layoutRows();
        };
        strip->setSelected (id == selectedId);
        list.addAndMakeVisible (*strip);
        strips.push_back (std::move (strip));
        changed = true;
    }

    // A slot can also change without a strip coming or going: a loaded canvas,
    // or a drop. Rows are cheap to lay out; compare where everybody sits.
    if (! changed)
        for (auto& s : strips)
            if (! s->isBeingDragged())
                if (auto* w = engine.windowById (s->windowId());
                    w != nullptr && (int) rowTops.size() > w->slot + 1 && s->getY() != rowTops[(size_t) w->slot])
                { changed = true; break; }

    if (changed) layoutRows();

    // One question for the whole panel, not one per strip: is the solo filter on?
    const bool soloFilterOn = engine.anyWindowSoloed();
    for (auto& s : strips) s->refreshState (soloFilterOn);
}

void MixerComponent::setSelectedWindow (uint64_t id)
{
    if (selectedId == id) return;
    selectedId = id;
    for (auto& s : strips) s->setSelected (s->windowId() == id);
    layoutRows();   // the Active strip is 2 px taller: the rows below move down
}

void MixerComponent::layoutRows()
{
    // One row per slot, 0..N-1: slots have no gaps.
    const int rows = engine.highestSlot() + 1;

    rowTops.assign (1, 0);
    for (int i = 0; i < rows; ++i)
    {
        const auto* s = stripInSlot (i);
        const int h = s != nullptr && s->isSelected() ? MixerStrip::kActiveHeight : MixerStrip::kHeight;
        rowTops.push_back (rowTops.back() + h);
    }

    list.setSize (MixerStrip::kWidth, rowTops.back());

    for (auto& s : strips)
    {
        auto* w = engine.windowById (s->windowId());
        if (w == nullptr || w->slot < 0 || w->slot >= rows) continue;
        const int h = s->isSelected() ? MixerStrip::kActiveHeight : MixerStrip::kHeight;
        if (! s->isBeingDragged())
            s->setBounds (0, rowTops[(size_t) w->slot], MixerStrip::kWidth, h);
        else
            s->setSize (MixerStrip::kWidth, h);
    }
    list.repaint();
}

int MixerComponent::gapAt (int top) const
{
    // The gap above the first row whose middle is below the dragged strip's middle.
    const int rows = (int) rowTops.size() - 1;
    const int centre = top + MixerStrip::kHeight / 2;
    for (int i = 0; i < rows; ++i)
        if (centre < (rowTops[(size_t) i] + rowTops[(size_t) i + 1]) / 2) return i;
    return juce::jmax (0, rows);
}

void MixerComponent::SlotList::paintOverChildren (Graphics& g)
{
    // Where a dragged strip would go: a 2 px black bar between two strips. PROVISIONAL
    // (no Figma node yet; the author is asked to design it). Dropping it back where
    // it was shows nothing.
    const int rows = (int) owner.rowTops.size() - 1;
    const int gap = owner.dropGap, from = owner.dragFrom;
    if (gap >= 0 && gap <= rows && gap != from && gap != from + 1)
    {
        const int y = juce::jlimit (1, juce::jmax (1, getHeight() - 1), owner.rowTops[(size_t) gap]);
        g.setColour (black);
        g.fillRect (0, y - 1, getWidth(), 2);
    }
}

void MixerComponent::paint (Graphics& g)
{
    // "new-open" 636:10150: a #dddddd bar laid out 270 wide like the strips (its
    // right edge falls under the panel frame). Its bevel is listed as 2 px, but the
    // OUTSIDE stroke grows the shape the inner shadows use and then covers the outer
    // pixel, so 1 px of each band shows. The stroke's top edge is the rule on the
    // title bar's last row (SidePanel::setTitleRule); its bottom edge is the line
    // under the bar.
    const Rectangle<int> bar (0, 0, MixerStrip::kWidth + 2, kBarHeight);
    g.setColour (gray100);
    g.fillRect (bar);
    juce::Path shape;
    shape.addRectangle (bar.toFloat());
    os8::innerShadow (g, shape,  1.0f,  1.0f, 0, white);
    os8::innerShadow (g, shape, -1.0f, -1.0f, 0, Colour (0xff808080));
    g.setColour (black);
    g.fillRect (0, kBarHeight, getWidth(), 1);
}

void MixerComponent::resized()
{
    // The two 122 x 25 primary buttons sit at y 9.5 in the design; on whole pixels
    // that is 10.
    newButton.setBounds (8, 10, 122, 25);
    openButton.setBounds (140, 10, 122, 25);

    viewport.setBounds (getLocalBounds().withTrimmedTop (kBarHeight));
    layoutRows();
}
