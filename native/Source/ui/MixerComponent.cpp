#include "MixerComponent.h"

#include <algorithm>
#include <cmath>

using juce::Colour;
using juce::Graphics;
using juce::Rectangle;

namespace {
// The same flat System 7 palette the canvas Windows are painted in.
const Colour kFace   { 0xffbfbfbf };
const Colour kInk    { 0xff000000 };
const Colour kWhite  { 0xffffffff };
const Colour kMeter  { 0xff4a4a4a };   // VU fill, dark enough to read under the cap
const Colour kClip   { 0xffcc2222 };   // the same red as the record dot
const Colour kDim    { 0xff7a7a7a };   // muted / empty text

constexpr int kPad       = 4;
constexpr int kButtonH   = 22;
constexpr int kHeaderH   = 15;
constexpr int kButtonBar = kButtonH + 2 * kPad;

// Strip metrics.
constexpr int kNameW   = 66;
constexpr int kMuteW   = 17;
constexpr int kMuteH   = 13;
constexpr int kTroughH = 12;
constexpr int kThumbW  = 7;
constexpr int kGap     = 5;
}

// --- Sys7Button -------------------------------------------------------------

void Sys7Button::paintButton (Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);

    g.setColour (down ? kInk : kFace);
    g.fillRoundedRectangle (r, 3.0f);

    // Weight carries hover, exactly as the Windows' borders do on the canvas.
    g.setColour (kInk);
    g.drawRoundedRectangle (r, 3.0f, over && ! down ? 2.0f : 1.0f);

    g.setColour (down ? kWhite : kInk);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred, false);
}

// --- Sys7LookAndFeel --------------------------------------------------------

void Sys7LookAndFeel::drawScrollbar (Graphics& g, juce::ScrollBar&, int x, int y, int w, int h,
                                     bool isVertical, int thumbStart, int thumbSize,
                                     bool isMouseOver, bool)
{
    const Rectangle<int> track (x, y, w, h);
    g.setColour (kWhite);
    g.fillRect (track);
    g.setColour (kInk);
    g.drawRect (track, 1);

    if (thumbSize <= 0) return;

    const auto thumb = (isVertical ? Rectangle<int> (x, thumbStart, w, thumbSize)
                                   : Rectangle<int> (thumbStart, y, thumbSize, h)).reduced (1);
    g.setColour (kFace);
    g.fillRect (thumb);
    g.setColour (kInk);
    g.drawRect (thumb, isMouseOver ? 2 : 1);
}

// --- MixerStrip: the shared dB scale ----------------------------------------

float MixerStrip::posToDb (float pos) noexcept
{
    // The bottom of the travel is silence, not kMinDb, so the fader can shut the
    // channel completely rather than leaving it quietly running.
    if (pos <= 0.005f) return -96.0f;
    return kMinDb + pos * (kMaxDb - kMinDb);
}

float MixerStrip::dbToPos (float db) noexcept
{
    return juce::jlimit (0.0f, 1.0f, (db - kMinDb) / (kMaxDb - kMinDb));
}

namespace {
/** Linear amplitude (gain, or a meter peak) -> position on the strip's dB scale. */
float ampToPos (float amp) noexcept
{
    if (amp <= 0.0f) return 0.0f;
    return MixerStrip::dbToPos (20.0f * std::log10 (amp));
}
}

// --- MixerStrip -------------------------------------------------------------

MixerStrip::MixerStrip (se::Engine& e, uint64_t wid) : engine (e), id (wid) {}

Rectangle<int> MixerStrip::nameRect() const
{
    return getLocalBounds().reduced (kPad, 0).removeFromLeft (kNameW);
}

Rectangle<int> MixerStrip::muteRect() const
{
    auto r = getLocalBounds().reduced (kPad, 0);
    r.removeFromLeft (kNameW + kGap);
    return r.removeFromLeft (kMuteW).withSizeKeepingCentre (kMuteW, kMuteH);
}

Rectangle<int> MixerStrip::troughRect() const
{
    auto r = getLocalBounds().reduced (kPad, 0);
    r.removeFromLeft (kNameW + kGap + kMuteW + kGap);
    return r.withSizeKeepingCentre (r.getWidth(), kTroughH);
}

namespace {
/** Where a 0..1 position sits in the trough, inset so the cap never overhangs it. */
int xForPos (Rectangle<int> trough, float pos) noexcept
{
    const int half = kThumbW / 2;
    const int x0 = trough.getX() + half;
    const int x1 = trough.getRight() - 1 - half;
    return x0 + juce::roundToInt (juce::jlimit (0.0f, 1.0f, pos) * (float) (x1 - x0));
}

float posForX (Rectangle<int> trough, int x) noexcept
{
    const int half = kThumbW / 2;
    const int x0 = trough.getX() + half;
    const int x1 = trough.getRight() - 1 - half;
    if (x1 <= x0) return 0.0f;
    return juce::jlimit (0.0f, 1.0f, (float) (x - x0) / (float) (x1 - x0));
}
}

