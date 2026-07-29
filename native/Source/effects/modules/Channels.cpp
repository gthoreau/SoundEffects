// The track-math modules — Sum, Difference, Selection to Channel.
// SPEC §8 #33/#5/#29. [DOC], not byte-verified.
// Ported from `src/effects/channels.ts`.
//
// These are the modules that exercise the unequal-track model hardest: they read
// two tracks of possibly different lengths and write a third.
//
// Where one track has run out it contributes DIGITAL ZERO rather than being
// skipped. That matches Iterative Crossfade's confirmed zero-padding
// ([BINARY: Sample-Hose-Iterative-Crossfade.md]) rather than Select Peak's
// skip-short-tracks rule. The two conventions genuinely differ in this app and
// conflating them would be easy.

#include "Modules.h"

#include "../Skeleton.h"
#include "engine/SampleFormat.h"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <vector>

namespace se::modules {

namespace {

const char* kNoOptions = "Sorry, man. I haven't got any options.";

/** The two tracks a pairwise module reads: the first two SELECTED ones. */
struct Pair
{
    bool      ok = false;
    int       aIdx = 0, bIdx = 0;
    ByteRange ra, rb;
    size_t    frames = 0;
};

/**
 * Resolve the pair and the frame count the output spans — the LONGER of the two
 * ranges, since the shorter zero-pads rather than cutting the result short.
 *
 * The reference indexes both tracks unguarded and would throw on a document that
 * cannot supply them; the `< 2` test above it is what normally prevents that. The
 * bounds check here declines instead, which is the same outcome by a safer route.
 */
Pair resolvePair (SoundDocument& doc, size_t frameSize)
{
    Pair p;
    if (doc.lastSelTrack - doc.firstSelTrack + 1 < 2) return p;

    p.aIdx = doc.firstSelTrack - 1;
    p.bIdx = doc.firstSelTrack;          // the second selected track
    if (p.aIdx < 0 || p.bIdx >= doc.numTracks()) return p;

    p.ra = trackRange (doc, p.aIdx, doc.selSt, doc.selEnd);
    p.rb = trackRange (doc, p.bIdx, doc.selSt, doc.selEnd);
    p.frames = std::max ((p.ra.end - p.ra.start) / frameSize,
                         (p.rb.end - p.rb.start) / frameSize);
    p.ok = true;
    return p;
}

/** Read frame `i` of a range, or 0 once that track has run out. */
int64_t sampleOrZero (const std::vector<uint8_t>& data, const ByteRange& r,
                      size_t i, size_t frameSize, int bps)
{
    const size_t p = r.start + i * frameSize;
    if (p + frameSize > r.end) return 0;
    return getSample (data.data(), p, bps);
}

/** `b' = op(a, b)` over the two selected tracks, written back into b. */
ModResult combine (SoundDocument& doc, Host& host,
                   const std::function<int64_t (int64_t, int64_t)>& op,
                   const char* text)
{
    if (doc.lastSelTrack - doc.firstSelTrack + 1 < 2) return kModCancel;

    expandEmptySelection (doc);
    host.setupProgress (text);

    const size_t frameSize = static_cast<size_t> (doc.frameSize());
    const Host::Limits limits = Host::getSampleValueLimits (doc.bps);
    const int bps = doc.bps;

    const Pair p = resolvePair (doc, frameSize);
    if (! p.ok)
    {
        host.closeProgress();
        return kModCancel;
    }

    const std::vector<uint8_t>& a = doc.tracks[static_cast<size_t> (p.aIdx)].data;
    std::vector<uint8_t>&       b = doc.tracks[static_cast<size_t> (p.bIdx)].data;

    for (size_t i = 0; i < p.frames; ++i)
    {
        const size_t pb = p.rb.start + i * frameSize;

        const int64_t va = sampleOrZero (a, p.ra, i, frameSize, bps);
        const int64_t vb = sampleOrZero (b, p.rb, i, frameSize, bps);

        if (pb + frameSize > p.rb.end) continue;   // nowhere to write

        int64_t v = op (va, vb);
        if (v < limits.min) v = limits.min;
        else if (v > limits.max) v = limits.max;

        setSample (b.data(), pb, static_cast<int32_t> (v), bps);
    }

    host.closeProgress();
    return kModNoError;
}

} // namespace

// --- Sum ---------------------------------------------------------------------

EffectModule makeSum()
{
    EffectModule m;
    m.name = "Sum";
    m.signature = "Sum";
    m.group = EffectGroup::Misc;
    m.version = "1.1";
    m.author = "Alberto Ricci";
    m.progressText = "Summing…";
    m.strings = { kNoOptions };
    // At LEAST two, not exactly two: the module takes the first two selected and
    // ignores the rest, and Difference documents an "…from the first one?" prompt
    // for that case. Greying it out on three would refuse work the original does.
    m.requires.minSelectedTracks = 2;

    m.effect = [] (SoundDocument& doc, Host& host, const Settings&) -> ModResult
    {
        return combine (doc, host, [] (int64_t a, int64_t b) { return a + b; }, "Summing…");
    };

    return m;
}

// --- Difference --------------------------------------------------------------

EffectModule makeDifference()
{
    EffectModule m;
    m.name = "Difference";
    m.signature = "Difference";
    m.group = EffectGroup::Misc;
    m.version = "1.1.1";
    m.author = "Alberto Ricci";
    m.progressText = "Subtracting…";
    m.requires.minSelectedTracks = 2;
    m.requires.createsTracks = true;
    m.requires.createsDocument = true;   // when destination = New Document

    m.defaults = [] { return Settings { { "destination", Value (int64_t (1)) },
                                        { "normalize", Value (false) } }; };

    m.schema = []
    {
        return SettingsSchema {
            radioGroup ("destination", { "New Channel", "New Document" }),
            checkBox ("normalize", "Normalize the difference"),
        };
    };

    // Unlike Sum, Difference does NOT overwrite a source track — the whole point is
    // comparing two takes, so destroying one of them would defeat it.
    m.effect = [] (SoundDocument& doc, Host& host, const Settings& prefs) -> ModResult
    {
        if (doc.lastSelTrack - doc.firstSelTrack + 1 < 2) return kModCancel;

        expandEmptySelection (doc);
        host.setupProgress ("Subtracting…");

        const size_t frameSize = static_cast<size_t> (doc.frameSize());
        const Host::Limits limits = Host::getSampleValueLimits (doc.bps);
        const int bps = doc.bps;

        const Pair p = resolvePair (doc, frameSize);
        if (! p.ok || p.frames < 1)
        {
            host.closeProgress();
            return kModCancel;
        }

        const std::vector<uint8_t>& a = doc.tracks[static_cast<size_t> (p.aIdx)].data;
        const std::vector<uint8_t>& b = doc.tracks[static_cast<size_t> (p.bIdx)].data;

        std::vector<int64_t> diff (p.frames);
        int64_t peak = 0;

        for (size_t i = 0; i < p.frames; ++i)
        {
            const int64_t v = sampleOrZero (a, p.ra, i, frameSize, bps)
                            - sampleOrZero (b, p.rb, i, frameSize, bps);
            diff[i] = v;
            const int64_t mag = std::abs (v);
            if (mag > peak) peak = mag;
        }

        // "Normalize the difference" — the usual reason to run this is to see HOW
        // SMALL the difference is, so scaling it up to full scale is the point.
        const int64_t gain = (getBool (prefs, "normalize") && peak > 0)
            ? (limits.max * 100) / peak
            : 100;

        std::vector<uint8_t> out (p.frames * frameSize, silentByte (doc.bps));
        for (size_t i = 0; i < p.frames; ++i)
        {
            int64_t v = gain == 100 ? diff[i] : (diff[i] * gain) / 100;
            if (v < limits.min) v = limits.min;
            else if (v > limits.max) v = limits.max;
            setSample (out.data(), i * frameSize, static_cast<int32_t> (v), bps);
        }

        if (getInt (prefs, "destination") == 2)
        {
            // ModNewDocument: effect() can only return an OSErr, so the host owns
            // the result and opens it as a Window.
            SoundDocument& fresh = host.newDocument (doc);
            fresh.addTrack();
            fresh.tracks[0].data = std::move (out);
        }
        else
        {
            host.addNewTrack (doc);
            doc.tracks[static_cast<size_t> (doc.numTracks() - 1)].data = std::move (out);
        }

        host.closeProgress();
        return kModNoError;
    };

    return m;
}

// --- Selection to Channel ----------------------------------------------------

EffectModule makeSelectionToChannel()
{
    EffectModule m;
    m.name = "Selection to Channel";
    m.signature = "SelectionToChannel";
    m.version = "1.1";
    m.author = "Alberto Ricci";
    m.progressText = "Copying…";
    m.strings = { kNoOptions };
    m.requires.createsTracks = true;

    // One new track per selected track, appended, each carrying that track's OWN
    // clamped intersection — so copying an unequal-length selection yields
    // unequal-length new tracks.
    m.effect = [] (SoundDocument& doc, Host& host, const Settings&) -> ModResult
    {
        expandEmptySelection (doc);
        host.setupProgress ("Copying…");

        // Every slice is taken BEFORE any track is added: appending tracks while
        // walking the selected ones would have the module copying its own output.
        std::vector<std::vector<uint8_t>> slices;
        for (const int i : doc.selectedTrackIndices())
        {
            const ByteRange r = trackRange (doc, i, doc.selSt, doc.selEnd);
            const auto& data = doc.tracks[static_cast<size_t> (i)].data;
            const size_t end = std::max (r.start, r.end);
            slices.emplace_back (data.begin() + static_cast<std::ptrdiff_t> (r.start),
                                 data.begin() + static_cast<std::ptrdiff_t> (end));
        }

        if (slices.empty())
        {
            host.closeProgress();
            return kModCancel;
        }

        for (auto& slice : slices)
        {
            host.addNewTrack (doc);
            doc.tracks[static_cast<size_t> (doc.numTracks() - 1)].data = std::move (slice);
        }

        host.closeProgress();
        return kModNoError;
    };

    return m;
}

} // namespace se::modules
