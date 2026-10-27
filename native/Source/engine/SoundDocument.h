// SoundDocument — the sound in one Window, SPEC §2.1. Ported from
// `src/document.ts`.
//
// Mutable and destructive by design: there is no undo here and there must never
// be one (§1). Every edit rewrites buffers in place. Tracks are independent
// (see Track.h).
//
// Selection is a single rectangle: a contiguous Track range [firstSelTrack,
// lastSelTrack] × a time range [selSt, selEnd). Not per-track.

#pragma once

#include "SampleFormat.h"
#include "Track.h"

#include <cstdint>
#include <string>
#include <vector>

namespace se {

/** A named region. Kinds seen in the app: "Chunk" and "Clipping". */
struct Chunk
{
    std::string name;
    size_t start = 0;
    size_t length = 0;
    int    track = 1;   // 1-based, matching the app's track numbering
    enum class Kind { Chunk, Clipping } kind = Kind::Chunk;
};

struct DocumentInit
{
    std::string name = "untitled sound #1";
    double rateHz = 22254.5454;   // NB: TS keeps this as 16.16 Fixed; file formats
                                  // will need the exact fixed representation.
    int    bps = 8;
    int    baseFreq = 60;
    int    numTracks = 1;
    size_t sizeBytes = 0;         // initial length of each track, in bytes
};

/** What File > New Sound (and the Sound a fresh canvas opens with) makes: one empty
    Track at 44.1 kHz, 16 bit (0.4.0 spec, File menu note 1). DocumentInit's own
    defaults stay the 0.9.2 ones (22254.5454 Hz, 8 bit) for the effects and fixtures
    that build documents without saying. The colour is not chosen here: the canvas
    draws a random theme nobody on the board is wearing when it first sees a Window. */
inline DocumentInit newSoundInit()
{
    DocumentInit init;
    init.name = "untitled sound";
    init.rateHz = 44100.0;
    init.bps = 16;
    init.numTracks = 1;
    return init;
}

class SoundDocument
{
public:
    explicit SoundDocument (const DocumentInit& init = {});

    // --- header state -------------------------------------------------------
    std::string name;
    double rateHz;      // the DECLARED rate. Obeyed even when it lies (§5).
    int    bps;         // bits per sample, 1..32. Lying about this is a feature.
    int    baseFreq;    // MIDI base note for playback pitch
    std::vector<Track> tracks;

    // --- selection: one rectangle. byte offsets frame-aligned, half-open ------
    size_t selSt = 0;
    size_t selEnd = 0;
    int    firstSelTrack = 1;   // 1-based, contiguous
    int    lastSelTrack  = 1;

    // --- Set Loop region, byte offsets --------------------------------------
    size_t loopSt = 0;
    size_t loopEnd = 0;

    std::vector<Chunk> chunks;

    // --- derived ------------------------------------------------------------
    int frameSize() const noexcept { return frameSizeFor (bps); }
    int numTracks() const noexcept { return static_cast<int> (tracks.size()); }

    /** ModMaxChSize — max size over ALL tracks. */
    size_t maxTrackSize() const noexcept;
    /** ModMaxRelChSize — max size over the SELECTED tracks. */
    size_t maxSelectedTrackSize() const noexcept;

    std::vector<int> selectedTrackIndices() const;

    // --- track operations (1-based, matching the app) -----------------------
    /** ModAddNewChannel — `where` is a 1-based insert position. Defaults to append. */
    Track& addTrack (int where = -1, size_t sizeBytes = 0);
    /** ModDelChannel — refuses to delete the only track of a mono sound. */
    bool delTrack (int track);
    /** Drag one track icon onto another → swap positions (§3). 1-based. */
    void swapTracks (int a, int b);

    SoundDocument clone() const;

private:
    Track newTrack (size_t sizeBytes) const;
    void clampTrackSelection();
};

} // namespace se