float MixerStrip::gainPos() const noexcept
{
    auto* w = window();
    return w != nullptr ? ampToPos (w->gain.load (std::memory_order_relaxed)) : 0.0f;
}

void MixerStrip::setGainFromX (int x)
{
    if (auto* w = window())
        w->gain.store (se::Engine::dbToGain (posToDb (posForX (troughRect(), x))),
                       std::memory_order_relaxed);
    repaint();
}

void MixerStrip::refreshState()
{
    auto* w = window();

    // The name is edited on the Window's title bar, and nothing tells this strip
    // when that happens — so poll it. A string compare per strip per tick is
    // nothing next to the repaint it saves.
    const juce::String name = w != nullptr && w->uiDoc != nullptr ? juce::String (w->uiDoc->name)
                                                                 : juce::String();
    if (name != lastName)
    {
        lastName = name;
        repaint (nameRect());
    }

    const float pos = w != nullptr ? ampToPos (w->meter.load (std::memory_order_relaxed)) : 0.0f;

    // Half a pixel of a ~130px trough: below that the repaint would show nothing.
    if (std::abs (pos - meterLevel) < 0.004f) return;
    meterLevel = pos;
    repaint (troughRect());
}

void MixerStrip::paint (Graphics& g)
{
    auto* w = window();
    if (w == nullptr) return;

    const bool muted = w->muted.load (std::memory_order_relaxed);

    g.setColour (kFace);
    g.fillRect (getLocalBounds());
    g.setColour (kInk);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

    // Name — or the dB readout, which takes the cell over for the length of a
    // fader drag so the strip needs no permanent numeric column.
    g.setFont (juce::FontOptions (10.0f));
    g.setColour (muted ? kDim : kInk);
    juce::String label;
    if (draggingFader)
    {
        const float db = posToDb (gainPos());
        label = db <= kMinDb ? juce::String::fromUTF8 ("-\xE2\x88\x9E dB")
                             : (db >= 0.0f ? "+" : "") + juce::String (db, 1) + " dB";
        g.setColour (kInk);
    }
    else
    {
        label = juce::String (w->uiDoc != nullptr ? w->uiDoc->name : std::string ("window"));
    }
    g.drawText (label, nameRect(), juce::Justification::centredLeft, true);   // long names ellipse

    // Mute, lit = inverted (the transport buttons' convention).
    auto m = muteRect();
    g.setColour (muted ? kInk : kFace);   g.fillRect (m);
    g.setColour (muted ? kWhite : kInk);  g.drawRect (m, 1);
    g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
    g.drawText ("M", m, juce::Justification::centred, false);

    // The combined fader / VU trough. One scale, three marks: the fill is what
    // the channel is putting out, the tick is 0 dB, the cap is the gain.
    auto t = troughRect();
    g.setColour (kWhite);
    g.fillRect (t);

    const int unityX = xForPos (t, dbToPos (0.0f));
    if (meterLevel > 0.0f)
    {
        auto fill = t.reduced (1);
        const int end = xForPos (t, meterLevel);
        g.setColour (muted ? kDim : kMeter);
        g.fillRect (fill.withRight (std::min (end, unityX)));
        if (end > unityX)   // past 0 dBFS: the same red the record dot uses
        {
            g.setColour (kClip);
            g.fillRect (fill.withLeft (unityX).withRight (end));
        }
    }

    g.setColour (kInk);
    g.drawVerticalLine (unityX, (float) t.getY(), (float) t.getBottom());
    g.drawRect (t, 1);

    // Fader cap: a face-coloured handle riding the meter, notched down the middle.
    const int cx = xForPos (t, gainPos());
    Rectangle<int> cap (cx - kThumbW / 2, t.getY() - 2, kThumbW, t.getHeight() + 4);
    g.setColour (kFace); g.fillRect (cap);
    g.setColour (kInk);  g.drawRect (cap, 1);
    g.drawVerticalLine (cx, (float) (cap.getY() + 2), (float) (cap.getBottom() - 2));
}

void MixerStrip::mouseDown (const juce::MouseEvent& e)
{
    if (muteRect().contains (e.getPosition()))
    {
        if (auto* w = window())
            w->muted.store (! w->muted.load (std::memory_order_relaxed), std::memory_order_relaxed);
        repaint();
        return;
    }

    // The whole row height grabs the fader — a 12px trough is a mean target.
    if (troughRect().expanded (2, kHeight).contains (e.getPosition()))
    {
        draggingFader = true;
        setGainFromX (e.x);
    }
}

