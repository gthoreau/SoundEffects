#include "AudioSourcePopup.h"

#include "MixerComponent.h"

#include <cmath>

namespace audiosource {

namespace {

constexpr int kRowsHeightBase = 78;        // down to under the dropdown pair
constexpr int kSettingsRow = 28;           // External's extra row
constexpr int kCleanRow = 20;

float percentToPos (double percent) noexcept
{
    const double g = std::max (percent, 0.0) / 100.0;
    return MixerStrip::dbToPos (g <= 0.0 ? -96.0f : 20.0f * std::log10 ((float) g));
}

double posToPercent (double pos) noexcept
{
    return 100.0 * (double) se::Engine::dbToGain (MixerStrip::posToDb ((float) pos));
}

const char* kindName (se::AudioSourceKind k)
{
    switch (k)
    {
        case se::AudioSourceKind::external: return "External";
        case se::AudioSourceKind::canvas:   return "Canvas";
        case se::AudioSourceKind::daw:      return "DAW";
    }
    return "";
}

/** The gain slider with the source's VU in its trough. It keeps the engine listening
    to the source for as long as it lives. */
class SourceSlider : public Os8Slider,
                     private juce::Timer
{
public:
    explicit SourceSlider (const Context& c)
        : Os8Slider (os8::SliderKind::volume), engine (c.engine), owner (c.owner) {}

    ~SourceSlider() override
    {
        if (auto* e = live()) e->probeStop();
    }

    void listenTo (const se::CaptureSetup& source, double gainPercent)
    {
        if (auto* e = live())
        {
            setGain (gainPercent);
            e->probeStart (source);
            startTimerHz (30);
        }
    }

    /** Called with each meter reading, while the engine is there. */
    std::function<void()> onTick;

    void setGain (double percent)
    {
        if (auto* e = live()) e->setProbeGain ((float) (percent / 100.0));
    }

private:
    se::Engine* live() const noexcept { return owner != nullptr ? engine : nullptr; }

    void timerCallback() override
    {
        auto* e = live();
        if (e == nullptr) { stopTimer(); return; }
        // A Mixer strip's mapping: the bar ends where the fader would sit at that level.
        const auto bar = [this] (float amp)
        {
            if (amp <= 0.001f) return 0.0f;
            const float wdt = (float) std::max (1, getWidth());
            const float half = (float) os8::kThumbWidth * 0.5f;
            const float pos = MixerStrip::dbToPos (20.0f * std::log10 (amp));
            return (half + pos * (wdt - 2.0f * half)) / wdt;
        };
        setLevels (bar (e->probeMeterLeft()), bar (e->probeMeterRight()));
        if (onTick) onTick();
    }

    se::Engine* engine;
    juce::Component::SafePointer<juce::Component> owner;
};

/** What a capture of `choice` would take: the source the VU listens to. */
se::CaptureSetup captureOf (const Context& ctx, const SourceChoice& choice)
{
    se::CaptureSetup s;
    s.kind = choice.kind;
    if (choice.kind == se::AudioSourceKind::canvas)
    {
        s.tapWindow = choice.canvasWindow;
        s.excludeWindow = ctx.window;
    }
    else if (choice.kind == se::AudioSourceKind::external)
    {
        const auto list = externalSources (ctx.numInputChannels);
        s.firstChannel = -1;        // no input: silence
        if (! list.empty())
        {
            const auto& src = list[(size_t) std::clamp (choice.externalIndex, 0, (int) list.size() - 1)];
            s.firstChannel = src.firstChannel;
            s.stereo = src.stereo;
        }
    }
    return s;
}

} // namespace

std::vector<se::AudioSourceKind> offeredKinds (bool daw)
{
    std::vector<se::AudioSourceKind> k { se::AudioSourceKind::external, se::AudioSourceKind::canvas };
    if (daw) k.push_back (se::AudioSourceKind::daw);
    return k;
}

std::vector<ExternalSource> externalSources (int n)
{
    std::vector<ExternalSource> out;
    for (int c = 0; c + 1 < n; c += 2)
        out.push_back ({ "Input " + juce::String (c + 1) + " + " + juce::String (c + 2), c, true });
    for (int c = 0; c < n; ++c)
        out.push_back ({ "Input " + juce::String (c + 1), c, false });
    return out;
}

Built build (const Context& ctx, const SourceChoice& current,
             std::function<void (const SourceChoice&)> apply,
             std::function<void (const SourceChoice&)> reopen)
{
    auto choice = std::make_shared<SourceChoice> (current);
    const bool external = choice->kind == se::AudioSourceKind::external;
    const bool canvas   = choice->kind == se::AudioSourceKind::canvas;
    const bool showSettings = external && ctx.openAudioSettings != nullptr;

    Built b;
    b.popup = std::make_unique<Os8Popup> ("Select Audio Source",
                                          kRowsHeightBase + (showSettings ? kSettingsRow : 0)
                                              + (ctx.offersClean ? kCleanRow : 0));
    auto& p = *b.popup;

    // --- Monitor [slider] [100] % ---
    auto monitor = std::make_unique<Os8Toggle> ("Monitor", Os8Toggle::Shape::checkbox, os8::TextStyle::headline);
    monitor->setWantsKeyboardFocus (false);
    monitor->setMouseClickGrabsKeyboardFocus (false);
    monitor->setToggleState (choice->monitor, juce::dontSendNotification);
    auto* mon = p.addContent (std::move (monitor), { 0, 7, 0, os8::kToggleSize });
    mon->setSize (mon->preferredWidth(), os8::kToggleSize);
    mon->onClick = [choice, mon] { choice->monitor = mon->getToggleState(); };
    // Monitoring a Canvas source would only double what is already audible.
    mon->setEnabled (! canvas);
    if (canvas) mon->setToggleState (false, juce::dontSendNotification);

    auto slider = std::make_unique<SourceSlider> (ctx);
    slider->setRange (0.0, 1.0);
    slider->setMarkerValue ((double) percentToPos (100.0));
    slider->setDoubleClickReturnValue (true, (double) percentToPos (100.0));
    slider->setValue ((double) percentToPos (choice->gainPercent), juce::dontSendNotification);
    slider->setWantsKeyboardFocus (false);
    auto* sl = p.addContent (std::move (slider), { 98, 4, 90, os8::kSliderHeight });

    auto field = std::make_unique<Os8TextField> (os8::FieldSize::small);
    field->setNumeric (1.0, 0, 0.0, 200.0);
    field->setValue (choice->gainPercent);
    auto* fl = p.addContent (std::move (field), { 192, 5, 41, 16 });
    p.addLabel ("%", 237.0f, 8.0f, os8::TextStyle::smallLabel);

    sl->onValueChange = [choice, sl, fl]
    {
        choice->gainPercent = std::round (posToPercent (sl->getValue()));
        fl->setValue (choice->gainPercent);
        sl->setGain (choice->gainPercent);
    };
    fl->onCommit = [choice, sl, fl] (const juce::String&)
    {
        choice->gainPercent = std::clamp (fl->getValue(), 0.0, 200.0);
        sl->setValue ((double) percentToPos (choice->gainPercent), juce::dontSendNotification);
        sl->setGain (choice->gainPercent);
    };
    auto* answering = &p;
    fl->onReturn = [answering] { answering->dismiss (1); };
    fl->onEscape = [answering] { answering->dismiss (0); };

    // --- "Select Audio source:" and the dropdown pair (rows from y 34) ---
    p.addLabel ("Select Audio source:", 0.0f, 38.0f);

    const auto kinds = offeredKinds (ctx.dawAvailable);
    auto kindMenu = std::make_unique<Os8Dropdown> (Os8Dropdown::Style::standard);
    juce::StringArray kindNames;
    int kindIndex = 0;
    for (size_t i = 0; i < kinds.size(); ++i)
    {
        kindNames.add (kindName (kinds[i]));
        if (kinds[i] == choice->kind) kindIndex = (int) i;
    }
    kindMenu->setItems (kindNames);
    kindMenu->setSelectedIndex (kindIndex);
    kindMenu->setWantsKeyboardFocus (false);
    auto* km = p.addContent (std::move (kindMenu), { 0, 57, 80, os8::kDropdownHeight });

    // The second dropdown: what within the kind (none for DAW: no choice).
    std::vector<uint64_t> canvasIds;   // canvas list entry -> window id (0 = main mix)
    if (external || canvas)
    {
        auto menu = std::make_unique<Os8Dropdown> (Os8Dropdown::Style::standard);
        juce::StringArray names;
        int selected = 0;
        if (external)
        {
            const auto list = externalSources (ctx.numInputChannels);
            for (const auto& s : list) names.add (s.name);
            if (names.isEmpty()) names.add ("No input");
            selected = std::clamp (choice->externalIndex, 0, std::max (0, names.size() - 1));
            choice->externalIndex = selected;
        }
        else
        {
            names.add ("Main Mix");
            canvasIds.push_back (0);
            if (ctx.engine != nullptr)
                for (uint64_t id : ctx.engine->windowIds())
                {
                    if (id == ctx.window) continue;   // a Sound is not a source for itself
                    auto* w = ctx.engine->windowById (id);
                    if (w == nullptr || ! w->uiDoc) continue;
                    names.add ((w->slot >= 0 ? juce::String (w->slot + 1) + " - " : juce::String()) + juce::String (w->uiDoc->name));
                    canvasIds.push_back (id);
                    if (id == choice->canvasWindow) selected = names.size() - 1;
                }
            if (selected == 0) choice->canvasWindow = 0;
        }
        menu->setItems (names);
        menu->setSelectedIndex (selected);
        menu->setWantsKeyboardFocus (false);
        auto* m = p.addContent (std::move (menu), { 84, 57, 168, os8::kDropdownHeight });
        m->onChange = [choice, external, canvasIds, ctx, sl] (int i)
        {
            if (external) choice->externalIndex = i;
            else if (i >= 0 && i < (int) canvasIds.size()) choice->canvasWindow = canvasIds[(size_t) i];
            sl->listenTo (captureOf (ctx, *choice), choice->gainPercent);
        };
    }

    // The kind changed: the popup has another shape, so it is shown again.
    auto* pop = &p;
    km->onChange = [choice, kinds, pop] (int i)
    {
        choice->kind = kinds[(size_t) std::clamp (i, 0, (int) kinds.size() - 1)];
        pop->dismiss (2);
    };

    int y = kRowsHeightBase;
    if (showSettings)
    {
        auto button = std::make_unique<Os8Button> ("Audio/Midi Setup");
        button->setWantsKeyboardFocus (false);
        button->setMouseClickGrabsKeyboardFocus (false);
        auto open = ctx.openAudioSettings;
        // Apply nothing: the setup dialog is the host's own, opened over this popup.
        button->onClick = [open] { open(); };
        p.addContent (std::move (button), { 0, 82, 126, 20 });
        y += kSettingsRow;
    }

    if (ctx.offersClean)
    {
        auto clean = std::make_unique<Os8Toggle> ("Clean conversion", Os8Toggle::Shape::checkbox, os8::TextStyle::headline);
        clean->setWantsKeyboardFocus (false);
        clean->setMouseClickGrabsKeyboardFocus (false);
        clean->setToggleState (choice->clean, juce::dontSendNotification);
        auto* cl = p.addContent (std::move (clean), { 0, y + 4, 0, os8::kToggleSize });
        cl->setSize (cl->preferredWidth(), os8::kToggleSize);
        cl->onClick = [choice, cl] { choice->clean = cl->getToggleState(); };
    }

    sl->listenTo (captureOf (ctx, *choice), choice->gainPercent);
    if (ctx.liveInputChannels != nullptr)
        sl->onTick = [answering, live = ctx.liveInputChannels, shown = ctx.numInputChannels]
        {
            // Not under the setup itself: once it has gone, 2 asks to be shown again.
            if (live() != shown && ! answering->isCurrentlyBlockedByAnotherModalComponent())
                answering->dismiss (2);
        };

    p.setButtons ("Cancel", "Apply");
    p.onDismiss = [choice, apply, reopen] (int result)
    {
        if (result == 1)      { if (apply) apply (*choice); }
        else if (result == 2) { if (reopen) reopen (*choice); }
    };
    return b;
}

} // namespace audiosource
