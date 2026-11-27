// MidiMap — Sounds played from MIDI notes (0.6.0 step 8).
//
// Twelve pads, one for each note C0..B0 (MIDI 12..23; the Keyboard's lowest key is
// C1 = 24). A pad names a mixer slot (so reordering the Mixer changes the Sound) and
// says what happens when its note starts and when it ends. Starting on Stop or Pause
// forces the end to Play; starting on Play ends on Stop or Pause.
//
// MidiMapPlayer turns notes into actions on Sounds. Per Sound, the most recent note
// wins: a note-end acts only if its note is still the latest one holding the Sound
// (the Keyboard's held-note rule), so notes overlapping on one Sound do not cut each
// other off. What a note does when it ends is what its pad said when it started, on
// the Sound it started, whatever has been edited or reordered since.
//
// Pure models, no JUCE and no Engine. Message thread.

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace se {

enum class MapAction : uint8_t { play = 0, stop = 1, pause = 2 };

struct MapPad
{
    static constexpr int kNoSlot = -1;
    int       slot  = kNoSlot;           // mixer slot, 0-based
    MapAction start = MapAction::play;
    MapAction end   = MapAction::stop;

    bool operator== (const MapPad& o) const noexcept { return slot == o.slot && start == o.start && end == o.end; }
    bool operator!= (const MapPad& o) const noexcept { return ! (*this == o); }
};

class MidiMap
{
public:
    static constexpr int kPads = 12;
    static constexpr int kLowNote = 12;                    // C0
    static constexpr int kHighNote = kLowNote + kPads - 1; // B0

    /** Is this note the map's (and so never the Keyboard's)? */
    static bool takes (int note) noexcept { return note >= kLowNote && note <= kHighNote; }

    /** May a pad that starts on `start` end on `end`? */
    static bool endAllowed (MapAction start, MapAction end) noexcept
    {
        return start == MapAction::play ? end != MapAction::play : end == MapAction::play;
    }

    const MapPad& pad (int i) const noexcept { return pads[index (i)]; }
    /** Set a pad, made legal: a negative slot is none, and an end its start does not
        allow becomes Play (after Stop or Pause) or Stop (after Play). */
    void setPad (int i, MapPad p) noexcept
    {
        if (p.slot < 0) p.slot = MapPad::kNoSlot;
        if (! endAllowed (p.start, p.end))
            p.end = p.start == MapAction::play ? MapAction::stop : MapAction::play;
        pads[index (i)] = p;
    }
    void setSlot (int i, int slot) noexcept       { auto p = pad (i); p.slot = slot;   setPad (i, p); }
    void setStart (int i, MapAction a) noexcept   { auto p = pad (i); p.start = a;     setPad (i, p); }
    void setEnd (int i, MapAction a) noexcept     { auto p = pad (i); if (endAllowed (p.start, a)) { p.end = a; setPad (i, p); } }
    void clear() noexcept { pads = {}; }
    bool empty() const noexcept
    {
        return std::all_of (pads.begin(), pads.end(), [] (const MapPad& p) { return p == MapPad {}; });
    }

private:
    static size_t index (int i) noexcept { return static_cast<size_t> (std::clamp (i, 0, kPads - 1)); }
    std::array<MapPad, kPads> pads {};
};

/** What a note asks of a Sound. `fromStart`: a note starting (its Play restarts a
    Sound already playing); otherwise a note ending (its Play only resumes). */
struct MapCommand
{
    uint64_t  sound = 0;
    MapAction action = MapAction::play;
    bool      fromStart = false;

    bool operator== (const MapCommand& o) const noexcept
    { return sound == o.sound && action == o.action && fromStart == o.fromStart; }
};

class MidiMapPlayer
{
public:
    /** A note starts. `soundInSlot` (slot -> Sound id, 0 for none) answers for the
        Mixer as it is now. False: nothing to do (not a pad, or its slot is empty). */
    template <typename SoundInSlot>
    bool noteOn (int note, const MidiMap& map, SoundInSlot&& soundInSlot, MapCommand& out)
    {
        if (! MidiMap::takes (note)) return false;
        drop (note);
        const MapPad& p = map.pad (note - MidiMap::kLowNote);
        const uint64_t sound = p.slot == MapPad::kNoSlot ? 0 : static_cast<uint64_t> (soundInSlot (p.slot));
        if (sound == 0) return false;
        held.push_back ({ note, sound, p.end });
        out = { sound, p.start, true };
        return true;
    }

    /** A note ends. False: nothing to do (unknown, or a later note holds its Sound). */
    bool noteOff (int note, MapCommand& out)
    {
        const auto it = std::find_if (held.begin(), held.end(), [note] (const Held& h) { return h.note == note; });
        if (it == held.end()) return false;
        const Held ended = *it;
        const bool latest = std::none_of (it + 1, held.end(),
                                          [&ended] (const Held& h) { return h.sound == ended.sound; });
        held.erase (it);
        if (! latest) return false;
        out = { ended.sound, ended.end, false };
        return true;
    }

    /** Every note up at once (All Notes Off): each held Sound ends as its latest
        note would. */
    std::vector<MapCommand> allOff()
    {
        std::vector<MapCommand> out;
        while (! held.empty())
        {
            const Held latest = held.back();
            out.push_back ({ latest.sound, latest.end, false });
            held.erase (std::remove_if (held.begin(), held.end(),
                                        [&latest] (const Held& h) { return h.sound == latest.sound; }),
                        held.end());
        }
        return out;
    }

    bool isHeld (int note) const noexcept
    { return std::any_of (held.begin(), held.end(), [note] (const Held& h) { return h.note == note; }); }

    /** Forget every held note, acting on none (the canvas was replaced). */
    void reset() noexcept { held.clear(); }

private:
    struct Held { int note; uint64_t sound; MapAction end; };
    void drop (int note)
    {
        held.erase (std::remove_if (held.begin(), held.end(), [note] (const Held& h) { return h.note == note; }),
                    held.end());
    }
    std::vector<Held> held;   // oldest first; at most one per pad
};

} // namespace se
