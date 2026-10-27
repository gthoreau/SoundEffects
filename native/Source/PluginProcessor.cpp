#include "PluginProcessor.h"

#include "PluginEditor.h"

SoundEffectsAudioProcessor::SoundEffectsAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true))
{
    // Seed the desktop with one window so there is something to hear/see. The UI
    // will let the user spawn, move, mosaic and trash windows on the canvas.
    engineImpl.addWindow (std::make_unique<se::SoundDocument> (se::newSoundInit()));

    // Reclaim Windows the audio thread retires (deletes happen here, msg thread).
    startTimerHz (30);   // retired Windows, and the recorder drains the capture ring
}

SoundEffectsAudioProcessor::~SoundEffectsAudioProcessor()
{
    stopTimer();
    cancelPendingUpdate();   // no restore may land on a half-destroyed engine
}

// ================================ state =====================================

void SoundEffectsAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Reads each Window's UI document, which the message thread owns — so this
    // expects to be called there, as both the standalone (save / quit) and every
    // host we target do. There is no marshalling escape on this side: the host
    // wants the bytes before the call returns.
    const auto blob = se::serializeEngine (engineImpl);
    destData.setSize (0);
    destData.append (blob.data(), blob.size());
}

void SoundEffectsAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0) return;

    se::EngineState state;
    if (! se::parseEngineState (data, static_cast<size_t> (sizeInBytes), state))
        return;   // not ours, or damaged: keep the canvas the user already has

    loadState (std::move (state));
}

void SoundEffectsAudioProcessor::loadState (se::EngineState state)
{
    {
        const std::lock_guard<std::mutex> lock (pendingStateLock);
        pendingState = std::move (state);
    }

    if (juce::MessageManager::existsAndIsCurrentThread())
    {
        // Already where the engine wants us. Apply now rather than next message,
        // so a host that loads state and immediately asks what we contain — or the
        // standalone, whose editor is built right after — sees the restored canvas.
        cancelPendingUpdate();
        handleAsyncUpdate();
    }
    else
    {
        triggerAsyncUpdate();
    }
}

se::EngineState SoundEffectsAudioProcessor::captureState()
{
    return se::captureEngineState (engineImpl);
}

bool SoundEffectsAudioProcessor::importState (const se::EngineState& state)
{
    return se::importEngineState (engineImpl, state);
}

void SoundEffectsAudioProcessor::handleAsyncUpdate()
{
    se::EngineState state;
    {
        const std::lock_guard<std::mutex> lock (pendingStateLock);
        state = std::move (pendingState);
        pendingState = {};
    }

    se::applyEngineState (engineImpl, state);
}

void SoundEffectsAudioProcessor::timerCallback()
{
    engineImpl.collectRetired();
    recorderImpl.service();
}

void SoundEffectsAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engineImpl.prepare (sampleRate, samplesPerBlock);
    inputCopy.setSize (std::max (1, getTotalNumInputChannels()), std::max (1, samplesPerBlock), false, true, false);
}

bool SoundEffectsAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    // As an audio effect the input should match the output; as an instrument the
    // input bus may be disabled by the host.
    const auto& in = layouts.getMainInputChannelSet();
    return in == out || in.isDisabled();
}

void SoundEffectsAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer& /*midi*/)
{
    juce::ScopedNoDenormals noDenormals;

    const int numFrames = buffer.getNumSamples();
    auto* left  = buffer.getWritePointer (0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : left;

    // The device / host input, copied aside first: the buffer is shared with the
    // output, which render() is about to overwrite. Always copied (a few channels
    // by one block), whether or not a recording is running, so the audio thread
    // never branches on UI state here.
    const int numIn = std::min (getTotalNumInputChannels(), inputCopy.getNumChannels());
    const float* inputs[8] {};
    const int nIn = std::min (numIn, 8);
    if (numFrames <= inputCopy.getNumSamples())
        for (int ch = 0; ch < nIn; ++ch)
        {
            inputCopy.copyFrom (ch, 0, buffer, ch, 0, numFrames);
            inputs[ch] = inputCopy.getReadPointer (ch);
        }

    // The engine sums every open window into the output. (When numChannels == 1,
    // left == right, so the render collapses to mono at the buffer edge.)
    engineImpl.render (left, right, numFrames, inputs, numFrames <= inputCopy.getNumSamples() ? nIn : 0);

    // Clear any extra output channels beyond stereo.
    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numFrames);
}

juce::AudioProcessorEditor* SoundEffectsAudioProcessor::createEditor()
{
    return new SoundEffectsAudioProcessorEditor (*this);
}

// The plugin/standalone entry point.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SoundEffectsAudioProcessor();
}
