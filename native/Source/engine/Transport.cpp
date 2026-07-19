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
    const bool hasSelection = doc.selSt != doc.selEnd;

    ctx = {};
    if (opts.loop && opts.useLoopRegion && doc.loopEnd > doc.loopSt)
    {
        // option-Play: loop the Set Loop region across all tracks.
        ctx.mode = LoopMode::selection;
        ctx.selSt = doc.loopSt; ctx.selEnd = doc.loopEnd;
        ctx.firstSelTrack = 1; ctx.lastSelTrack = doc.numTracks();
        pos = static_cast<double> (ctx.selSt);
    }
    else if (opts.loop && hasSelection)
    {
        // Loop the selection, in the selected tracks (they stay in phase).
        ctx.mode = LoopMode::selection;
        ctx.selSt = doc.selSt; ctx.selEnd = doc.selEnd;
        ctx.firstSelTrack = doc.firstSelTrack; ctx.lastSelTrack = doc.lastSelTrack;
        pos = static_cast<double> (ctx.selSt);
    }
    else if (opts.loop)
    {
        // No selection: every track loops over ITS OWN length, independently.
        ctx.mode = LoopMode::all;
        pos = 0.0;
    }
    else
    {
        // Play once. A selection plays just the selection; else from caret/start.
        ctx.mode = LoopMode::once;
        if (hasSelection) { pos = static_cast<double> (doc.selSt); playEnd = doc.selEnd; }
        else
        {
            pos = (playFrom == PlayFrom::insertionPoint) ? static_cast<double> (doc.selSt) : 0.0;
            playEnd = doc.maxTrackSize();
        }
    }

    if (opts.soloTrack >= 0) { monitor.soloed.clear(); monitor.soloed.insert (opts.soloTrack); }

    uiLoopModeCode.store (static_cast<uint8_t> (ctx.mode), std::memory_order_relaxed);
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

    cachedFrameSize = doc.frameSize();
    const double s         = step (doc);
    const bool   playOnce  = ctx.mode == LoopMode::once;
    const bool   forceMono = ! supportsStereo;

    MixOptions mix;
    mix.volume    = masterVolume;
    mix.monitor   = &monitor;
    mix.forceMono = forceMono;

    int written = 0;
    for (int i = 0; i < frames; ++i)
    {
        // pos is MONOTONIC — mixFrame wraps each track at its own length. Only
        // play-once has a stop point; the loop modes run forever.
        if (playOnce && pos >= static_cast<double> (playEnd)) { setState (TransportState::stopped); break; }

        const size_t p = static_cast<size_t> (std::floor (pos));
        const StereoFrame f = mixFrame (doc, p, ctx, mix);
        left[i]  = f.left;
        right[i] = f.right;

        pos += s;
        ++written;
    }

    uiPos.store (framePos(), std::memory_order_relaxed);
    return written;
}

} // namespace se
