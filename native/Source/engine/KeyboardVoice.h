// KeyboardVoice — the Keyboard palette's voice, without the palette (0.5.0 step 12).
//
// The palette used to hold this state itself. MIDI has to keep playing when the
// plug-in's window is closed, so the voice lives with the processor and the palette
// is only a view of it (and a mouse source for it). JUCE-free; message thread.
//
// How a key sounds is unchanged (see KeyboardPalette.h): a stopped Window is started
// looping at the pitch and stopped on release; a rolling Window is re-pitched in place
// and handed back at its own rate on release. The pitch is
//     2 ^ ((note - baseNote + bend) / 12)
// with `bend` in semitones, so an MPE slide is a continuous rate.
// Declicking (0.6.0) is the Transport's: a strike fades in, a release fades out, and
// a key struck again while the last one fades crosses over it. A new key while one
// is held only changes the speed, which cannot jump.

#pragma once

#include "Engine.h"
#include "MidiNotes.h"

#include <cstdint>

namespace se {

class KeyboardVoice
{
public:
    /** The keys the palette draws (C1..B8); KeyboardPalette.cpp asserts they match. */
    static constexpr int kLowNote = 24;
    static constexpr int kHighNote = 119;

    explicit KeyboardVoice (Engine& e) : engine (e) {}

    /** The Window the keys are aimed at (the canvas selection, pushed by the editor).
        0 for none. Until an editor has ever set it, the first Window stands in, so a
        plug-in played before its window was opened still sounds. */
    void setTarget (uint64_t windowId);
    uint64_t target() const;

    /** The note the sound's own pitch is measured from (the dot on the keyboard). */
    int baseNote() const;
    /** Is there a Window with audio in it to play? */
    bool hasTarget() const;

    // --- the voice (mouse and MIDI share it) ---
    void strike (int note, double bendSemitones = 0.0);
    void release();
    /** End what this voice is driving and touch nothing else. */
    void allNotesOff();

    // --- MIDI keys: mono, last-note priority ---
    void midiNoteOn (int id, int note, double bendSemitones);
    void midiNoteOff (int id);
    void midiBend (int id, double bendSemitones);
    void midiPlainBend (double semitones);
    void midiAllOff();

    // --- what the palette draws ---
    int  heldNote() const noexcept { return held; }       // the key pressed, -1 none
    int  lastNote() const noexcept { return last; }       // the last key played, -1 none
    double slide() const noexcept { return sounding; }    // its bend, semitones
    void forgetLastNote() noexcept { last = -1; }

private:
    const SoundDocument* doc() const;
    double soundingBend() const;
    void retune();

    Engine& engine;
    uint64_t targetId = 0;
    bool targetSet = false;

    uint64_t voiceId = 0;        // the Window being driven, 0 when idle
    bool startedVoice = false;   // started (stop on release) or borrowed (hand back)
    int held = -1;
    int last = -1;
    double sounding = 0.0;

    NoteStack midiHeld;
    double plainBend = 0.0;
};

} // namespace se
