// Quick Mix — SPEC §8 #23. [DOC+RES], not byte-verified.
// Ported from `src/effects/quick-mix.ts`.
//
// Mixes the selected tracks down to a New Channel or a New Document, in Mono or
// Stereo. Stereo mode routes by each track's L/R speaker buttons — the same
// toggles that sit beside each lane in the wave editor (§3), which is a nice piece
// of design: the routing you hear while editing is the routing you get when you
// mix.
//
// [INTERPRETED]: the mix is a plain sum with a saturating clamp, no averaging and
// no headroom scaling. That matches Sum's behaviour and this app's general refusal
// to protect you from yourself — but it is inference. "Use each channel's pan"
// scales by the per-track volume; whether it also applies the 8.8 volume field is
// unrecorded. See SPEC §14.

#include "Modules.h"

#include "../Skeleton.h"
#include "engine/SampleFormat.h"
#include "engine/Track.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace se::modules {

EffectModule makeQuickMix()
{
    EffectModule m;
    m.name = "Quick Mix";
    m.signature = "QuickMix";
    m.version = "1.0";
    m.author = "Alberto Ricci";
    m.progressText = "Mixing…";
    m.requires.createsTracks = true;
    m.requires.createsDocument = true;   // when destination = New Document

    m.defaults = [] { return Settings { { "destination", Value (int64_t (1)) },
                                        { "use", Value (int64_t (1)) },
                                        { "usePan", Value (false) } }; };

    m.schema = []
    {
        return SettingsSchema {
            // Headings. These happen to render correctly as plain static text
            // because a radio group declines a suffix — but that is the next
            // item's business, not theirs, and saying what they are keeps them
            // right if the row types around them ever change.
            staticHeading ("Mix to"),
            radioGroup ("destination", { "New Channel", "New Document" }),
            staticHeading ("Use"),
            radioGroup ("use", { "Mono", "Stereo" }),
            checkBox ("usePan", "Use each channel's pan"),
        };
    };

    m.effect = [] (SoundDocument& doc, Host& host, const Settings& prefs) -> ModResult
    {
        expandEmptySelection (doc);
        host.setupProgress ("Mixing…");

        const size_t frameSize = static_cast<size_t> (doc.frameSize());
        const Host::Limits limits = Host::getSampleValueLimits (doc.bps);
        const int bps = doc.bps;

        const std::vector<int> indices = doc.selectedTrackIndices();
        if (indices.empty())
        {
            host.closeProgress();
            return kModCancel;
        }

        // The mix spans the LONGEST selected track; shorter ones contribute zero
        // past their end, as Iterative Crossfade does.
        size_t frames = 0;
        for (const int i : indices)
        {
            const ByteRange r = trackRange (doc, i, doc.selSt, doc.selEnd);
            frames = std::max (frames, (r.end - r.start) / frameSize);
        }
        if (frames < 1)
        {
            host.closeProgress();
            return kModCancel;
        }

        const bool   stereo = getInt (prefs, "use") == 2;
        const bool   usePan = getBool (prefs, "usePan");
        const size_t outCount = stereo ? 2u : 1u;

        // Accumulated in DOUBLE and truncated once at the end, not clamped per
        // contribution: a mix that swings past the rail and back must come home,
        // which it would not if every addition were pinned on the way.
        std::vector<std::vector<double>> acc (outCount, std::vector<double> (frames, 0.0));

        for (const int i : indices)
        {
            const auto& track = doc.tracks[static_cast<size_t> (i)];
            const ByteRange r = trackRange (doc, i, doc.selSt, doc.selEnd);
            // 8.8 fixed: 0x0100 = 1.0. Values above unity overdrive on purpose.
            const double vol = usePan
                ? static_cast<double> (track.volume) / static_cast<double> (VOLUME_UNITY)
                : 1.0;

            for (size_t f = 0; f < frames; ++f)
            {
                const size_t p = r.start + f * frameSize;
                if (p + frameSize > r.end) break;

                const double s = static_cast<double> (getSample (track.data.data(), p, bps)) * vol;

                if (! stereo)
                {
                    acc[0][f] += s;
                }
                else
                {
                    // Routed by the lane's own speaker toggles.
                    if (track.outLeft)  acc[0][f] += s;
                    if (track.outRight) acc[1][f] += s;
                }
            }
        }

        std::vector<std::vector<uint8_t>> outputs;
        outputs.reserve (outCount);

        for (const auto& a : acc)
        {
            std::vector<uint8_t> out (frames * frameSize, silentByte (bps));
            for (size_t f = 0; f < frames; ++f)
            {
                int64_t v = static_cast<int64_t> (std::trunc (a[f]));
                if (v < limits.min) v = limits.min;
                else if (v > limits.max) v = limits.max;
                setSample (out.data(), f * frameSize, static_cast<int32_t> (v), bps);
            }
            outputs.push_back (std::move (out));
        }

        if (getInt (prefs, "destination") == 2)
        {
            // ModNewDocument: the host owns the resulting window and picks it up
            // from spawnedDocuments. The SOURCE document is left untouched.
            SoundDocument& fresh = host.newDocument (doc);
            for (auto& out : outputs)
            {
                fresh.addTrack();
                fresh.tracks[static_cast<size_t> (fresh.numTracks() - 1)].data = std::move (out);
            }
            host.closeProgress();
            return kModNoError;
        }

        for (auto& out : outputs)
        {
            host.addNewTrack (doc);
            doc.tracks[static_cast<size_t> (doc.numTracks() - 1)].data = std::move (out);
        }

        host.closeProgress();
        return kModNoError;
    };

    return m;
}

} // namespace se::modules
