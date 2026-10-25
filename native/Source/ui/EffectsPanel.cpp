#include "EffectsPanel.h"

#include "EffectLayouts.h"
#include "effects/Units.h"

#include <algorithm>
#include <cmath>

using juce::Graphics;
using juce::Rectangle;
namespace colour = os8::colour;

namespace {

// --- the groups, as Effects-group 527:5980 lists them ----------------------------
//
// The design is the authority on what a button says and where it sits, so the panel's
// layout is this table, not the registry's own grouping: a button is the design's label
// over the module it runs. A module the registry does not (yet) have is drawn greyed.
// `width` is the button's width in the design: text plus 12 px, as Figma measures it. It
// is listed rather than computed because the one string the font's advances disagree
// on ("Invert", 42 px of text) would put every button after it a pixel out.
struct Spec { const char* label; const char* module; int width; };

const Spec kOneClick[] = {
    { "Invert", "Invert", 54 }, { "Noise", "Noise", 49 }, { "Normalize", "Normalize", 81 },
    { "Reverse", "Reverse", 67 }, { "Robotize", "Robotize", 71 }, { "Silence", "Silence", 58 },
    { "Selection to Channel", "Selection to Channel", 149 }, { "Smooth", "Smooth", 62 },
    { "Sum", "Sum", 39 }, { "Weird Tempo", "Weird Tempo", 101 },
    // the FIR Filters sub-category
    { "Bandpass FIR", "Bandpass FIR", 100 }, { "Bandstop FIR", "Bandstop FIR", 99 },
    { "Hi-Pass FIR", "High Pass FIR", 87 }, { "Low-Pass FIR", "Low Pass FIR", 102 },
};
constexpr int kFirStart = 10;

const Spec kMisc[] = {
    { "Amplify", "Amplify", 63 }, { "Dither", "Dither", 54 }, { "Difference", "Difference", 84 },
    { "Draw Line", "Draw Line", 80 }, { "Fade-In", "Fade In", 64 }, { "Fade-Out", "Fade Out", 74 },
    { "Quick Mix", "Quick Mix", 77 }, { "Replicate", "Replicate", 74 },
    { "Waveform Generator", "Waveform Generator", 156 }, { "Zero Crosser", "Zero Crosser", 101 },
};
const Spec kEchoes[] = { { "Echo", "Echo", 44 }, { "Delay", "Delay", 49 }, { "Flanger", "Flanger", 64 } };
const Spec kIir[] = {
    { "Bandpass IIR", "Bandpass IIR", 96 }, { "Hi-Pass IIR", "High Pass IIR", 82 },
    { "Low-Pass IIR", "Low Pass IIR", 97 }, { "Shelving", "Shelving", 68 },
};
const Spec kMike[] = {
    { "Granular Synthesis", "Granular Synthesis", 139 }, { "Iterative Crossfade", "Iterative Crossfade", 145 },
    { "Sample Hose", "Sample Hose", 96 },
};
const Spec kResampling[] = {
    { "Downsample Bits", "Downsample Bits", 126 }, { "Resample Rate", "Resample Rate", 111 },
    { "Upsample Bits", "Upsample Bits", 106 },
};

struct GroupSpec
{
    const char* title;
    const Spec* specs;
    int count;
    int subStart;           // index of the first button of the sub-category, or -1
    const char* subTitle;
};

template <size_t N>
constexpr GroupSpec group (const char* title, const Spec (&specs)[N], int subStart = -1, const char* sub = "")
{
    return { title, specs, (int) N, subStart, sub };
}

const GroupSpec kGroups[] = {
    group ("1-Click Effects", kOneClick, kFirStart, "FIR Filters"),
    group ("Misc", kMisc),
    group ("Echoes & Reverbs", kEchoes),
    group ("IIR Filters", kIir),
    group ("Mike\xE2\x80\x99s Modules", kMike),
    group ("Resampling", kResampling),
};

// --- geometry (all from Figma: 636:14867, 527:5980, 103:8685, 103:8688) ------------

constexpr int kFrameW = 270;          // frames are 270 wide in a 268 px column: 1 px each side is cut
constexpr int kHeaderH = 48;
constexpr int kGroupX = 8, kGroupTop = 4;     // the Effects-group instance inside its frame
constexpr int kRowStride = 24, kButtonH = 20, kButtonGap = 4, kRowWidth = 252;
constexpr int kTitleY = 13;                    // frame top → the group title's text box
constexpr int kTrayInset = 9;                  // the tray frame → its Effect-settings content

const juce::Colour kShadowGrey { 0xff808080 };

/** Frames / 103:8685 and Sound-info: #dddddd with a white and a grey inner bevel. */
void paintBevelFrame (Graphics& g, Rectangle<int> r, bool strokeOutside)
{
    juce::Path shape;
    shape.addRectangle (r.toFloat());
    g.setColour (colour::gray100);
    g.fillRect (r);
    os8::innerShadow (g, shape, 2.0f, 2.0f, 0, colour::white);
    os8::innerShadow (g, shape, -2.0f, -2.0f, 0, kShadowGrey);
    g.setColour (colour::black);
    g.drawRect (strokeOutside ? r.expanded (1) : r, 1);
}

/** Frames / 103:8688: white, a 1 px black stroke inside, a hard grey shadow up and left. */
void paintTrayFrame (Graphics& g, Rectangle<int> r)
{
    juce::Path shape;
    shape.addRectangle (r.toFloat());
    os8::dropShadow (g, shape, -1.0f, -1.0f, kShadowGrey);
    g.setColour (colour::white);
    g.fillRect (r);
    g.setColour (colour::black);
    g.drawRect (r, 1);
}

/** Trim `text` to `width` px with "..." — the header's lines are short by design. */
juce::String fitText (const juce::String& text, os8::TextStyle style, int width)
{
    if (os8::widthOf (text, style) <= width) return text;
    auto t = text;
    while (t.isNotEmpty() && os8::widthOf (t + "...", style) > width) t = t.dropLastCharacters (1);
    return t + "...";
}

} // namespace

