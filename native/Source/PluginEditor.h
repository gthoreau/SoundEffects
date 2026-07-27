// PluginEditor — hosts the infinite canvas of sound Windows and the main mixer,
// with a small toolbar (Mosaic / zoom / view state). New and Open sit at the top
// of the mixer panel, which owns the whole left-hand column.
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
    /** ⌘N / ⌘O (Ctrl elsewhere). Reached by bubbling: the focused Window and the
        canvas both decline the combo, so it arrives here. */
    bool keyPressed (const juce::KeyPress&) override;

private:
    void timerCallback() override;
    void openFile();
    void newWindow();
    void updateViewStatus();

    SoundEffectAudioProcessor& processorRef;

    juce::TextButton mosaicButton { "Mosaic" };
    juce::TextButton zoomResetButton { "100%" };
    juce::Label      viewStatus;

    CanvasComponent canvas;
    MixerComponent  mixer;

    juce::AudioFormatManager formatManager;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SoundEffectAudioProcessorEditor)
};
