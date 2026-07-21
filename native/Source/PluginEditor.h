// PluginEditor — hosts the infinite canvas of sound Windows and the main mixer,
// with a small toolbar (New / Open / Mosaic / view state). System 7 dressing
// comes later.
//
// A 30 Hz timer refreshes the canvas + mixer (transport playhead, window set).

#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"
#include "ui/CanvasComponent.h"
#include "ui/MixerComponent.h"

class SoundEffectAudioProcessorEditor : public juce::AudioProcessorEditor,
                                        private juce::Timer
{
public:
    explicit SoundEffectAudioProcessorEditor (SoundEffectAudioProcessor&);
    ~SoundEffectAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void openFile();
    void newWindow();
    void updateViewStatus();

    SoundEffectAudioProcessor& processorRef;

    juce::TextButton newButton    { "New" };
    juce::TextButton openButton   { "Open\xE2\x80\xA6" };
    juce::TextButton mosaicButton { "Mosaic" };
    juce::TextButton zoomResetButton { "100%" };
    juce::Label      viewStatus;

    CanvasComponent canvas;
    MixerComponent  mixer;

    juce::AudioFormatManager formatManager;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SoundEffectAudioProcessorEditor)
};