// ============================================================================
// the panel's pieces
// ============================================================================

struct EffectsPanelComponent::Entry
{
    std::unique_ptr<Os8Button> button;
    const se::EffectModule* module = nullptr;
    juce::String label;
    Rectangle<int> rel;                        // inside the frame, below its top
    se::Applicability applicability = se::Applicability::Ok;
};

struct EffectsPanelComponent::Group
{
    const GroupSpec* spec = nullptr;
    std::vector<Entry> entries;
    int frameH = 0;                            // already includes the 4 px above and below
    int subTitleY = 0;                         // frame-relative, or 0 for none
    Rectangle<int> frame;                      // content coordinates, set by layoutAll()
    std::unique_ptr<Os8Toggle> autoApply;      // the 1-click group only
};

class EffectsPanelComponent::Content : public juce::Component
{
public:
    explicit Content (EffectsPanelComponent& o) : owner (o) {}

    void paint (Graphics& g) override;

    /** The open tray, in content coordinates, and what it is made of. */
    Rectangle<int> trayRect;
    fxlayout::Layout trayLayout;

private:
    void paintHeader (Graphics&);
    void paintGroup (Graphics&, const Group&);
    void paintTray (Graphics&);

    EffectsPanelComponent& owner;
};

// ============================================================================
// EffectsPanelComponent
// ============================================================================

EffectsPanelComponent::EffectsPanelComponent (se::Engine& e)
    : engine (e), runner (e)
{
    content = std::make_unique<Content> (*this);

    // The column is 268 px wide and the frames are 270: the scroll bar would shift
    // every frame, so the column scrolls by wheel and trackpad alone.
    viewport.setScrollBarsShown (false, false, true, false);
    viewport.setViewedComponent (content.get(), false);
    addAndMakeVisible (viewport);

    rebuildButtons();
    AppPrefs::get().addListener (this);
}

EffectsPanelComponent::~EffectsPanelComponent()
{
    AppPrefs::get().removeListener (this);
}

const se::SoundDocument* EffectsPanelComponent::targetDoc() const
{
    if (targetId == 0) return nullptr;
    auto* w = const_cast<se::Engine&> (engine).windowById (targetId);
    return w != nullptr ? w->uiDoc.get() : nullptr;
}

