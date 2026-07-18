// MixerComponent — the main mixer: one channel strip per Window (a dB fader +
// mute), feeding the engine's global additive sum. This is the "true mixer" that
// replaces the original's dead "Level" widget.
//
// Writes go to the per-Window atomics (gain / muted), so it is safe to drive
// while audio runs. UNVERIFIED against JUCE — build locally.

#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "engine/Engine.h"

#include <memory>
#include <vector>

class MixerComponent : public juce::Component
{
public:
    explicit MixerComponent (se::Engine& engine);

    /** Sync strips to the current set of Windows. Called by the editor timer. */
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kStripW = 54;

private:
    struct Strip
    {
        uint64_t id = 0;
        std::unique_ptr<juce::Slider>     fader;   // vertical, dB
        std::unique_ptr<juce::TextButton> mute;
        std::unique_ptr<juce::Label>      name;
    };

    se::Engine& engine;
    std::vector<std::unique_ptr<Strip>> strips;

    Strip& makeStrip (uint64_t id, const juce::String& label);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerComponent)
};
