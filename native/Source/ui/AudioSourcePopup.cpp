#include "AudioSourcePopup.h"

#include "MixerComponent.h"

#include <cmath>

namespace audiosource {

namespace {

constexpr int kRowsHeightBase = 78 + 22;   // Figma's 78 (Canvas, DAW) + the Clean row
constexpr int kSettingsRow = 28;           // External's extra row

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
                                          kRowsHeightBase + (showSettings ? kSettingsRow : 0));
    auto& p = *b.popup;

    // --- Monitor, Gain: [slider] [100] % (a 26 px row) ---
    auto monitor = std::make_unique<Os8Toggle> ("Monitor", Os8Toggle::Shape::checkbox);
    monitor->setWantsKeyboardFocus (false);
    monitor->setToggleState (choice->monitor, juce::dontSendNotification);
    auto* mon = p.addContent (std::move (monitor), { 0, 7, 0, os8::kToggleSize });
    mon->setSize (mon->preferredWidth(), os8::kToggleSize);
    mon->onClick = [choice, mon] { choice->monitor = mon->getToggleState(); };
    // Monitoring a Canvas source would only double what is already audible.
    mon->setEnabled (! canvas);
    if (canvas) mon->setToggleState (false, juce::dontSendNotification);

    p.addLabel ("Gain:", 65.0f, 8.0f);

    auto slider = std::make_unique<Os8Slider> (os8::SliderKind::volume);
    slider->setRange (0.0, 1.0);
    slider->setMarkerValue ((double) percentToPos (100.0));
    slider->setDoubleClickReturnValue (true, (double) percentToPos (100.0));
    slider->setValue ((double) percentToPos (choice->gainPercent), juce::dontSendNotification);
    auto* sl = p.addContent (std::move (slider), { 96, 4, 90, os8::kSliderHeight });

    auto field = std::make_unique<Os8TextField> (os8::FieldSize::small);
    field->setNumeric (1.0, 0, 0.0, 200.0);
    field->setValue (choice->gainPercent);
    auto* fl = p.addContent (std::move (field), { 190, 5, 41, 16 });
    p.addLabel ("%", 235.0f, 8.0f);

    sl->onValueChange = [choice, sl, fl]
    {
        choice->gainPercent = std::round (posToPercent (sl->getValue()));
        fl->setValue (choice->gainPercent);
    };
    fl->onCommit = [choice, sl, fl] (const juce::String&)
    {
        choice->gainPercent = std::clamp (fl->getValue(), 0.0, 200.0);
        sl->setValue ((double) percentToPos (choice->gainPercent), juce::dontSendNotification);
    };

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
        auto* m = p.addContent (std::move (menu), { 84, 57, 166, os8::kDropdownHeight });
        m->onChange = [choice, external, canvasIds] (int i)
        {
            if (external) choice->externalIndex = i;
            else if (i >= 0 && i < (int) canvasIds.size()) choice->canvasWindow = canvasIds[(size_t) i];
        };
    }

    // The kind changed: the popup has another shape, so it is shown again.
    auto* pop = &p;
    km->onChange = [choice, kinds, pop] (int i)
    {
        choice->kind = kinds[(size_t) std::clamp (i, 0, (int) kinds.size() - 1)];
        pop->dismiss (2);
    };

    int y = 57 + os8::kDropdownHeight + 4;   // under the pair
    if (showSettings)
    {
        auto button = std::make_unique<Os8Button> ("Audio/Midi Settings");
        button->setWantsKeyboardFocus (false);
        auto open = ctx.openAudioSettings;
        // Apply nothing: the settings dialog is the host's own, opened over the popup's window.
        button->onClick = [open] { open(); };
        p.addContent (std::move (button), { 0, 82, 143, 20 });
        y += kSettingsRow;
    }

    auto clean = std::make_unique<Os8Toggle> ("Clean conversion (band-limited, dithered)", Os8Toggle::Shape::checkbox);
    clean->setWantsKeyboardFocus (false);
    clean->setToggleState (choice->clean, juce::dontSendNotification);
    auto* cl = p.addContent (std::move (clean), { 0, y + 3, 0, os8::kToggleSize });
    cl->setSize (cl->preferredWidth(), os8::kToggleSize);
    cl->onClick = [choice, cl] { choice->clean = cl->getToggleState(); };

    p.setButtons ("Cancel", "Apply");
    p.onDismiss = [choice, apply, reopen] (int result)
    {
        if (result == 1)      { if (apply) apply (*choice); }
        else if (result == 2) { if (reopen) reopen (*choice); }
    };
    return b;
}

} // namespace audiosource
