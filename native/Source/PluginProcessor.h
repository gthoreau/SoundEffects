// PluginProcessor — the JUCE boundary. This is the ONLY layer that knows about
// JUCE / the host. It owns the host-agnostic se::Engine and pumps it from
// processBlock, in both the standalone build and the VST3/AU/AAX plugin builds
// (the VCV Rack Pro delivery model).

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "engine/Engine.h"

class SoundEffectAudioProcessor : public juce::AudioProcessor,
                                  private juce::Timer
{
public:
    SoundEffectAudioProcessor();
    ~SoundEffectAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return true; }   // "instrument" mode
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    /** The engine the editor draws and edits. */
    se::Engine& engine() noexcept { return engineImpl; }

private:
    void timerCallback() override;   // message thread: reclaim retired Windows

    se::Engine engineImpl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SoundEffectAudioProcessor)
};
