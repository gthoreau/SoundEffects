#include "TransportButtons.h"

namespace se {

void TransportButtons::dropHold (TransportCues& c) noexcept
{
    if (held == Held::none) return;
    held = Held::none;
    ffLit = false;
    c.push (TransportCue::fastOff);
}

TransportCues TransportButtons::startTakeCue() noexcept
{
    TransportCues c;
    c.push (take ? TransportCue::takeResume : TransportCue::takeStart);
    take = true;
    return c;
}

TransportCues TransportButtons::pressRecord() noexcept
{
    TransportCues c;
    dropHold (c);
    switch (rec)
    {
        case RecordLight::off:
            if (playLit)
            {
                // Play already on: recording starts right away.
                rec = RecordLight::on;
                for (auto q : startTakeCue()) c.push (q);
            }
            else rec = RecordLight::armed;
            break;

        case RecordLight::armed:
        case RecordLight::on:
            // Disarm. A take (running, or held by Pause) ends; playback carries on.
            rec = RecordLight::off;
            if (take) { take = false; c.push (TransportCue::takeEnd); }
            break;
    }
    return c;
}

TransportCues TransportButtons::directRecord() noexcept
{
    TransportCues c;
    if (rec == RecordLight::on) return c;
    dropHold (c);
    rec = RecordLight::on;
    playLit = true; pauseLit = false; stopLit = false;
    for (auto q : startTakeCue()) c.push (q);
    return c;
}

TransportCues TransportButtons::pressStop() noexcept
{
    TransportCues c;
    held = Held::none;   // no fastOff: Stop resets the speed anyway
    stopLit = true;
    playLit = pauseLit = ffLit = false;
    rec = RecordLight::off;
    take = false;
    c.push (TransportCue::stop);
    return c;
}

TransportCues TransportButtons::pressPlay() noexcept
{
    TransportCues c;
    if (playLit && held == Held::none) return c;   // push-on: already on
    dropHold (c);
    if (playLit) return c;   // the hold let go with Play on: nothing more to do

    const bool wasPaused = pauseLit;
    playLit = true; pauseLit = false; stopLit = false;
    if (rec == RecordLight::armed)
    {
        rec = RecordLight::on;
        for (auto q : startTakeCue()) c.push (q);
    }
    else c.push (wasPaused ? TransportCue::resume : TransportCue::play);
    return c;
}

TransportCues TransportButtons::pressPause() noexcept
{
    TransportCues c;
    dropHold (c);
    // Nothing to hold when nothing rolls (Stop or the initial state); already paused stays paused.
    if (! playLit) return c;

    playLit = false; pauseLit = true;
    if (rec == RecordLight::on)
    {
        rec = RecordLight::armed;   // blinks: Play carries on with the take
        c.push (TransportCue::takePause);
    }
    else c.push (TransportCue::pause);
    return c;
}

TransportCues TransportButtons::space() noexcept
{
    return (playLit && held == Held::none) ? pressPause() : pressPlay();
}

TransportCues TransportButtons::ffDown() noexcept
{
    TransportCues c;
    if (! ffEnabled() || held != Held::none) return c;

    if (playLit)       held = Held::fromPlay;
    else if (pauseLit) { held = Held::fromPause; c.push (TransportCue::resume); }
    else               { held = stopLit ? Held::fromStop : Held::fromIdle; c.push (TransportCue::play); }

    ffLit = true; playLit = true; pauseLit = false; stopLit = false;
    c.push (TransportCue::fastOn);
    return c;
}

TransportCues TransportButtons::ffUp() noexcept
{
    TransportCues c;
    const Held h = held;
    if (h == Held::none) return c;
    held = Held::none;
    ffLit = false;

    switch (h)
    {
        case Held::fromPlay:
            c.push (TransportCue::fastOff);
            break;
        case Held::fromPause:
            playLit = false; pauseLit = true;
            c.push (TransportCue::fastOff);
            c.push (TransportCue::pause);
            break;
        case Held::fromStop:
        case Held::fromIdle:
            playLit = false;
            stopLit = h == Held::fromStop;
            c.push (TransportCue::stop);   // resets the speed and the playheads
            break;
        case Held::none:
            break;
    }
    return c;
}

void TransportButtons::toggleLoop() noexcept
{
    if (loopEnabled()) loopLit = ! loopLit;
}

void TransportButtons::ended() noexcept
{
    held = Held::none;
    stopLit = true;
    playLit = pauseLit = ffLit = false;
    rec = RecordLight::off;
    take = false;
}

void TransportButtons::takeRefused (bool transportRolling) noexcept
{
    rec = RecordLight::off;
    take = false;
    if (! transportRolling) playLit = false;
}

void TransportButtons::engineChanged (RollState was, RollState now) noexcept
{
    if (was == now) return;
    switch (now)
    {
        case RollState::stopped:
            // A take is the Recorder's business: its own end arrives through ended().
            if (take) return;
            if (playLit || pauseLit) ended();
            return;
        case RollState::playing:
            if (! playLit) { playLit = true; pauseLit = false; stopLit = false; }
            return;
        case RollState::paused:
            if (take || held != Held::none) return;
            if (playLit) { playLit = false; pauseLit = true; }
            return;
    }
}

} // namespace se