void MixerStrip::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingFader) setGainFromX (e.x);
}

void MixerStrip::mouseUp (const juce::MouseEvent&)
{
    if (! draggingFader) return;
    draggingFader = false;
    repaint();
}

void MixerStrip::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! troughRect().expanded (2, kHeight).contains (e.getPosition())) return;
    if (auto* w = window()) w->gain.store (1.0f, std::memory_order_relaxed);   // back to unity
    repaint();
}

// --- MixerComponent ---------------------------------------------------------

MixerComponent::MixerComponent (se::Engine& e) : engine (e)
{
    setLookAndFeel (&lookAndFeel);   // children inherit it: the list's scrollbar

    newButton.onClick  = [this] { if (onNew)  onNew(); };
    openButton.onClick = [this] { if (onOpen) onOpen(); };
    addAndMakeVisible (newButton);
    addAndMakeVisible (openButton);

    viewport.setViewedComponent (&stripHolder, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (10);
    addAndMakeVisible (viewport);
}

MixerComponent::~MixerComponent()
{
    setLookAndFeel (nullptr);   // before the LookAndFeel member goes
}

void MixerComponent::refresh()
{
    const auto& ids = engine.windowIds();
    const size_t before = strips.size();

    // Drop strips whose Window is gone.
    strips.erase (std::remove_if (strips.begin(), strips.end(),
                    [&ids] (const std::unique_ptr<MixerStrip>& s)
                    { return std::find (ids.begin(), ids.end(), s->windowId()) == ids.end(); }),
                  strips.end());

    bool changed = strips.size() != before;

    // Add strips for new Windows, in engine order.
    for (uint64_t id : ids)
    {
        const bool have = std::any_of (strips.begin(), strips.end(),
                                       [id] (const std::unique_ptr<MixerStrip>& s)
                                       { return s->windowId() == id; });
        if (! have)
        {
            strips.push_back (std::make_unique<MixerStrip> (engine, id));
            stripHolder.addAndMakeVisible (*strips.back());
            changed = true;
        }
    }

    if (changed)
    {
        resized();
        repaint();
    }

    for (auto& s : strips) s->refreshState();
}

Rectangle<int> MixerComponent::listArea() const
{
    return getLocalBounds().reduced (1).withTrimmedTop (kButtonBar + kHeaderH);
}

void MixerComponent::paint (Graphics& g)
{
    g.fillAll (kFace);

    auto inner = getLocalBounds().reduced (1);

    // Button bar, then the header naming the panel and counting its channels.
    auto bar = inner.removeFromTop (kButtonBar);
    g.setColour (kInk);
    g.drawHorizontalLine (bar.getBottom() - 1, (float) bar.getX(), (float) bar.getRight());

    auto header = inner.removeFromTop (kHeaderH);
    g.setColour (kInk);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("Mixer", header.reduced (kPad, 0), juce::Justification::centredLeft, false);
    g.setFont (juce::FontOptions (10.0f));
    g.setColour (kDim);
    const int n = (int) strips.size();
    g.drawText (juce::String (n) + (n == 1 ? " channel" : " channels"),
                header.reduced (kPad, 0), juce::Justification::centredRight, false);
    g.setColour (kInk);
    g.drawHorizontalLine (header.getBottom() - 1, (float) header.getX(), (float) header.getRight());

    if (strips.empty())
    {
        g.setColour (kDim);
        g.setFont (juce::FontOptions (10.0f));
        g.drawText ("No sounds open", listArea().removeFromTop (40),
                    juce::Justification::centred, false);
    }

    g.setColour (kInk);
    g.drawRect (getLocalBounds(), 1);
}

void MixerComponent::resized()
{
    auto inner = getLocalBounds().reduced (1);

    auto bar = inner.removeFromTop (kButtonBar).reduced (kPad);
    const int halfW = (bar.getWidth() - kGap) / 2;
    newButton.setBounds (bar.removeFromLeft (halfW));
    bar.removeFromLeft (kGap);
    openButton.setBounds (bar.removeFromLeft (halfW));

    inner.removeFromTop (kHeaderH);   // painted, no component
    viewport.setBounds (inner);

    // Height first, so the scrollbar has decided whether it is there before the
    // strips take their width from what is left.
    const int h = (int) strips.size() * MixerStrip::kHeight;
    stripHolder.setSize (viewport.getWidth(), h);
    stripHolder.setSize (juce::jmax (1, viewport.getMaximumVisibleWidth()), h);

    int y = 0;
    for (auto& s : strips)
    {
        s->setBounds (0, y, stripHolder.getWidth(), MixerStrip::kHeight);
        y += MixerStrip::kHeight;
    }
}
