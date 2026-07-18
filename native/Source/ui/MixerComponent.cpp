#include "MixerComponent.h"

#include <algorithm>

using juce::Colour;

MixerComponent::MixerComponent (se::Engine& e) : engine (e) {}

MixerComponent::Strip& MixerComponent::makeStrip (uint64_t id, const juce::String& label)
{
    auto s = std::make_unique<Strip>();
    s->id = id;

    s->fader = std::make_unique<juce::Slider> (juce::Slider::LinearVertical, juce::Slider::NoTextBox);
    s->fader->setRange (-96.0, 6.0, 0.1);
    s->fader->setValue (0.0, juce::dontSendNotification);   // 0 dB = unity
    s->fader->onValueChange = [this, id]
    {
        if (auto* w = engine.windowById (id))
            for (auto& st : strips)
                if (st->id == id)
                    w->gain.store (se::Engine::dbToGain ((float) st->fader->getValue()));
    };
    addAndMakeVisible (*s->fader);

    s->mute = std::make_unique<juce::TextButton> ("M");
    s->mute->setClickingTogglesState (true);
    s->mute->onClick = [this, id]
    {
        if (auto* w = engine.windowById (id))
            for (auto& st : strips)
                if (st->id == id)
                    w->muted.store (st->mute->getToggleState());
    };
    addAndMakeVisible (*s->mute);

    s->name = std::make_unique<juce::Label> (juce::String(), label);
    s->name->setJustificationType (juce::Justification::centred);
    s->name->setFont (juce::FontOptions (10.0f));
    addAndMakeVisible (*s->name);

    strips.push_back (std::move (s));
    return *strips.back();
}

void MixerComponent::refresh()
{
    const auto& ids = engine.windowIds();

    // Drop strips whose Window is gone.
    strips.erase (std::remove_if (strips.begin(), strips.end(),
                    [&ids] (const std::unique_ptr<Strip>& s)
                    { return std::find (ids.begin(), ids.end(), s->id) == ids.end(); }),
                  strips.end());

    // Add strips for new Windows, in engine order.
    for (uint64_t id : ids)
    {
        const bool have = std::any_of (strips.begin(), strips.end(),
                                       [id] (const std::unique_ptr<Strip>& s) { return s->id == id; });
        if (! have)
        {
            juce::String label = "win";
            if (auto* w = engine.windowById (id)) label = juce::String (w->uiDoc->name);
            makeStrip (id, label);
        }
    }

    resized();
}

void MixerComponent::paint (juce::Graphics& g)
{
    g.fillAll (Colour (0xffb0b0b0));
    g.setColour (juce::Colours::black);
    g.drawRect (getLocalBounds(), 1);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("Mixer", getLocalBounds().removeFromTop (18), juce::Justification::centred, false);
}

void MixerComponent::resized()
{
    auto area = getLocalBounds().withTrimmedTop (20).reduced (4);
    int x = area.getX();
    for (auto& s : strips)
    {
        juce::Rectangle<int> col (x, area.getY(), kStripW - 6, area.getHeight());
        s->name->setBounds (col.removeFromTop (16));
        s->mute->setBounds (col.removeFromBottom (20).reduced (8, 0));
        s->fader->setBounds (col.reduced (10, 4));
        x += kStripW;
    }
}