void EffectsPanelComponent::appPrefChanged (const juce::String& key)
{
    if (key != AppPrefs::autoApplyKey) return;
    for (auto& g : groups)
        if (g->autoApply != nullptr)
            g->autoApply->setToggleState (AppPrefs::get().autoApply(), juce::dontSendNotification);
}

void EffectsPanelComponent::setTargetWindow (uint64_t windowId)
{
    if (targetId == windowId) return;
    targetId = windowId;
    resultLine.clear();
    lastScopeWindow = 0;
    refresh();
}

void EffectsPanelComponent::refresh()
{
    const se::SoundDocument* doc = targetDoc();

    // Only recompute when something the readout actually depends on has moved.
    const size_t selSt = doc ? doc->selSt : 0;
    const size_t selEnd = doc ? doc->selEnd : 0;
    const size_t maxSize = doc ? doc->maxTrackSize() : 0;
    const int firstTrack = doc ? doc->firstSelTrack : 0;
    const int lastTrack = doc ? doc->lastSelTrack : 0;
    const int numTracks = doc ? doc->numTracks() : 0;

    if (targetId == lastScopeWindow && selSt == lastSelSt && selEnd == lastSelEnd
        && maxSize == lastMaxSize && firstTrack == lastFirstTrack
        && lastTrack == lastLastTrack && numTracks == lastNumTracks)
        return;

    lastScopeWindow = targetId;
    lastSelSt = selSt;
    lastSelEnd = selEnd;
    lastMaxSize = maxSize;
    lastFirstTrack = firstTrack;
    lastLastTrack = lastTrack;
    lastNumTracks = numTracks;

    // A track added or removed can make a two-track module become applicable, so the
    // whole set of buttons is re-derived, not just the status line.
    for (auto& g : groups)
        for (auto& e : g->entries)
        {
            e.applicability = e.module != nullptr ? runner.check (targetId, *e.module) : se::Applicability::NoWindow;
            e.button->setGreyed (e.applicability != se::Applicability::Ok);
        }

    if (openModule != nullptr)
    {
        openApplicability = runner.check (targetId, *openModule);
        if (applyButton != nullptr)
            applyButton->setGreyed (openApplicability != se::Applicability::Ok);
    }

    updateStatus();
    content->repaint();
}

void EffectsPanelComponent::updateStatus()
{
    const se::SoundDocument* doc = targetDoc();

    if (doc == nullptr || doc->maxTrackSize() == 0)
    {
        scopeLine.clear();
        return;
    }

    const se::Scope s = runner.scope (targetId);
    const int tracks = s.lastTrack - s.firstTrack + 1;
    scopeLine = juce::String (s.wholeWindow ? "Whole sound" : "Selection")
              + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + juce::String (tracks)
              + (tracks == 1 ? " track" : " tracks");
}

void EffectsPanelComponent::showResult (const juce::String& message)
{
    resultLine = message;
    // The scope readout is derived from the document the run just changed, so it has
    // to be recomputed rather than left saying what was true before.
    lastScopeWindow = 0;
    content->repaint();
}

// --- the buttons ---------------------------------------------------------------

