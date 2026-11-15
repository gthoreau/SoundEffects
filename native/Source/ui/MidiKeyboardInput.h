// MidiKeyboardInput — plays the Keyboard palette from MIDI (0.5.0 step 12).
//
// Message thread. It receives the `se::MidiEvent`s the audio thread queued and turns
// them into palette calls:
//
//   - channel 1 notes are PLAIN: one key, the master pitch wheel bends them +-2 st;
//   - everything else goes through a `juce::MPEInstrument` set to a lower zone of
//     15 member channels (per-note pitch bend range 48 st, the MPE default; a
//     controller that announces another range with an RPN is believed). Each
//     member-channel note is one finger, and its pitch slide moves the pitch of
//     the palette continuously (`KeyboardPalette::midiBend`).
//
// The MPEInstrument is not wait-free (it allocates), which is why it lives here and
// not in `processBlock`.

#pragma once

#include "engine/MidiNotes.h"

#include <juce_audio_basics/juce_audio_basics.h>

class KeyboardPalette;

class MidiKeyboardInput : private juce::MPEInstrument::Listener
{
public:
    explicit MidiKeyboardInput (KeyboardPalette& target);
    ~MidiKeyboardInput() override;

    void handle (const se::MidiEvent& event);

    /** A finger's key identity in the palette's stack: plain keys use the note
        number (0..127), MPE notes 1000 + their note id. */
    static constexpr int kMpeIdBase = 1000;

private:
    void noteAdded (juce::MPENote) override;
    void notePitchbendChanged (juce::MPENote) override;
    void noteReleased (juce::MPENote) override;

    KeyboardPalette& palette;
    juce::MPEInstrument mpe;
};
