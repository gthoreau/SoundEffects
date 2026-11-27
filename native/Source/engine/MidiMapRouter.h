// MidiMapRouter — where a mapped note goes (0.6.0 step 8.2).
//
// The processor owns one, so mapped notes work with the plug-in's window closed.
// The canvas's map (Engine::midiMap) and MidiMapPlayer say what a note asks of which
// Sound; with an editor open that goes down the same path as a press of the Sound's
// transport button (`perform`, so the lights follow and a multi-selection shares
// it). Without one it is applied to the engine here, as a freshly opened editor's
// buttons would: Play loops (Loop starts on), to the multi-selection the Keyboard
// was last aimed at when the Sound is in it, in one batch. Message thread.

#pragma once

#include "Engine.h"
#include "MidiMap.h"

#include <array>
#include <functional>
#include <vector>

namespace se {

class MidiMapRouter
{
public:
    explicit MidiMapRouter (Engine& e) : engine (e) {}

    /** The editor's, while it is open. */
    std::function<void (const MapCommand&)> perform;
    /** Without an editor: the Sounds a command on this one reaches. */
    std::function<std::vector<uint64_t> (uint64_t)> reach;
    /** Without an editor: a Sound to leave alone (a take is running in it). */
    std::function<bool (uint64_t)> busy;

    void noteOn (int note)
    {
        MapCommand c;
        const auto inSlot = [this] (int slot) { return engine.windowInSlot (slot); };
        if (MidiMap::takes (note)) ++struck[static_cast<size_t> (note - MidiMap::kLowNote)];
        if (player.noteOn (note, engine.midiMap(), inSlot, c)) send (c);
    }
    void noteOff (int note)
    {
        MapCommand c;
        if (player.noteOff (note, c)) send (c);
    }
    void allOff() { for (const auto& c : player.allOff()) send (c); }
    /** A note holding a Sound (not one on an empty pad: see strikes). */
    bool isHeld (int note) const noexcept { return player.isHeld (note); }
    /** How many notes pad `i` has had, assigned or not: the panel lights on a change. */
    uint32_t strikes (int i) const noexcept
    { return i >= 0 && i < MidiMap::kPads ? struck[static_cast<size_t> (i)] : 0; }

private:
    void send (const MapCommand& c)
    {
        if (engine.windowById (c.sound) == nullptr) return;   // closed under a held note
        if (perform) perform (c);
        else         apply (c);
    }

    void apply (const MapCommand& c)
    {
        const auto ids = reach ? reach (c.sound) : std::vector<uint64_t> { c.sound };
        const Engine::Batch together (engine);
        for (const uint64_t id : ids)
        {
            if (engine.windowById (id) == nullptr || (busy && busy (id))) continue;
            const auto now = engine.rollState (id);
            switch (c.action)
            {
                case MapAction::play:
                    if (now == TransportState::paused) engine.pause (id);   // resumes
                    else if (now == TransportState::stopped || c.fromStart)
                    {
                        PlayOptions o;
                        o.loop = o.useLoopRegion = o.fromCaret = true;
                        engine.play (id, o);
                    }
                    break;
                case MapAction::pause:
                    if (now == TransportState::playing) engine.pause (id);
                    break;
                case MapAction::stop:
                    if (now != TransportState::stopped) engine.stop (id);
                    break;
            }
        }
    }

    Engine& engine;
    MidiMapPlayer player;
    std::array<uint32_t, MidiMap::kPads> struck {};
};

} // namespace se