void EffectsPanelComponent::rebuildButtons()
{
    groups.clear();

    for (const auto& spec : kGroups)
    {
        auto g = std::make_unique<Group>();
        g->spec = &spec;

        for (int i = 0; i < spec.count; ++i)
        {
            Entry e;
            e.module = se::findEffect (spec.specs[i].module);
            e.label = spec.specs[i].label;
            e.applicability = e.module != nullptr ? runner.check (targetId, *e.module) : se::Applicability::NoWindow;
            e.button = std::make_unique<Os8Button> (e.label, os8::ButtonKind::secondary);
            e.button->setWantsKeyboardFocus (false);
            e.button->setMouseClickGrabsKeyboardFocus (false);
            e.button->setGreyed (e.applicability != se::Applicability::Ok);
            content->addAndMakeVisible (*e.button);
            g->entries.push_back (std::move (e));
        }

        // The one-click group opens with the Auto Apply switch.
        if (&spec == &kGroups[0])
        {
            g->autoApply = std::make_unique<Os8Toggle> ("Auto Apply", Os8Toggle::Shape::checkbox);
            g->autoApply->setWantsKeyboardFocus (false);
            g->autoApply->setMouseClickGrabsKeyboardFocus (false);
            g->autoApply->setToggleState (AppPrefs::get().autoApply(), juce::dontSendNotification);
            g->autoApply->onClick = [this, t = g->autoApply.get()] { AppPrefs::get().setAutoApply (t->getToggleState()); };
            content->addAndMakeVisible (*g->autoApply);
        }

        // Flow the buttons into rows of the group's 252 px: greedy, 4 px apart.
        const bool oneClick = g->autoApply != nullptr;
        int row = oneClick ? 2 : 1;      // the first row below the title (and the switch)
        int x = 0;
        for (int i = 0; i < (int) g->entries.size(); ++i)
        {
            auto& e = g->entries[(size_t) i];

            if (i == spec.subStart)
            {
                if (x > 0) ++row;
                g->subTitleY = kGroupTop + 4 + row * kRowStride + 5;
                ++row;
                x = 0;
            }

            const int w = spec.specs[i].width;
            if (x > 0 && x + w > kRowWidth) { ++row; x = 0; }
            e.rel = { kGroupX + x, kGroupTop + 4 + row * kRowStride, w, kButtonH };
            x += w + kButtonGap;
        }

        // 4 px above the instance, its last row, 8 px of its own padding, 4 px below.
        g->frameH = kRowStride * row + 40;
        groups.push_back (std::move (g));
    }

    layoutAll();
}

void EffectsPanelComponent::layoutAll()
{
    int y = kHeaderH;
    content->trayRect = {};

    for (auto& g : groups)
    {
        g->frame = { 0, y, kFrameW, g->frameH };

        for (auto& e : g->entries)
            e.button->setBounds (e.rel.translated (0, y));

        if (g->autoApply != nullptr)
            g->autoApply->setBounds (kGroupX, y + kGroupTop + 4 + kRowStride + 4, g->autoApply->preferredWidth(), os8::kToggleSize);

        y += g->frameH;

        // The tray goes under the group that holds its button.
        bool holdsOpen = false;
        for (auto& e : g->entries) holdsOpen = holdsOpen || (openModule != nullptr && e.module == openModule);
        if (holdsOpen)
        {
            content->trayLayout = openModule != nullptr && fxlayout::layoutFor (openModule->name) != nullptr
                                ? *fxlayout::layoutFor (openModule->name)
                                : fxlayout::Layout { openModule->name, {} };
            const int h = content->trayLayout.contentHeight() + 2 * kTrayInset;
            content->trayRect = { 0, y, kFrameW, h };
            y += h;

            if (applyButton != nullptr)
            {
                const int cy = content->trayRect.getY() + kTrayInset + content->trayLayout.contentHeight() - fxlayout::kButtonsH;
                applyButton->setBounds (kTrayInset + 199, cy + 4, 53, os8::buttonHeight (os8::ButtonKind::primary));
                resetButton->setBounds (kTrayInset + 139, cy + 6, 50, os8::buttonHeight (os8::ButtonKind::secondary));
            }
        }
    }

    content->setSize (viewport.getMaximumVisibleWidth(), std::max (y, viewport.getHeight()));
}

void EffectsPanelComponent::effectClicked (Entry& e)
{
    if (e.module == nullptr)
    {
        showResult (e.label + " is not available yet");
        return;
    }

    if (e.applicability != se::Applicability::Ok)
    {
        showResult (juce::String (se::applicabilityText (e.applicability, e.module->requires)));
        return;
    }

    // An effect with something to ask opens its tray; one with nothing to ask applies.
    if (e.module->schema)
    {
        openEffect (e.module);
        return;
    }

    const se::Settings settings = e.module->defaults ? e.module->defaults() : se::Settings{};
    if (runModule (*e.module, settings)) e.button->flashApplied();
}

