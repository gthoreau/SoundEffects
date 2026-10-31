#include "Edit.h"

#include "Resample.h"
#include "SampleFormat.h"

#include <algorithm>
#include <cstring>

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

bool selectLoop (SoundDocument& doc)
{
    if (doc.maxTrackSize() == 0) return false;
    doc.selSt = doc.loopSt;
    doc.selEnd = doc.loopEnd;
    doc.firstSelTrack = 1;
    doc.lastSelTrack = doc.numTracks();
    return true;
}

bool extendToBeginning (SoundDocument& doc)
{
    if (doc.maxSelectedTrackSize() == 0) return false;
    doc.selSt = 0;
    return true;
}

bool extendToEnd (SoundDocument& doc)
{
    const size_t max = doc.maxSelectedTrackSize();
    if (max == 0) return false;
    doc.selEnd = max;
    return true;
}

bool extendAbove (SoundDocument& doc)
{
    if (doc.firstSelTrack <= 1) return false;
    --doc.firstSelTrack;
    return true;
}

bool extendBelow (SoundDocument& doc)
{
    if (doc.lastSelTrack >= doc.numTracks()) return false;
    ++doc.lastSelTrack;
    return true;
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
    if (clip.bps != doc.bps || ! sameRate (clip.rateHz, doc.rateHz))
    {
        conformed = std::make_unique<SoundDocument> (clip.clone());
        if (conformed->bps != doc.bps)       resampleSoundBits (*conformed, doc.bps);
        if (! sameRate (conformed->rateHz, doc.rateHz)) resampleSoundRate (*conformed, doc.rateHz);
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

bool duplicate (SoundDocument& doc)
{
    auto clip = copy (doc);
    if (clip == nullptr) return false;
    doc.selSt = doc.selEnd;
    return paste (doc, *clip) > 0;
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

// --- Shift Sound… / Leave Silence… ----------------------------------------------

namespace {

size_t leadingSilence (const std::vector<uint8_t>& data, size_t frameSize)
{
    size_t run = 0;
    if (frameSize == 1)
        while (run < data.size() && data[run] == 0x80) ++run;
    else
    {
        while (run < data.size() && data[run] == 0x00) ++run;
        run -= run % frameSize;   // a frame is silent only if all its bytes are zero
    }
    return run;
}

void prependSilence (std::vector<uint8_t>& data, size_t n, uint8_t silent)
{
    data.insert (data.begin(), n, silent);
}

void fillSilence (std::vector<uint8_t>& data, size_t from, size_t to, uint8_t silent)
{
    std::fill (data.begin() + static_cast<std::ptrdiff_t> (from),
               data.begin() + static_cast<std::ptrdiff_t> (to), silent);
}

/** BlockMove: overlapping ranges are fine. */
void moveBytes (std::vector<uint8_t>& data, size_t from, size_t to, size_t len)
{
    if (len > 0) std::memmove (data.data() + to, data.data() + from, len);
}

} // namespace

ShiftScope shiftScope (const SoundDocument& doc)
{
    const bool caret = doc.selSt == doc.selEnd;
    const bool all = doc.firstSelTrack == 1 && doc.lastSelTrack == doc.numTracks();
    ShiftScope s;
    if ((caret || (doc.selSt == 0 && doc.selEnd == doc.maxTrackSize())) && all)
    {
        s.kind = ShiftScope::Kind::sound;
        s.firstTrack = 1; s.lastTrack = doc.numTracks();
        s.start = 0; s.end = doc.maxTrackSize();
    }
    else if (caret || (doc.selSt == 0 && doc.selEnd == doc.maxSelectedTrackSize()))
    {
        s.kind = doc.firstSelTrack == doc.lastSelTrack ? ShiftScope::Kind::track : ShiftScope::Kind::tracks;
        s.firstTrack = doc.firstSelTrack; s.lastTrack = doc.lastSelTrack;
        s.start = 0; s.end = doc.maxSelectedTrackSize();
    }
    else
    {
        s.kind = ShiftScope::Kind::selection;
        s.firstTrack = doc.firstSelTrack; s.lastTrack = doc.lastSelTrack;
        s.start = doc.selSt; s.end = doc.selEnd;
    }
    return s;
}

std::string shiftCaption (const ShiftScope& s)
{
    switch (s.kind)
    {
        case ShiftScope::Kind::sound:     return "Shift the whole sound by:";
        case ShiftScope::Kind::track:     return "Shift channel number " + std::to_string (s.firstTrack) + " by:";
        case ShiftScope::Kind::tracks:    return "Shift channels " + std::to_string (s.firstTrack) + " to "
                                               + std::to_string (s.lastTrack) + " by:";
        case ShiftScope::Kind::selection: return "Shift the selection by:";
    }
    return {};
}

bool shiftSound (SoundDocument& doc, size_t bytes, bool right)
{
    const size_t frameSize = static_cast<size_t> (doc.frameSize());
    const size_t amount = bytes - (bytes % frameSize);
    if (amount == 0) return false;
    const ShiftScope scope = shiftScope (doc);
    size_t start = scope.start, end = scope.end;
    const uint8_t silent = silentByte (doc.bps);

    if (! right)
    {
        if (start < amount)
        {
            const size_t grow = amount - start;
            for (auto& t : doc.tracks)
                if (! t.data.empty()) prependSilence (t.data, grow, silent);
            start += grow; end += grow;
            doc.selSt += grow; doc.selEnd += grow;
        }
        for (int i = scope.firstTrack - 1; i <= scope.lastTrack - 1; ++i)
        {
            auto& data = doc.tracks[static_cast<size_t> (i)].data;
            const size_t size = data.size();
            const size_t s = std::min (start, size), e = std::min (end, size), len = e - s;
            if (len > 0) moveBytes (data, s, s - amount, len);
            if (e == size)       data.resize (size - std::min (len, amount));
            else if (len > amount) fillSilence (data, e - amount, e, silent);
            else                 fillSilence (data, s, e, silent);
        }
        doc.selSt -= amount; doc.selEnd -= amount;
    }
    else
    {
        for (int i = scope.firstTrack - 1; i <= scope.lastTrack - 1; ++i)
        {
            auto& data = doc.tracks[static_cast<size_t> (i)].data;
            const size_t size = data.size();
            const size_t s = std::min (start, size), e = std::min (end, size);
            if (s == e) continue;
            if (e + amount > size) data.resize (e + amount, silent);
            moveBytes (data, s, s + amount, e - s);
            fillSilence (data, s, std::min (s + amount, e), silent);
        }
        doc.selSt += amount; doc.selEnd += amount;
    }
    return true;
}

void leaveSilence (SoundDocument& doc, size_t bytes, bool wholeSound)
{
    const size_t frameSize = static_cast<size_t> (doc.frameSize());
    const size_t want = bytes - (bytes % frameSize);
    const uint8_t silent = silentByte (doc.bps);
    if (wholeSound)
    {
        doc.firstSelTrack = 1;
        doc.lastSelTrack = doc.numTracks();
    }

    // The shortest lead-in among the audible tracks; silent ones are emptied.
    size_t run = doc.maxTrackSize();
    bool audible = false;
    for (int i : doc.selectedTrackIndices())
    {
        auto& data = doc.tracks[static_cast<size_t> (i)].data;
        const size_t lead = leadingSilence (data, frameSize);
        if (run > lead && lead != data.size()) { run = lead; audible = true; }
        else if (lead == data.size())          data.clear();
    }
    if (! audible) { run = 0; doc.selSt = doc.selEnd = 0; }

    for (int i : doc.selectedTrackIndices())
    {
        auto& data = doc.tracks[static_cast<size_t> (i)].data;
        if (data.empty())     data.assign (want, silent);
        else if (run < want)  prependSilence (data, want - run, silent);   // its first `run` bytes are silent already
        else if (run > want)  spliceOut (data, want, run);
    }

    if (run < want)
    {
        const size_t delta = want - run;
        if (run < doc.selEnd) { doc.selSt += delta; doc.selEnd += delta; }
        else                  { doc.selSt = doc.selEnd = want; }
    }
    else
    {
        const size_t delta = run - want;
        doc.selSt  = delta < doc.selSt  ? doc.selSt  - delta : 0;
        doc.selEnd = delta < doc.selEnd ? doc.selEnd - delta : 0;
    }
}

// --- Select Exactly… / Select Peak… / Select Silence ----------------------------

void selectExactly (SoundDocument& doc, size_t n, ExactlyAnchor anchor)
{
    const size_t frameSize = static_cast<size_t> (doc.frameSize());
    const size_t maxSel = doc.maxSelectedTrackSize();
    // The original's unsigned "room left": an end already past maxSel wraps round
    // and reads as plenty (kept 32-bit, as the reference and the 68k compute it).
    const auto room = [maxSel] (size_t from) { return static_cast<uint32_t> (maxSel - from); };
    switch (anchor)
    {
        case ExactlyAnchor::before:
            doc.selEnd = doc.selSt;
            doc.selSt = n < doc.selSt ? doc.selSt - n : 0;
            break;
        case ExactlyAnchor::centered:
        {
            size_t mid = (doc.selSt + doc.selEnd) / 2;
            mid -= mid % frameSize;
            const size_t half = n / 2;
            doc.selSt = half > mid ? 0 : mid - half;
            doc.selEnd = room (doc.selSt) > n ? doc.selSt + n : maxSel;
            break;
        }
        case ExactlyAnchor::after:
            doc.selSt = doc.selEnd;
            doc.selEnd = room (doc.selEnd) > n ? doc.selEnd + n : maxSel;
            break;
    }
    doc.selSt -= doc.selSt % frameSize;
    doc.selEnd -= doc.selEnd % frameSize;
}

bool peakScopeIsSound (const SoundDocument& doc)
{
    const bool whole = doc.selSt == doc.selEnd || (doc.selSt == 0 && doc.selEnd == doc.maxTrackSize());
    return whole && doc.firstSelTrack == 1 && doc.lastSelTrack == doc.numTracks();
}

namespace {

/** |sum across the selected tracks| per frame, first maximum (CODE 4 @0x3090): 8-bit
    sums wrap at 16 bits, wider ones at 32; a track shorter than the position is
    skipped; frames wider than 4 bytes count their first 4. */
size_t findPeak (const SoundDocument& doc, size_t selSt, size_t selEnd, int firstTrack, int lastTrack)
{
    const size_t frameSize = static_cast<size_t> (doc.frameSize());
    const size_t n = std::min<size_t> (frameSize, 4);
    int64_t best = 0;
    size_t peakPos = 0;

    for (size_t pos = selSt; pos < selEnd; pos += frameSize)
    {
        int64_t acc = 0;
        for (int t = firstTrack - 1; t <= lastTrack - 1; ++t)
        {
            const auto& buf = doc.tracks[static_cast<size_t> (t)].data;
            if (pos >= buf.size()) continue;
            if (frameSize == 1)
                acc += static_cast<int8_t> (buf[pos] ^ 0x80);
            else
            {
                uint32_t v = 0;
                for (size_t b = 0; b < n; ++b)
                    v = (v << 8) | (pos + b < buf.size() ? buf[pos + b] : 0u);
                const int shift = static_cast<int> ((4 - n) * 8);
                acc += static_cast<int32_t> (v << shift) >> shift;
            }
        }
        const int64_t wrapped = frameSize == 1 ? static_cast<int16_t> (static_cast<uint16_t> (acc))
                                               : static_cast<int32_t> (static_cast<uint32_t> (acc));
        const int64_t metric = wrapped < 0 ? -wrapped : wrapped;
        if (metric > best) { best = metric; peakPos = pos; }
    }
    if (frameSize > 1) peakPos -= peakPos % frameSize;
    return peakPos;
}

bool silentFrameAt0 (const std::vector<uint8_t>& buf, size_t frameSize)
{
    if (buf.size() < frameSize) return false;
    if (frameSize == 1) return buf[0] == 0x80;
    for (size_t b = 0; b < frameSize; ++b)
        if (buf[b] != 0x00) return false;
    return true;
}

} // namespace

void selectPeak (SoundDocument& doc, size_t marginBytes, bool wholeSound)
{
    const size_t frameSize = static_cast<size_t> (doc.frameSize());
    if (wholeSound)
    {
        doc.firstSelTrack = 1;
        doc.lastSelTrack = doc.numTracks();
        doc.selSt = 0;
        doc.selEnd = doc.maxTrackSize();
    }
    else if (doc.selSt == doc.selEnd)
    {
        doc.selSt = 0;
        doc.selEnd = doc.maxSelectedTrackSize();
    }

    const size_t peakPos = findPeak (doc, doc.selSt, doc.selEnd, doc.firstSelTrack, doc.lastSelTrack);
    const auto maxSelLen = static_cast<int64_t> (doc.maxSelectedTrackSize());
    const auto margin = static_cast<int64_t> (marginBytes);

    int64_t start = static_cast<int64_t> (peakPos);
    int64_t end = start + static_cast<int64_t> (frameSize);
    start = margin < start ? start - margin : 0;
    end = maxSelLen - end > margin ? end + margin : maxSelLen;
    doc.selSt = static_cast<size_t> (start);
    doc.selEnd = static_cast<size_t> (end);
}

bool selectSilence (SoundDocument& doc)
{
    if (doc.selSt != doc.selEnd) return false;   // an insertion point is required
    const auto fs = static_cast<int64_t> (doc.frameSize());
    const auto caret = static_cast<int64_t> (doc.selSt);
    int64_t selSt = 0;
    int64_t selEnd = static_cast<int64_t> (doc.maxSelectedTrackSize());

    for (int t : doc.selectedTrackIndices())
    {
        const auto& buf = doc.tracks[static_cast<size_t> (t)].data;
        const auto size = static_cast<int64_t> (buf.size());
        const auto at = [&buf] (int64_t i) { return buf[static_cast<size_t> (i)]; };

        int64_t w = caret;
        bool adjusted = false;
        if (w == size) { w -= fs; adjusted = true; }
        if (size <= w) continue;   // too short: skipped

        // Forward: the right edge, one silent frame left out as margin.
        int64_t q = w;
        if (fs == 1) while (q < size && at (q) == 0x80) ++q;
        else
        {
            while (q < size && at (q) == 0x00) ++q;
            q -= q % fs;
        }
        int64_t endChan = q - fs;
        if (endChan < w) endChan = w;
        if (adjusted) endChan += fs;

        // Silence running to the track's end takes no margin. (The multi-byte test
        // walks DOWN from the last frame's first byte: the original's quirk.)
        if (endChan == size - fs && size >= fs)
        {
            bool lastSilent = true;
            if (fs == 1) lastSilent = at (size - 1) == 0x80;
            else
                for (int64_t i = 0; i < fs; ++i)
                    if (at (size - fs - i) != 0x00) { lastSilent = false; break; }
            if (lastSilent) endChan = size;
        }

        // Backward: the left edge.
        int64_t p = w;
        if (fs == 1) while (p >= 0 && at (p) == 0x80) --p;
        else
        {
            while (p >= 0 && at (p) == 0x00) --p;
            p += p % fs;   // C's signed remainder, as the original
        }
        int64_t startChan = p + 2 * fs;

        if (endChan < startChan) { startChan = caret; endChan = caret; }
        if (startChan > caret + fs) startChan = caret;
        if (startChan == fs && size >= fs && silentFrameAt0 (buf, static_cast<size_t> (fs))) startChan = 0;

        selSt = std::max (selSt, startChan);
        selEnd = std::min (selEnd, endChan);
    }

    if (selSt == caret && selEnd == caret) return false;
    doc.selSt = static_cast<size_t> (selSt);
    doc.selEnd = static_cast<size_t> (selEnd);
    return true;
}

} // namespace se
