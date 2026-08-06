// PluginEditor — hosts the infinite canvas of sound Windows between two panels:
// the main mixer down the LEFT (New and Open sit at its top) and the destructive
// effects down the RIGHT. The view controls — layout, zoom — float over the canvas
// in a palette rather than sitting in a toolbar above it.
//
// A 30 Hz timer refreshes the canvas, the mixer and the effects panel (transport
// playhead, window set, selection scope).

#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"
#include "ui/CanvasComponent.h"
#include "ui/EffectsPanel.h"
#include "ui/MixerComponent.h"
#include "ui/ViewPalette.h"

class SoundEffectAudioProcessorEditor : public juce::AudioProcessorEditor,
                                        private juce::Timer
{
public:
    explicit SoundEffectAudioProcessorEditor (SoundEffectAudioProcessor&);
    ~SoundEffectAudioProcessorEditor() override;

    /** Show the audio device settings. Only the STANDALONE app can answer this —
        it owns the device — so StandaloneApp.cpp installs it after creating the
        editor, and in a VST3/AU it stays null because the host owns the device.
        Reached from a Window's greyed record button, via the canvas. */
    std::function<void()> onShowAudioSettings;

    /** Is audio input live, or muted? Polled on the 30 Hz tick and pushed down to
        every sound Window, where it lights or greys the record button. Null in a
        VST3/AU — the host owns the device — and a null hook reads as muted, which
        greys the button and is the truthful answer there. */
    std::function<bool()> isAudioInputActive;

    void paint (juce::Graphics&) override;
    void resized() override;
    /** ⌘N / ⌘O (Ctrl elsewhere). Reached by bubbling: the focused Window and the
        canvas both decline the combo, so it arrives here. */
    bool keyPressed (const juce::KeyPress&) override;

private:
    void timerCallback() override;
    void openFile();
    void newWindow();

    SoundEffectAudioProcessor& processorRef;

    CanvasComponent       canvas;
    MixerComponent        mixer;
    EffectsPanelComponent effects;
    ViewPalette           palette;   // floats OVER the canvas, so it is added last

    juce::AudioFormatManager formatManager;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SoundEffectAudioProcessorEditor)
};