void EffectsPanelComponent::openEffect (const se::EffectModule* module)
{
    // Clicking the open effect's button closes it: a tray you cannot dismiss has taken
    // the column hostage.
    if (openModule == module) { closeTray(); return; }

    closeTray();
    openModule = module;
    resultLine.clear();
    working = module->defaults ? module->defaults() : se::Settings{};
    openApplicability = runner.check (targetId, *module);

    applyButton = std::make_unique<Os8Button> ("Apply", os8::ButtonKind::primary);
    applyButton->setWantsKeyboardFocus (false);
    applyButton->setMouseClickGrabsKeyboardFocus (false);
    applyButton->setGreyed (openApplicability != se::Applicability::Ok);
    applyButton->onClick = [this] { applyOpen(); };
    content->addAndMakeVisible (*applyButton);

    resetButton = std::make_unique<Os8Button> ("Reset", os8::ButtonKind::secondary);
    resetButton->setWantsKeyboardFocus (false);
    resetButton->setMouseClickGrabsKeyboardFocus (false);
    resetButton->onClick = [this]
    {
        if (openModule != nullptr) working = openModule->defaults ? openModule->defaults() : se::Settings{};
        content->repaint();
    };
    content->addAndMakeVisible (*resetButton);

    layoutAll();
    content->repaint();
}

juce::Rectangle<int> EffectsPanelComponent::openEffectNamed (const juce::String& moduleName)
{
    const auto* module = se::findEffect (moduleName.toStdString());
    if (module == nullptr) return {};
    if (openModule != nullptr) closeTray();
    openEffect (module);
    return content->trayRect;
}

juce::Rectangle<int> EffectsPanelComponent::groupFrame (int index) const
{
    return juce::isPositiveAndBelow (index, (int) groups.size()) ? groups[(size_t) index]->frame
                                                                 : juce::Rectangle<int>();
}

void EffectsPanelComponent::closeTray()
{
    openModule = nullptr;
    applyButton.reset();
    resetButton.reset();
    layoutAll();
    content->repaint();
}

void EffectsPanelComponent::applyOpen()
{
    if (openModule == nullptr) return;

    if (openApplicability != se::Applicability::Ok)
    {
        showResult (juce::String (se::applicabilityText (openApplicability, openModule->requires)));
        return;
    }

    if (runModule (*openModule, working) && applyButton != nullptr)
        applyButton->flashApplied();
}

bool EffectsPanelComponent::applyFromKeyboard()
{
    if (openModule == nullptr || openApplicability != se::Applicability::Ok)
        return false;

    applyOpen();
    return true;
}

bool EffectsPanelComponent::runModule (const se::EffectModule& module, const se::Settings& settings)
{
    const se::EffectRunner::Outcome outcome = runner.apply (targetId, module, settings, false);

    if (! outcome.ran)
    {
        showResult ("Could not apply");
        return false;
    }

    // Something the module said while running outranks the outcome: "it applied" is
    // true but useless next to Weird Tempo's own apology for the noise it just made.
    if (! outcome.notices.empty())
    {
        showResult (juce::String (outcome.notices.front()));
        if (onDocumentChanged) onDocumentChanged (targetId);
        return true;
    }

    // The module's own STR# 1000 voice wins when it has one (§7.7).
    juce::String say;
    bool ok = false;
    if (! outcome.message.empty())                  say = juce::String (outcome.message);
    else if (outcome.result == se::kModCancel)      say = juce::String (module.name) + ": nothing to do";
    else if (se::isError (outcome.result))          say = juce::String (module.name) + " failed";
    else                                            { say = juce::String (module.name) + " applied"; ok = true; }

    if (! outcome.spawnedWindows.empty())
        say << "  (" << (int) outcome.spawnedWindows.size() << " new)";

    showResult (say);
    if (onDocumentChanged) onDocumentChanged (targetId);
    return ok;
}

bool EffectsPanelComponent::keyPressed (const juce::KeyPress& k)
{
    // Bare Return only: ⌘Return and friends belong to whatever else claims them.
    if (k.getKeyCode() == juce::KeyPress::returnKey && ! k.getModifiers().isAnyModifierKeyDown())
        return applyFromKeyboard();

    return false;
}

void EffectsPanelComponent::paint (Graphics& g)
{
    g.fillAll (colour::gray000);
}

void EffectsPanelComponent::resized()
{
    viewport.setBounds (getLocalBounds());
    layoutAll();
}

// ============================================================================
// Content
// ============================================================================

