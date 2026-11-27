// KeyboardVoice — the Keyboard palette's voice, without the palette (0.5.0 step 12).
//
// The palette used to hold this state itself. MIDI has to keep playing when the
// plug-in's window is closed, so the voice lives with the processor and the palette
// is only a view of it (and a mouse source for it). JUCE-free; message thread.
//
// In a multi-selection (0.6.0 step 7.4) a key strikes every member at that pitch, in
// one engine batch, each measured from its own base note; letting go ends them
// together. A Sound that joins while a key is down waits for the next one; one that
// leaves is let go at once.
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
#include <vector>

namespace se {

class KeyboardVoice
{
public:
    /** The keys the palette draws (C1..B8); KeyboardPalette.cpp asserts they match. */
    static constexpr int kLowNote = 24;
    static constexpr int kHighNote = 119;

    explicit KeyboardVoice (Engine& e) : engine (e) {}

    /** The Windows the keys are aimed at (the canvas selection, pushed by the
        editor): the primary first, then the rest of its multi-selection. Empty for
        none. Until an editor has ever set it, the first Window stands in, so a
        plug-in played before its window was opened still sounds. */
    void setTargets (const std::vector<uint64_t>& windowIds);
    /** One Window alone, or none (0). */
    void setTarget (uint64_t windowId) { setTargets (windowId != 0 ? std::vector<uint64_t> { windowId }
                                                                   : std::vector<uint64_t> {}); }
    /** The primary: what the palette draws and Apply writes to. */
    uint64_t target() const;

    /** The note the primary's own pitch is measured from (the dot on the keyboard). */
    int baseNote() const;
    /** Is the primary a Window with audio in it? */
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
    /** The document of Window `id`, or null. */
    const SoundDocument* doc (uint64_t id) const;
    /** Every Window a key reaches, the primary first. */
    std::vector<uint64_t> reach() const;
    double soundingBend() const;
    void retune();

    Engine& engine;
    std::vector<uint64_t> targets;   // the primary first
    bool targetSet = false;

    /** A Window being driven: started (stopped on release) or borrowed (handed back). */
    struct Voice { uint64_t id; bool started; };
    std::vector<Voice> voices;       // empty when idle
    /** End what is being driven on one Window. */
    void letGo (const Voice&);
    int held = -1;
    int last = -1;
    double sounding = 0.0;

    NoteStack midiHeld;
    double plainBend = 0.0;
};

} // namespace se
