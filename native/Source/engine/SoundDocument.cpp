#include "SoundDocument.h"

#include <algorithm>

namespace se {

SoundDocument::SoundDocument (const DocumentInit& init)
    : name (init.name),
      rateHz (init.rateHz),
      bps (init.bps),
      baseFreq (init.baseFreq)
{
    tracks.reserve (static_cast<size_t> (std::max (0, init.numTracks)));
    for (int i = 0; i < init.numTracks; ++i)
        tracks.push_back (newTrack (init.sizeBytes));

    lastSelTrack = std::max (1, init.numTracks);
}

Track SoundDocument::newTrack (size_t sizeBytes) const
{
    Track t;
    t.data.assign (sizeBytes, silentByte (bps));
    return t;
}

size_t SoundDocument::maxTrackSize() const noexcept
{
    size_t maxSize = 0;
    for (const auto& t : tracks)
        maxSize = std::max (maxSize, t.data.size());
    return maxSize;
}

size_t SoundDocument::maxSelectedTrackSize() const noexcept
{
    size_t maxSize = 0;
    for (int idx : selectedTrackIndices())
        maxSize = std::max (maxSize, tracks[static_cast<size_t> (idx)].data.size());
    return maxSize;
}

std::vector<int> SoundDocument::selectedTrackIndices() const
{
    std::vector<int> out;
    for (int i = firstSelTrack - 1; i <= lastSelTrack - 1; ++i)
        if (i >= 0 && i < numTracks())
            out.push_back (i);
    return out;
}

Track& SoundDocument::addTrack (int where, size_t sizeBytes)
{
    if (where < 0)
        where = numTracks() + 1;   // append by default

    const int idx = std::clamp (where - 1, 0, numTracks());
    tracks.insert (tracks.begin() + idx, newTrack (sizeBytes));
    clampTrackSelection();
    return tracks[static_cast<size_t> (idx)];
}

bool SoundDocument::delTrack (int track)
{
    // Refuses to delete the last track: "I'm sorry, but I can't delete the only
    // channel of a mono sound. Delete the document instead." (§3). The refusal is
    // the caller's to voice; we just decline.
    if (numTracks() <= 1)               return false;
    if (track < 1 || track > numTracks()) return false;

    tracks.erase (tracks.begin() + (track - 1));

    // Drop chunks on the deleted track and renumber those above it.
    chunks.erase (std::remove_if (chunks.begin(), chunks.end(),
                                  [track] (const Chunk& c) { return c.track == track; }),
                  chunks.end());
    for (auto& c : chunks)
        if (c.track > track) --c.track;

    clampTrackSelection();
    return true;
}

void SoundDocument::swapTracks (int a, int b)
{
    if (a < 1 || b < 1 || a > numTracks() || b > numTracks()) return;
    std::swap (tracks[static_cast<size_t> (a - 1)],
               tracks[static_cast<size_t> (b - 1)]);
}

void SoundDocument::clampTrackSelection()
{
    firstSelTrack = std::max (1, std::min (firstSelTrack, numTracks()));
    lastSelTrack  = std::max (firstSelTrack, std::min (lastSelTrack, numTracks()));
}

SoundDocument SoundDocument::clone() const
{
    DocumentInit init;
    init.name = name;
    init.rateHz = rateHz;
    init.bps = bps;
    init.baseFreq = baseFreq;
    init.numTracks = 0;

    SoundDocument doc (init);
    doc.tracks = tracks;   // deep copy — clipboard holds real sound (§5)
    doc.selSt = selSt;
    doc.selEnd = selEnd;
    doc.firstSelTrack = firstSelTrack;
    doc.lastSelTrack = lastSelTrack;
    doc.loopSt = loopSt;
    doc.loopEnd = loopEnd;
    doc.chunks = chunks;
    return doc;
}

} // namespace se
