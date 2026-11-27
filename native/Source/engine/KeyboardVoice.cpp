#include "KeyboardVoice.h"

#include <algorithm>
#include <cmath>

namespace se {

static double pitch (int note, const SoundDocument& d, double bendSemitones)
{
    return std::pow (2.0, (note - d.baseFreq + bendSemitones) / 12.0);
}

void KeyboardVoice::setTargets (const std::vector<uint64_t>& windowIds)
{
    targetSet = true;
    targets = windowIds;
    if (voices.empty() && midiHeld.empty()) return;

    // A Window no longer aimed at is let go: its note does not carry over. The ones
    // that stay keep sounding, and one that joins waits for the next key.
    const Engine::Batch together (engine);
    for (size_t i = voices.size(); i-- > 0;)
        if (std::find (targets.begin(), targets.end(), voices[i].id) == targets.end())
        {
            letGo (voices[i]);
            voices.erase (voices.begin() + static_cast<std::ptrdiff_t> (i));
        }
    if (voices.empty())
    {
        held = -1;
        midiHeld.clear();
    }
}

uint64_t KeyboardVoice::target() const
{
    const auto all = reach();
    return all.empty() ? 0 : all.front();
}

std::vector<uint64_t> KeyboardVoice::reach() const
{
    if (targetSet) return targets;
    // No editor has aimed the keys yet: the first Window stands in.
    const auto& ids = engine.windowIds();
    return ids.empty() ? std::vector<uint64_t> {} : std::vector<uint64_t> { ids.front() };
}

const SoundDocument* KeyboardVoice::doc (uint64_t id) const
{
    auto* w = id != 0 ? engine.windowById (id) : nullptr;
    return w != nullptr ? w->uiDoc.get() : nullptr;
}

int KeyboardVoice::baseNote() const
{
    const auto* d = doc (target());
    return d != nullptr ? d->baseFreq : 60;
}

bool KeyboardVoice::hasTarget() const
{
    const auto* d = doc (target());
    return d != nullptr && d->maxTrackSize() > 0;
}

void KeyboardVoice::strike (int note, double bendSemitones)
{
    if (note < kLowNote || note > kHighNote) return;

    const Engine::Batch together (engine);   // every member in the same block
    bool struck = false;
    for (const uint64_t id : reach())
    {
        const auto* d = doc (id);
        if (d == nullptr || d->maxTrackSize() == 0) continue;
        struck = true;

        const double rateScale = pitch (note, *d, bendSemitones);
        const bool ours = std::any_of (voices.begin(), voices.end(),
                                       [id] (const Voice& v) { return v.id == id; });

        // ORDER MATTERS: our own note is tested first (once we have started one the
        // transport is rolling too, and "is it rolling?" would call it borrowed).
        if (ours)
        {
            engine.setRateScale (id, rateScale);              // a glissando, not a stutter
        }
        else if (engine.isRolling (id))
        {
            // BORROW a Window the user set playing: re-pitch where it stands.
            engine.setRateScale (id, rateScale);
            voices.push_back ({ id, false });
        }
        else
        {
            PlayOptions opts;
            opts.rateScale = rateScale;
            opts.loop = true;           // a held key sustains; every selected track plays
            engine.play (id, opts);
            voices.push_back ({ id, true });
        }
    }
    if (! struck) return;
    held = note;
    last = note;
    sounding = bendSemitones;
}

void KeyboardVoice::release()
{
    if (held < 0) return;
    held = -1;
    allNotesOff();
}

void KeyboardVoice::letGo (const Voice& v)
{
    // Only what this voice STARTED is stopped; a rolling Window is handed back at its
    // own pitch, still rolling. Nothing else on the canvas is touched.
    if (v.started) engine.stop (v.id);
    else           engine.setRateScale (v.id, 1.0);
}

void KeyboardVoice::allNotesOff()
{
    const Engine::Batch together (engine);
    for (const auto& v : voices) letGo (v);
    voices.clear();
    held = -1;
}

double KeyboardVoice::soundingBend() const
{
    const int id = midiHeld.topId();
    return midiHeld.topBend() + (id >= 0 && id < 1000 ? plainBend : 0.0);
}

void KeyboardVoice::retune()
{
    if (voices.empty() || midiHeld.empty()) return;
    sounding = soundingBend();
    const auto aimed = reach();
    const Engine::Batch together (engine);
    for (const auto& v : voices)
    {
        const auto* d = doc (v.id);
        if (d != nullptr && std::find (aimed.begin(), aimed.end(), v.id) != aimed.end())
            engine.setRateScale (v.id, pitch (midiHeld.top(), *d, sounding));
    }
}

void KeyboardVoice::midiNoteOn (int id, int note, double bendSemitones)
{
    if (note < kLowNote || note > kHighNote) return;
    midiHeld.press (id, note, bendSemitones);
    strike (note, soundingBend());
}

void KeyboardVoice::midiNoteOff (int id)
{
    const bool wasTop = midiHeld.topId() == id;
    const int now = midiHeld.lift (id);
    if (! wasTop) return;                              // under the sounding key: nothing changes
    if (now >= 0) strike (now, soundingBend());        // the key before it takes over
    else          release();
}

void KeyboardVoice::midiBend (int id, double bendSemitones)
{
    midiHeld.setBend (id, bendSemitones);
    if (midiHeld.topId() == id) retune();
}

void KeyboardVoice::midiPlainBend (double semitones)
{
    plainBend = semitones;
    const int id = midiHeld.topId();
    if (id >= 0 && id < 1000) retune();
}

void KeyboardVoice::midiAllOff()
{
    midiHeld.clear();
    release();
}

} // namespace se
