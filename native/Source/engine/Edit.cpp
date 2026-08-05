#include "Edit.h"

#include "Resample.h"
#include "SampleFormat.h"

#include <algorithm>

namespace se {

namespace {

void spliceOut (std::vector<uint8_t>& data, size_t start, size_t end)
{
    if (end <= start || start >= data.size()) return;
    end = std::min (end, data.size());
    data.erase (data.begin() + static_cast<std::ptrdiff_t> (start),
                data.begin() + static_cast<std::ptrdiff_t> (end));
}

void spliceIn (std::vector<uint8_t>& data, size_t at, const uint8_t* bytes, size_t n)
{
    at = std::min (at, data.size());
    data.insert (data.begin() + static_cast<std::ptrdiff_t> (at), bytes, bytes + n);
}

} // namespace

ByteRange trackRange (const SoundDocument& doc, int trackIndex, size_t selSt, size_t selEnd)
{
    const size_t frameSize = static_cast<size_t> (doc.frameSize());
    const size_t trackSize = doc.tracks[static_cast<size_t> (trackIndex)].data.size();
    size_t start = std::min (selSt, trackSize);
    size_t end   = std::min (selEnd, trackSize);
    start -= start % frameSize;
    end   -= end   % frameSize;
    return { start, end };
}

void setSelection (SoundDocument& doc, size_t start, size_t end)
{
    const size_t frameSize = static_cast<size_t> (doc.frameSize());
    const size_t limit = doc.maxTrackSize();
    size_t s = std::min (start, limit);
    size_t e = std::min (end, limit);
    if (e < s) std::swap (s, e);
    s -= s % frameSize;
    e -= e % frameSize;
    doc.selSt = s;
    doc.selEnd = e;
}

void setLoop (SoundDocument& doc)
{
    doc.loopSt = doc.selSt;
    doc.loopEnd = doc.selEnd;
}

bool clear (SoundDocument& doc)
{
    if (doc.selSt == doc.selEnd) return false;
    bool changed = false;
    for (int i : doc.selectedTrackIndices())
    {
        const auto r = trackRange (doc, i, doc.selSt, doc.selEnd);
        if (r.end > r.start)
        {
            spliceOut (doc.tracks[static_cast<size_t> (i)].data, r.start, r.end);
            changed = true;
        }
    }
    if (changed) doc.selEnd = doc.selSt;   // collapse to a caret at the cut point
    return changed;
}

std::unique_ptr<SoundDocument> copy (const SoundDocument& doc)
{
    if (doc.selSt == doc.selEnd) return nullptr;

    DocumentInit init;
    init.name = "Clipboard";
    init.rateHz = doc.rateHz;
    init.bps = doc.bps;
    init.baseFreq = doc.baseFreq;
    init.numTracks = 0;
    auto clip = std::make_unique<SoundDocument> (init);

    for (int i : doc.selectedTrackIndices())
    {
        const auto& src = doc.tracks[static_cast<size_t> (i)];
        const auto r = trackRange (doc, i, doc.selSt, doc.selEnd);
        Track t;
        t.outLeft = src.outLeft;
        t.outRight = src.outRight;
        t.volume = src.volume;
        if (r.end > r.start)
            t.data.assign (src.data.begin() + static_cast<std::ptrdiff_t> (r.start),
                           src.data.begin() + static_cast<std::ptrdiff_t> (r.end));
        clip->tracks.push_back (std::move (t));
    }

    if (clip->numTracks() == 0) return nullptr;
    clip->firstSelTrack = 1;
    clip->lastSelTrack = clip->numTracks();
    clip->selSt = 0;
    clip->selEnd = clip->maxTrackSize();
    return clip;
}

std::unique_ptr<SoundDocument> cut (SoundDocument& doc)
{
    auto clip = copy (doc);
    if (clip) clear (doc);
    return clip;
}

int paste (SoundDocument& doc, const SoundDocument& clip)
{
    // Conform the clipboard to the destination's format first. Without this a
    // 16-bit clip pasted into an 8-bit document reinterprets bytes at the wrong
    // frame width and mangles the audio. Matches the app's Resample-on-paste.
    std::unique_ptr<SoundDocument> conformed;
    const SoundDocument* clipPtr = &clip;
    if (clip.bps != doc.bps || clip.rateHz != doc.rateHz)
    {
        conformed = std::make_unique<SoundDocument> (clip.clone());
        if (conformed->bps != doc.bps)       resampleSoundBits (*conformed, doc.bps);
        if (conformed->rateHz != doc.rateHz) resampleSoundRate (*conformed, doc.rateHz);
        clipPtr = conformed.get();
    }
    const SoundDocument& src = *clipPtr;

    if (doc.selSt != doc.selEnd) clear (doc);
    const size_t at = doc.selSt;
    int pasted = 0;
    size_t maxLen = 0;

    for (int j = 0; j < src.numTracks(); ++j)
    {
        const int target = doc.firstSelTrack - 1 + j;
        // Create destination tracks as needed so an N-track copy always lands
        // across N tracks (the new tracks are zero-length, padded to the caret
        // below). This diverges from the original, which dropped surplus tracks.
        while (target > doc.numTracks() - 1) doc.addTrack();

        auto& dst = doc.tracks[static_cast<size_t> (target)].data;
        const auto& bytes = src.tracks[static_cast<size_t> (j)].data;

        // A track shorter than the caret is padded to the caret first.
        if (dst.size() < at)
            dst.resize (at, silentByte (doc.bps));

        spliceIn (dst, at, bytes.data(), bytes.size());
        maxLen = std::max (maxLen, bytes.size());
        ++pasted;
    }

    if (pasted > 0)
    {
        doc.selSt = at;
        doc.selEnd = at + maxLen;
    }
    return pasted;
}

bool crop (SoundDocument& doc)
{
    if (doc.selSt == doc.selEnd) return false;
    bool changed = false;
    for (int i : doc.selectedTrackIndices())
    {
        auto& data = doc.tracks[static_cast<size_t> (i)].data;
        const auto r = trackRange (doc, i, doc.selSt, doc.selEnd);
        // A selected track wholly before the selection crops to empty — the
        // clamp collapses its range, same as slicing past a short track's end.
        data.erase (data.begin() + static_cast<std::ptrdiff_t> (r.end), data.end());
        data.erase (data.begin(), data.begin() + static_cast<std::ptrdiff_t> (r.start));
        changed = true;
    }
    if (changed)
    {
        doc.selSt = 0;
        doc.selEnd = doc.maxSelectedTrackSize();
    }
    return changed;
}

void insertSilence (SoundDocument& doc, size_t bytes)
{
    const size_t frameSize = static_cast<size_t> (doc.frameSize());
    const size_t len = bytes - (bytes % frameSize);
    if (len == 0) return;
    const size_t at = doc.selSt;
    const uint8_t silent = silentByte (doc.bps);

    for (int i : doc.selectedTrackIndices())
    {
        auto& data = doc.tracks[static_cast<size_t> (i)].data;
        const size_t insertAt = std::min (at, data.size());
        data.insert (data.begin() + static_cast<std::ptrdiff_t> (insertAt), len, silent);
    }
    doc.selSt = at;
    doc.selEnd = at + len;
}

void silenceSelection (SoundDocument& doc)
{
    const uint8_t silent = silentByte (doc.bps);
    for (int i : doc.selectedTrackIndices())
    {
        auto& data = doc.tracks[static_cast<size_t> (i)].data;
        const auto r = trackRange (doc, i, doc.selSt, doc.selEnd);
        for (size_t p = r.start; p < r.end && p < data.size(); ++p) data[p] = silent;
    }
}

} // namespace se