void EffectsPanelComponent::Content::paintHeader (Graphics& g)
{
    paintBevelFrame (g, { 0, 0, kFrameW, kHeaderH }, false);

    const se::SoundDocument* doc = owner.targetDoc();
    auto* w = owner.targetId != 0 ? owner.engine.windowById (owner.targetId) : nullptr;

    // The mixer slot, boxed, as in the Sound window's own status row.
    const Rectangle<int> box (9, 27, 16, 16);
    g.setColour (colour::black);
    g.drawRect (box, 1);
    os8::drawText (g, w != nullptr && w->slot > 0 ? juce::String (w->slot) : juce::String ("-"),
                   os8::TextStyle::xSmallLabel,
                   { (float) box.getX() + 1.0f, (float) box.getY() + 2.0f, 14.0f, 9.0f },
                   juce::Justification::horizontallyCentred);

    if (doc == nullptr)
    {
        g.setColour (colour::gray400);
        os8::drawText (g, "No sound selected", os8::TextStyle::headline, { 9.0f, 8.0f, 126.0f, 12.0f });
        return;
    }

    g.setColour (colour::black);
    os8::drawText (g, fitText (juce::String (doc->name), os8::TextStyle::headline, 126),
                   os8::TextStyle::headline, { 9.0f, 8.0f, 126.0f, 12.0f });

    // Rate, bits, tracks, length — 6 px apart, as the Window's own row.
    const double seconds = doc->rateHz > 0.0 && doc->frameSize() > 0
                         ? (double) doc->maxTrackSize() / (double) doc->frameSize() / doc->rateHz : 0.0;
    const juce::String parts[] = {
        juce::String (doc->rateHz / 1000.0, 1) + "khz",
        juce::String (doc->bps) + "bits",
        juce::String (doc->numTracks()) + (doc->numTracks() == 1 ? " track" : " tracks"),
        juce::String (seconds, 2) + "s"
    };
    int x = box.getRight() + 6;
    for (const auto& part : parts)
    {
        const int pw = os8::widthOf (part, os8::TextStyle::smallLabel);
        os8::drawText (g, part, os8::TextStyle::smallLabel, { (float) x, 30.0f, (float) pw, 10.0f });
        x += pw + 6;
    }

    // The right half of the name row — the design leaves it empty — says what Apply
    // would eat, or what the last Apply did.
    const bool result = owner.resultLine.isNotEmpty();
    const auto line = result ? owner.resultLine : owner.scopeLine;
    if (line.isNotEmpty())
    {
        g.setColour (result ? colour::black : colour::gray500);
        os8::drawText (g, fitText (line, os8::TextStyle::smallLabel, 126), os8::TextStyle::smallLabel,
                       { 135.0f, 9.0f, 126.0f, 10.0f });
    }
}

void EffectsPanelComponent::Content::paintGroup (Graphics& g, const Group& group)
{
    paintBevelFrame (g, group.frame, true);

    g.setColour (colour::black);
    const int x = group.frame.getX() + kGroupX;
    os8::drawText (g, juce::String::fromUTF8 (group.spec->title), os8::TextStyle::smallLabel,
                   { (float) x, (float) group.frame.getY() + kTitleY, 200.0f, 10.0f });

    if (group.subTitleY > 0)
        os8::drawText (g, group.spec->subTitle, os8::TextStyle::smallLabel,
                       { (float) x, (float) (group.frame.getY() + group.subTitleY), 200.0f, 10.0f });
}

void EffectsPanelComponent::Content::paintTray (Graphics& g)
{
    paintTrayFrame (g, trayRect);

    const int ox = trayRect.getX() + kTrayInset, oy = trayRect.getY() + kTrayInset;

    // The title row, ruled in grey beneath.
    g.setColour (colour::black);
    os8::drawText (g, trayLayout.title, os8::TextStyle::smallLabel, { (float) ox, (float) oy + 4.0f, 200.0f, 10.0f });
    g.setColour (colour::gray400);
    g.fillRect (ox, oy + fxlayout::kTitleH - 1, fxlayout::kContentW, 1);
}

void EffectsPanelComponent::Content::paint (Graphics& g)
{
    g.fillAll (colour::gray000);

    paintHeader (g);
    for (auto& grp : owner.groups) paintGroup (g, *grp);
    if (! trayRect.isEmpty()) paintTray (g);
}
