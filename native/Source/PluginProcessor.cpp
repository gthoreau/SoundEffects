#include "PluginProcessor.h"

#include "PluginEditor.h"

SoundEffectAudioProcessor::SoundEffectAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true))
{
    // Seed the desktop with one window so there is something to hear/see. The UI
    // will let the user spawn, move, mosaic and trash windows on the canvas.
    se::DocumentInit init;
    init.name = "untitled sound #1";
    init.numTracks = 1;
    engineImpl.addWindow (std::make_unique<se::SoundDocument> (init));

    // Reclaim Windows the audio thread retires (deletes happen here, msg thread).
    startTimerHz (5);
}

SoundEffectAudioProcessor::~SoundEffectAudioProcessor()
{
    stopTimer();
}

void SoundEffectAudioProcessor::timerCallback()
{
    engineImpl.collectRetired();
}

void SoundEffectAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engineImpl.prepare (sampleRate, samplesPerBlock);
}

bool SoundEffectAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    // As an audio effect the input should match the output; as an instrument the
    // input bus may be disabled by the host.
    const auto& in = layouts.getMainInputChannelSet();
    return in == out || in.isDisabled();
}

void SoundEffectAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer& /*midi*/)
{
    juce::ScopedNoDenormals noDenormals;

    const int numFrames = buffer.getNumSamples();
    auto* left  = buffer.getWritePointer (0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : left;

    // The engine sums every open window into the output. (When numChannels == 1,
    // left == right, so the render collapses to mono at the buffer edge.)
    engineImpl.render (left, right, numFrames);

    // Clear any extra output channels beyond stereo.
    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numFrames);
}

juce::AudioProcessorEditor* SoundEffectAudioProcessor::createEditor()
{
    return new SoundEffectAudioProcessorEditor (*this);
}

// The plugin/standalone entry point.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SoundEffectAudioProcessor();
}
