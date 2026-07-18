#include "Transport.h"

#include <algorithm>
#include <cmath>

namespace se {

Transport::Transport (double rate) : deviceRate (rate) {}

size_t Transport::framePos() const noexcept
{
    const auto p = static_cast<size_t> (std::floor (pos));
    return p - (p % static_cast<size_t> (std::max (1, cachedFrameSize)));
}

double Transport::step (const SoundDocument& doc) const noexcept
{
    const int frameSize = doc.frameSize();
    const double declared = doc.rateHz * playOpts.rateScale * advanceScale;   // DECLARED rate
    return (declared / deviceRate) * frameSize;
}

void Transport::play (const SoundDocument& doc, const PlayOptions& opts)
{
    playOpts = opts;
    advanceScale = 1.0;
    cachedFrameSize = doc.frameSize();

    if (opts.loop && opts.useLoopRegion) { loopStart = doc.loopSt; loopEnd = doc.loopEnd; }
    else if (opts.loop)                  { loopStart = doc.selSt;  loopEnd = doc.selEnd;  }
    else                                 { loopStart = 0;          loopEnd = 0;           }

    if (opts.loop && loopEnd <= loopStart) { loopStart = 0; loopEnd = doc.maxTrackSize(); }

    if (opts.loop)
    {
        pos = static_cast<double> (loopStart);
        playEnd = loopEnd;
    }
    else
    {
        const bool hasSelection = doc.selSt != doc.selEnd;
        if (hasSelection) { pos = static_cast<double> (doc.selSt); playEnd = doc.selEnd; }
        else
        {
            pos = (playFrom == PlayFrom::insertionPoint) ? static_cast<double> (doc.selSt) : 0.0;
            playEnd = doc.maxTrackSize();
        }
    }

    if (opts.soloTrack >= 0) { monitor.soloed.clear(); monitor.soloed.insert (opts.soloTrack); }

    setState (TransportState::playing);
    uiPos.store (static_cast<size_t> (pos), std::memory_order_relaxed);
}

void Transport::stop() noexcept
{
    setState (TransportState::stopped);
    pos = 0.0;
    advanceScale = 1.0;
    uiPos.store (0, std::memory_order_relaxed);
}

void Transport::pause() noexcept
{
    if (currentState == TransportState::playing)      setState (TransportState::paused);
    else if (currentState == TransportState::paused)  setState (TransportState::playing);
}

void Transport::advance (const SoundDocument& doc)
{
    if (currentState != TransportState::playing) play (doc, playOpts);
    advanceScale = 2.0;
}

void Transport::seek (const SoundDocument& doc, size_t bytes) noexcept
{
    const int frameSize = doc.frameSize();
    cachedFrameSize = frameSize;
    const size_t p = std::min (bytes, doc.maxTrackSize());
    pos = static_cast<double> (p - (p % static_cast<size_t> (frameSize)));
}

int Transport::render (const SoundDocument& doc, float* left, float* right, int frames) noexcept
{
    if (left == nullptr || right == nullptr)      return 0;
    if (currentState != TransportState::playing)  return 0;

    const int    frameSize = doc.frameSize();
    cachedFrameSize = frameSize;
    const double s         = step (doc);
    const bool   looping   = playOpts.loop;
    const bool   forceMono = ! supportsStereo;

    MixOptions mix;
    mix.volume    = masterVolume;
    mix.monitor   = &monitor;
    mix.forceMono = forceMono;

    int written = 0;
    for (int i = 0; i < frames; ++i)
    {
        if (pos >= static_cast<double> (playEnd))
        {
            if (! looping) { setState (TransportState::stopped); break; }
            const double span = static_cast<double> (playEnd) - static_cast<double> (loopStart);
            if (span <= 0.0) { setState (TransportState::stopped); break; }
            pos = static_cast<double> (loopStart) + std::fmod (pos - static_cast<double> (loopStart), span);
        }

        size_t read = static_cast<size_t> (std::floor (pos));
        read -= read % static_cast<size_t> (frameSize);

        const StereoFrame f = mixFrame (doc, read, mix);
        left[i]  = f.left;
        right[i] = f.right;

        pos += s;
        ++written;
    }

    uiPos.store (framePos(), std::memory_order_relaxed);
    return written;
}

} // namespace se
