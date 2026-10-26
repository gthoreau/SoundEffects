// PluginProcessor — the JUCE boundary. This is the ONLY layer that knows about
// JUCE / the host. It owns the host-agnostic se::Engine and pumps it from
// processBlock, in both the standalone build and the VST3/AU/AAX plugin builds
// (the VCV Rack Pro delivery model).

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "engine/Engine.h"
#include "engine/EngineState.h"

#include <mutex>

class SoundEffectsAudioProcessor : public juce::AudioProcessor,
                                  private juce::Timer,
                                  private juce::AsyncUpdater
{
public:
    SoundEffectsAudioProcessor();
    ~SoundEffectsAudioProcessor() override;

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

    // State here is the user's WORK — every open Window, its place and colour on
    // the canvas, and the sound inside it. See engine/EngineState.h for the format
    // and for which of the three steps runs on which thread.
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    /** The engine the editor draws and edits. */
    se::Engine& engine() noexcept { return engineImpl; }

private:
    void timerCallback() override;   // message thread: reclaim retired Windows

    // A host may call setStateInformation from any thread, but the engine's window
    // set may only be rebuilt from the message thread. So the blob is DECODED
    // wherever the call lands (parsing touches nothing shared) and only the apply
    // is marshalled here. AsyncUpdater is what makes that safe against teardown:
    // its destructor cancels a pending callback, so a processor deleted between
    // the trigger and the callback cannot be applied into.
    void handleAsyncUpdate() override;

    std::mutex        pendingStateLock;
    se::EngineState   pendingState;

    se::Engine engineImpl;
    juce::AudioBuffer<float> inputCopy;   // the input channels, aside from the output

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SoundEffectsAudioProcessor)
};
