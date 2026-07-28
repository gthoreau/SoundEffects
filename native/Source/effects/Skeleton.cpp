#include "Skeleton.h"

#include "engine/Edit.h"
#include "engine/SampleFormat.h"

namespace se {

std::vector<TrackPass> trackPasses (SoundDocument& doc)
{
    std::vector<TrackPass> passes;

    for (int track = doc.firstSelTrack - 1; track < doc.lastSelTrack; ++track)
    {
        if (track < 0 || track >= doc.numTracks()) continue;

        auto& data = doc.tracks[static_cast<size_t> (track)].data;
        const ByteRange r = trackRange (doc, track, doc.selSt, doc.selEnd);

        // Gain.c's `if (start != end)`: a track shorter than the selection has an
        // empty range and is skipped entirely, not padded.
        if (r.start == r.end) continue;

        passes.push_back ({ track, data.data(), data.size(), r.start, r.end });
    }

    return passes;
}

ModResult runSkeleton (SoundDocument& doc, Host& host, const SkeletonOptions& opts,
                       const std::function<ModResult (const TrackPass&, Progress&)>& body)
{
    host.setupProgress (opts.text);

    // Step 4 — and this is what makes "nothing selected inside the Window" mean
    // "the whole Window" without the panel doing anything special.
    expandEmptySelection (doc);

    const size_t frameSize = static_cast<size_t> (doc.frameSize());
    const size_t total = host.getBytesToProcess (doc) / frameSize;
    Progress progress (host, total, opts.canCancel);

    ModResult result = kModCancel;

    // Each pass is computed immediately before its body runs, never all up front.
    // The reference iterates a GENERATOR here, and that laziness is load-bearing:
    // Echo, Delay and Replicate grow their own track's buffer inside the body, so
    // a pass measured earlier would carry a stale size and a dangling pointer.
    for (int track = doc.firstSelTrack - 1; track < doc.lastSelTrack; ++track)
    {
        if (track < 0 || track >= doc.numTracks()) continue;

        auto& data = doc.tracks[static_cast<size_t> (track)].data;
        const ByteRange r = trackRange (doc, track, doc.selSt, doc.selEnd);

        // Gain.c's `if (start != end)`: a track shorter than the selection has an
        // empty range and is skipped entirely, not padded.
        if (r.start == r.end) continue;

        const TrackPass pass { track, data.data(), data.size(), r.start, r.end };

        const ModResult bodyResult = body (pass, progress);
        result = kModNoError;
        if (bodyResult != kModNoError)
        {
            host.closeProgress();
            return bodyResult;
        }
    }

    host.closeProgress();
    return result;
}

ModResult perSampleEffect (SoundDocument& doc, Host& host, const SkeletonOptions& opts,
                           const std::function<int64_t (int32_t, const TrackPass&, size_t)>& kernel)
{
    const Host::Limits limits = Host::getSampleValueLimits (doc.bps);
    const size_t frameSize = static_cast<size_t> (doc.frameSize());
    const int bps = doc.bps;

    return runSkeleton (doc, host, opts, [&] (const TrackPass& pass, Progress& progress) -> ModResult
    {
        for (size_t p = pass.start; p < pass.end; p += frameSize)
        {
            int64_t s = kernel (getSample (pass.data, p, bps), pass, p);

            // Step 7 — saturating clamp. Gain.c tests the sign bit first; the
            // effect is a plain clamp and the branch order is not observable.
            if (s < limits.min) s = limits.min;
            else if (s > limits.max) s = limits.max;

            setSample (pass.data, p, static_cast<int32_t> (s), bps);

            // Cancelled: keep the mangled half (§1.2).
            if (progress.tick()) return kModNoError;
        }
        return kModNoError;
    });
}

size_t growAndSilence (SoundDocument& doc, int trackIndex, size_t wanted)
{
    auto& data = doc.tracks[static_cast<size_t> (trackIndex)].data;
    const size_t oldSize = data.size();
    if (wanted <= oldSize) return oldSize;

    // ModSilenceOut(mi, ch+1, ch+1, oldSize, wanted) — before any mix.
    data.resize (wanted, silentByte (doc.bps));
    return wanted;
}

} // namespace se
