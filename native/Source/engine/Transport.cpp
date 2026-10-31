#include "Transport.h"

#include <algorithm>
#include <cmath>

namespace se {

Transport::Transport (double rate) : deviceRate (rate)
{
    for (auto& p : uiTrackPos) p.store (kNoPosition, std::memory_order_relaxed);
}

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

Transport::Aim Transport::loopAim (const SoundDocument& doc, const PlayOptions& opts) noexcept
{
    Aim a;
    if (opts.useLoopRegion && doc.loopEnd > doc.loopSt)
    {
        // The Set Loop region (0.4.0: the Loop button, whenever a region is set), across all tracks.
        a.mode = LoopMode::selection;
        a.st = doc.loopSt; a.end = doc.loopEnd;
        a.first = 1; a.last = doc.numTracks();
    }
    else if (doc.selSt != doc.selEnd)
    {
        // The selection, in the selected tracks (they stay in phase).
        a.mode = LoopMode::selection;
        a.st = doc.selSt; a.end = doc.selEnd;
        a.first = doc.firstSelTrack; a.last = doc.lastSelTrack;
    }
    // else: no selection — every track loops over ITS OWN length, independently.
    return a;
}

void Transport::publishRegion (size_t st, size_t end, int first, int last) noexcept
{
    uiRegionSt.store (st, std::memory_order_relaxed);
    uiRegionEnd.store (end, std::memory_order_relaxed);
    uiRegionFirst.store (first, std::memory_order_relaxed);
    uiRegionLast.store (last, std::memory_order_relaxed);
}

void Transport::publishTrackPositions (const SoundDocument& doc) noexcept
{
    const size_t fs = static_cast<size_t> (std::max (1, doc.frameSize()));
    const int n = std::min (doc.numTracks(), kPhaseTracks);
    for (int t = 0; t < kPhaseTracks; ++t)
    {
        size_t v = kNoPosition;
        if (t < n)
        {
            const TrackRead r = trackRead (doc, t, pos, ctx);
            if (r.present) v = r.frame * fs;
        }
        uiTrackPos[static_cast<size_t> (t)].store (v, std::memory_order_relaxed);
    }
}

void Transport::startCrossfade (const PlayContext& from) noexcept
{
    prev = from;
    xfadeTotal = std::max (1, static_cast<int> (std::lround (deviceRate * 0.002)));   // ~2 ms
    xfadeLeft = xfadeTotal;
}

void Transport::anchor (const SoundDocument& doc, PlayContext next) noexcept
{
    const size_t fs = static_cast<size_t> (std::max (1, doc.frameSize()));
    const int n = std::min (doc.numTracks(), kPhaseTracks);
    const PlayContext old = ctx;
    const double fresh = 0.25 * static_cast<double> (fs) - pos;   // u = 0: the span's start

    // The reference (12.8): the topmost track the OLD mapping had reading a frame of
    // its own. A track that cannot keep its frame follows it, so deselecting does not
    // send the tracks outside the old selection back to 0. See
    // docs/0.4.0/12.8-deselect-sync.md for the cases.
    bool haveRef = false, refUp = true;
    size_t refFrame = 0;
    for (int t = 0; t < n && ! haveRef; ++t)
    {
        const size_t lenFrames = doc.tracks[static_cast<size_t> (t)].data.size() / fs;
        const TrackRead was = readFor (old, t, lenFrames, fs, pos);
        // readFor does not know the track's length: present = inside its own data.
        if (was.inSpan && was.frame < lenFrames) { haveRef = true; refFrame = was.frame; refUp = was.up; }
    }

    bool up0 = true; size_t span0 = 0;
    for (int t = 0; t < n; ++t)
    {
        const size_t lenFrames = doc.tracks[static_cast<size_t> (t)].data.size() / fs;
        const TrackRead was = readFor (old, t, lenFrames, fs, pos);

        size_t lo = 0, S = 0; bool wrap = true;
        const bool plays = readSpan (next, t, lenFrames, fs, lo, S, wrap);
        auto& ph = next.phase[static_cast<size_t> (t)];

        bool up = true;
        if (plays && was.inSpan && was.frame >= lo && was.frame < lo + S)
        {
            ph = phaseToRead (next.dir, S, was.frame - lo, was.up, pos, fs);   // keep its own frame
            up = was.up;
        }
        else if (plays && haveRef && refFrame >= lo && refFrame < lo + S)
        {
            ph = phaseToRead (next.dir, S, refFrame - lo, refUp, pos, fs);       // the reference's frame
            up = refUp;
        }
        else if (plays && haveRef && next.mode == LoopMode::all)
        {
            ph = phaseToRead (next.dir, S, refFrame % S, refUp, pos, fs);       // ...wrapped to its own length
            up = refUp;
        }
        else
            ph = fresh;

        if (t == 0) { up0 = up; span0 = S; }
    }
    // A track the document gained since carries on from the last one's phase.
    for (int t = std::max (n, 1); t < kPhaseTracks; ++t) next.phase[static_cast<size_t> (t)] = next.phase[static_cast<size_t> (std::max (n, 1) - 1)];

    if (next.mode == LoopMode::once && n > 0)
    {
        // Play out what is left of the pass, then stop: to the end of the span going
        // up or down; a ping-pong finishes its round trip (a bounce off the far end,
        // then back, or off the near end and across).
        const int64_t s = static_cast<int64_t> (span0);
        const int64_t stopU = next.dir == PlayDirection::pingPong ? (up0 ? 2 * s : 3 * s) : s;
        stopPos = static_cast<double> (stopU) * static_cast<double> (fs) - next.phase[0];
    }

    startCrossfade (old);
    ctx = next;
    publishTrackPositions (doc);
}

void Transport::setDirection (PlayDirection d, const SoundDocument& doc) noexcept
{
    if (d == dir) return;
    dir = d;
    uiDirCode.store (static_cast<uint8_t> (d), std::memory_order_relaxed);

    if (currentState == TransportState::stopped) return;
    cachedFrameSize = doc.frameSize();
    PlayContext next = ctx;
    next.dir = d;
    anchor (doc, next);
}

void Transport::play (const SoundDocument& doc, const PlayOptions& opts)
{
    playOpts = opts;
    advanceScale = 1.0;
    cachedFrameSize = doc.frameSize();
    xfadeLeft = 0;
    const size_t fs = static_cast<size_t> (std::max (1, doc.frameSize()));
    const bool hasSelection = doc.selSt != doc.selEnd;

    // The region published to the UI: the same span the ear gets, whichever branch
    // below sets it. Track scope is every track unless a selection narrows it.
    size_t regionSt = 0, regionEnd = 0;
    int regionFirst = 1, regionLast = doc.numTracks();

    ctx = {};
    ctx.dir = dir;
    double phase = 0.0;

    if (opts.loop)
    {
        const Aim a = loopAim (doc, opts);
        ctx.mode = a.mode;
        if (a.mode == LoopMode::selection)
        {
            ctx.selSt = a.st; ctx.selEnd = a.end;
            ctx.firstSelTrack = a.first; ctx.lastSelTrack = a.last;
            regionSt = a.st; regionEnd = a.end;
            regionFirst = a.first; regionLast = a.last;
            // Forward from the span's start: (pos - selSt) is the distance travelled.
            pos = static_cast<double> (a.st);
            phase = -pos;
        }
        else
        {
            pos = 0.0;
            regionEnd = doc.maxTrackSize();
        }
    }
    else
    {
        // Play once, through one span: the selection, else the whole sound.
        ctx.mode = LoopMode::once;
        size_t a = 0, b = doc.maxTrackSize();
        if (hasSelection) { a = doc.selSt; b = doc.selEnd; }
        if (b < a) b = a;
        b = a + (b - a) - (b - a) % fs;
        ctx.selSt = a; ctx.selEnd = b;
        pos = static_cast<double> (a);
        phase = -pos;
        const double span = static_cast<double> (b - a);
        stopPos = pos + (dir == PlayDirection::pingPong ? 2.0 * span : span);
        regionSt = a; regionEnd = b;
    }
    ctx.phase.fill (phase);

    uiLoopModeCode.store (static_cast<uint8_t> (ctx.mode), std::memory_order_relaxed);
    publishRegion (regionSt, regionEnd, regionFirst, regionLast);
    setState (TransportState::playing);
    uiPos.store (static_cast<size_t> (pos), std::memory_order_relaxed);
    publishTrackPositions (doc);
}

void Transport::stop() noexcept
{
    setState (TransportState::stopped);
    pos = 0.0;
    advanceScale = 1.0;
    xfadeLeft = 0;
    uiPos.store (0, std::memory_order_relaxed);
    for (auto& p : uiTrackPos) p.store (kNoPosition, std::memory_order_relaxed);
    // Nothing is playing, so there is no playing region. The UI falls back to the
    // selection — which is what the next play() would loop. Pause does NOT clear
    // it: a held loop is still that loop.
    uiRegionSt.store (0, std::memory_order_relaxed);
    uiRegionEnd.store (0, std::memory_order_relaxed);
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

    // A loop follows the document. If what it should loop now (the Set Loop region,
    // the selection, or the whole sound) is not what it is looping, re-aim it: a
    // track whose playhead is inside the new range does not move at all — it just
    // wraps at the new ends — and one that is outside restarts at the range's start
    // (the end, going backward), under the crossfade.
    // While a take runs (excludeMask) the aim is frozen: the Recorder writes the span
    // it snapshotted, and the written tracks' playheads must not jump. A selection
    // drawn or cleared mid-take re-aims on the first block after the take ends.
    if (playOpts.loop && ctx.mode != LoopMode::once && excludeMask == 0)
    {
        const Aim a = loopAim (doc, playOpts);
        const bool same = a.mode == ctx.mode
            && (a.mode == LoopMode::all
                || (a.st == ctx.selSt && a.end == ctx.selEnd
                    && a.first == ctx.firstSelTrack && a.last == ctx.lastSelTrack));
        if (! same)
        {
            PlayContext next = ctx;
            next.mode = a.mode;
            next.selSt = a.mode == LoopMode::selection ? a.st : 0;
            next.selEnd = a.mode == LoopMode::selection ? a.end : 0;
            next.firstSelTrack = a.first; next.lastSelTrack = a.last;
            anchor (doc, next);
            uiLoopModeCode.store (static_cast<uint8_t> (ctx.mode), std::memory_order_relaxed);
            if (a.mode == LoopMode::selection) publishRegion (a.st, a.end, a.first, a.last);
            else                               publishRegion (0, doc.maxTrackSize(), 1, doc.numTracks());
        }
    }

    const double s         = step (doc);
    const bool   playOnce  = ctx.mode == LoopMode::once;

    MixOptions mix;
    mix.excludeMask = excludeMask;

    int written = 0;
    for (int i = 0; i < frames; ++i)
    {
        // pos is MONOTONIC — each track maps it through its own phase and span. Only
        // play-once has a stop point; the loop modes run forever.
        if (playOnce && pos >= stopPos) { setState (TransportState::stopped); break; }

        StereoFrame f = mixFrameAt (doc, pos, ctx, mix);
        if (xfadeLeft > 0)
        {
            const StereoFrame o = mixFrameAt (doc, pos, prev, mix);
            const float w = 1.0f - static_cast<float> (xfadeLeft) / static_cast<float> (xfadeTotal);
            f.left  = o.left  + (f.left  - o.left)  * w;
            f.right = o.right + (f.right - o.right) * w;
            --xfadeLeft;
        }
        left[i]  = f.left;
        right[i] = f.right;

        pos += s;
        ++written;
    }

    uiPos.store (framePos(), std::memory_order_relaxed);
    publishTrackPositions (doc);
    return written;
}

} // namespace se
