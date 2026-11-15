// MidiNotes — the JUCE-free half of the MIDI input (0.5.0 step 12).
//
// The audio thread turns what a MIDI keyboard (or the DAW) sends into `MidiEvent`s,
// pushes them through a `MidiQueue`, and the message thread feeds them to the
// keyboard palette. The palette is MONO — one Window, one rate at a time — so
// several held keys need a rule for which one sounds: the LAST ONE PLAYED, and
// when it comes up, the one before it (`NoteStack`). No JUCE here, so every rule is
// testable.

#pragma once

#include "SpscQueue.h"

#include <cmath>
#include <cstdint>

namespace se {

struct MidiEvent
{
    enum Type : uint8_t
    {
        noteOn,     // note
        noteOff,    // note
        bend,       // value = semitones, relative to the key's own pitch (see MidiBend)
        allOff,     // every key up (All Notes Off, All Sound Off, a host stop)
        raw         // any other message, three bytes, for the MPE tracker (status, note = data1, d2)
    };

    Type    type    = noteOn;
    uint8_t channel = 0;     // 0..15
    uint8_t note    = 0;     // 0..127
    float   value   = 0.0f;  // bend: the 14-bit wheel, 0..16383 (8192 = centre)
    uint8_t status  = 0;     // raw: the status byte
    uint8_t d2      = 0;     // raw: the second data byte
};

/** Audio thread pushes, message thread pops. 256 events is far more than a hand
    can play between two message-thread turns; a full queue drops the newest. */
using MidiQueue = SpscQueue<MidiEvent, 256>;

/**
 * The keys currently held, oldest first. The newest sounds; letting it go brings
 * the one before it back — the usual mono "last note priority".
 * A key struck twice (no note-off between) counts once, as the newest.
 */
class NoteStack
{
public:
    static constexpr int kMax = 16;

    /** Press `note` (0..127) under the identity `id` (plain keys: the note number;
        an MPE note: its own id). Returns the note that now sounds. */
    int press (int id, int note, double bend) noexcept
    {
        if (note < 0 || note > 127) return top();
        remove (id);
        if (count == kMax)   // more than sixteen fingers: the oldest gives way
        {
            for (int i = 1; i < kMax; ++i) held[i - 1] = held[i];
            --count;
        }
        held[count++] = { id, note, bend };
        return top();
    }
    int press (int note) noexcept { return press (note, note, 0.0); }

    /** Let go of the key `id`. Returns the note that sounds now, or -1. */
    int lift (int id) noexcept
    {
        remove (id);
        return top();
    }

    /** Move the bend of the key `id` (semitones, MPE per-note). */
    void setBend (int id, double bend) noexcept
    {
        for (int i = 0; i < count; ++i)
            if (held[i].id == id) held[i].bend = bend;
    }

    /** The note that sounds, or -1; and the identity / bend of that key. */
    int top() const noexcept { return count > 0 ? held[count - 1].note : -1; }
    int topId() const noexcept { return count > 0 ? held[count - 1].id : -1; }
    double topBend() const noexcept { return count > 0 ? held[count - 1].bend : 0.0; }
    bool empty() const noexcept { return count == 0; }
    int size() const noexcept { return count; }
    void clear() noexcept { count = 0; }

private:
    struct Key { int id = 0; int note = 0; double bend = 0.0; };

    void remove (int id) noexcept
    {
        int w = 0;
        for (int i = 0; i < count; ++i)
            if (held[i].id != id) held[w++] = held[i];
        count = w;
    }

    Key held[kMax] {};
    int count = 0;
};

namespace midibend {

/** A 14-bit pitch-bend value (0..16383, 8192 = centre) as -1..+1. The two ends are
    not symmetric on the wire (-8192..+8191); the top is stretched to +1. */
inline double normalised (int value14) noexcept
{
    const int v = value14 < 0 ? 0 : (value14 > 16383 ? 16383 : value14) ;
    const double c = (double) (v - 8192);
    return c < 0.0 ? c / 8192.0 : c / 8191.0;
}

/** Semitones for a bend wheel of `rangeSemitones` either way (plain MIDI: 2). */
inline double semitones (int value14, double rangeSemitones) noexcept
{
    return normalised (value14) * rangeSemitones;
}

} // namespace midibend

} // namespace se
