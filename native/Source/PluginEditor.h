// PluginEditor — hosts the infinite canvas of sound Windows between two panels:
// the main mixer down the LEFT (New and Open sit at its top) and the destructive
// effects down the RIGHT. A small toolbar carries Mosaic / zoom / view state.
//
// A 30 Hz timer refreshes the canvas, the mixer and the effects panel (transport
// playhead, window set, selection scope).

#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"
#include "ui/CanvasComponent.h"
#include "ui/EffectsPanel.h"
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

    CanvasComponent       canvas;
    MixerComponent        mixer;
    EffectsPanelComponent effects;

    juce::AudioFormatManager formatManager;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SoundEffectAudioProcessorEditor)
};
