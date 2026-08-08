// PluginEditor — hosts the infinite canvas of sound Windows between two panels:
// the main mixer down the LEFT (New and Open sit at its top) and the destructive
// effects down the RIGHT. The view controls — layout, zoom — float over the canvas
// in a palette rather than sitting in a toolbar above it, and the Keyboard hangs
// under that palette as a second floating strip, shown and hidden from a button on
// the first. The two float as one stack: hide the Keyboard and the View palette
// drops back down into the space it was using.
//
// A 30 Hz timer refreshes the canvas, the mixer and the effects panel (transport
// playhead, window set, selection scope).

#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"
#include "ui/CanvasComponent.h"
#include "ui/EffectsPanel.h"
#include "ui/KeyboardPalette.h"
#include "ui/MixerComponent.h"
#include "ui/ViewPalette.h"

class SoundEffectsAudioProcessorEditor : public juce::AudioProcessorEditor,
                                        private juce::Timer
{
public:
    explicit SoundEffectsAudioProcessorEditor (SoundEffectsAudioProcessor&);
    ~SoundEffectsAudioProcessorEditor() override;

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

    // --- what the canvas can be asked to do from outside --------------------
    // The mixer's two buttons drive these, and so does the standalone's File menu.
    // They are Window-level, not canvas-level: New makes one more sound Window on
    // the canvas, Open imports an audio file into one, Close throws one away. The
    // canvas as a whole is the standalone's business (StandaloneApp.cpp), because
    // only it has a document to save the canvas INTO.

    /** Add an empty sound Window. */
    void newWindow();
    /** Import an audio file into a new sound Window. */
    void openFile();
    /** Throw the focused Window away. No-op when nothing is focused; there is no
        undo in this program, but a Window is cheap to remake and the close box has
        always done this without asking. */
    void closeFocusedWindow();
    /** Is there a focused Window for closeFocusedWindow() to act on? Drives the
        greying of the File menu's Close item. */
    bool hasFocusedWindow() const;

    void paint (juce::Graphics&) override;
    void resized() override;
    /** ⌘N / ⌘O (Ctrl elsewhere). Reached by bubbling: the focused Window and the
        canvas both decline the combo, so it arrives here.

        In the STANDALONE these two combos never get this far: the File menu binds
        them as real command keys and macOS dispatches them straight to the menu,
        which calls the same two methods. This is the path for a VST3/AU, where
        there is no menu bar of ours at all. */
    bool keyPressed (const juce::KeyPress&) override;

private:
    void timerCallback() override;
    /** Show or hide the Keyboard palette, and re-lay the floating stack. */
    void setKeyboardShown (bool shown);

    SoundEffectsAudioProcessor& processorRef;

    CanvasComponent       canvas;
    MixerComponent        mixer;
    EffectsPanelComponent effects;
    ViewPalette           palette;    // floats OVER the canvas, so it is added last
    KeyboardPalette       keyboard;   // ...and hangs directly below the palette

    juce::AudioFormatManager formatManager;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SoundEffectsAudioProcessorEditor)
};
