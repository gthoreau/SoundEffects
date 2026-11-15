#include "KeyboardVoice.h"

#include <cmath>

namespace se {

void KeyboardVoice::setTarget (uint64_t windowId)
{
    targetSet = true;
    if (targetId == windowId) return;

    // A note held does not carry over to a new target — it was played on the old one.
    allNotesOff();
    midiHeld.clear();
    targetId = windowId;
}

uint64_t KeyboardVoice::target() const
{
    if (targetSet) return targetId;
    // No editor has aimed the keys yet: the first Window stands in.
    const auto& ids = engine.windowIds();
    return ids.empty() ? 0 : ids.front();
}

const SoundDocument* KeyboardVoice::doc() const
{
    const uint64_t id = target();
    if (id == 0) return nullptr;
    auto* w = engine.windowById (id);
    return w != nullptr ? w->uiDoc.get() : nullptr;
}

int KeyboardVoice::baseNote() const
{
    const auto* d = doc();
    return d != nullptr ? d->baseFreq : 60;
}

bool KeyboardVoice::hasTarget() const
{
    const auto* d = doc();
    return d != nullptr && d->maxTrackSize() > 0;
}

bool KeyboardVoice::isRolling() const
{
    auto* w = engine.windowById (target());
    return w != nullptr && w->transport != nullptr
        && w->transport->uiState() == TransportState::playing;
}

void KeyboardVoice::strike (int note, double bendSemitones)
{
    if (note < kLowNote || note > kHighNote) return;
    if (! hasTarget()) return;

    const uint64_t id = target();
    held = note;
    last = note;
    sounding = bendSemitones;

    const double rateScale = std::pow (2.0, (note - baseNote() + bendSemitones) / 12.0);

    // ORDER MATTERS: our own note is tested first (once we have started one the
    // transport is rolling too, and "is it rolling?" would call it borrowed).
    if (voiceId != 0 && voiceId == id)
    {
        engine.setRateScale (id, rateScale);              // a glissando, not a stutter
    }
    else if (isRolling())
    {
        // BORROW a Window the user set playing: re-pitch where it stands.
        engine.setRateScale (id, rateScale);
        voiceId = id;
        startedVoice = false;
    }
    else
    {
        PlayOptions opts;
        opts.rateScale = rateScale;
        opts.loop = true;           // a held key sustains; every selected track plays
        engine.play (id, opts);
        voiceId = id;
        startedVoice = true;
    }
}

void KeyboardVoice::release()
{
    if (held < 0) return;
    held = -1;
    allNotesOff();
}

void KeyboardVoice::allNotesOff()
{
    // Only what this voice STARTED is stopped; a rolling Window is handed back at its
    // own pitch, still rolling. Nothing else on the canvas is touched.
    if (voiceId != 0)
    {
        if (startedVoice) engine.stop (voiceId);
        else              engine.setRateScale (voiceId, 1.0);
        voiceId = 0;
        startedVoice = false;
    }
    held = -1;
}

double KeyboardVoice::soundingBend() const
{
    const int id = midiHeld.topId();
    return midiHeld.topBend() + (id >= 0 && id < 1000 ? plainBend : 0.0);
}

void KeyboardVoice::retune()
{
    if (voiceId == 0 || voiceId != target() || midiHeld.empty()) return;
    sounding = soundingBend();
    engine.setRateScale (voiceId, std::pow (2.0, (midiHeld.top() - baseNote() + sounding) / 12.0));
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
