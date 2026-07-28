#include "Host.h"

#include "Selection.h"
#include "engine/Edit.h"
#include "engine/Resample.h"
#include "engine/SampleFormat.h"

#include <algorithm>
#include <cmath>

namespace se {

Host::Host (HostOptions opts)
    : random (opts.randSeed),
      progressInterval (opts.progressInterval < 1 ? 1 : opts.progressInterval),
      askDoExtendFn (std::move (opts.askDoExtend)),
      onProgress (std::move (opts.onProgress))
{
}

// --- progress ---------------------------------------------------------------

void Host::setupProgress (const std::string& text)
{
    progressText = text;
    progressOpen = true;
    sinceLastProgress = 0;
    timeToCallProgress = true;
}

ModResult Host::showProgress (size_t cur, size_t total, bool canCancel)
{
    sinceLastProgress = 0;
    timeToCallProgress = false;

    const bool wantsCancel = onProgress ? onProgress (cur, total, progressText) : false;

    // Re-arm on the host's own schedule.
    if (++sinceLastProgress >= progressInterval)
        timeToCallProgress = true;

    if (wantsCancel && canCancel) return 1;
    return kModNoError;
}

void Host::closeProgress()
{
    progressOpen = false;
    timeToCallProgress = false;
}

// --- settings ---------------------------------------------------------------

const Settings* Host::loadEffectSettings (const std::string& signature) const
{
    const auto it = prefsStore.find (signature);
    return it == prefsStore.end() ? nullptr : &it->second;
}

void Host::saveEffectSettings (const std::string& signature, Settings s)
{
    prefsStore[signature] = std::move (s);
}

// --- environment ------------------------------------------------------------

bool Host::askDoExtend()
{
    if (doExtend == DoExtend::Ask)
        return askDoExtendFn ? askDoExtendFn() : false;
    return doExtend == DoExtend::Always;
}

// --- selection queries ------------------------------------------------------

size_t Host::getBytesToProcess (const SoundDocument& doc) const
{
    const ByteRange sel = resolveSelection (doc);
    size_t total = 0;
    for (int i : doc.selectedTrackIndices())
    {
        const ByteRange r = trackRange (doc, i, sel.start, sel.end);
        if (r.end > r.start) total += r.end - r.start;
    }
    return total;
}

Host::Peak Host::getPeak (const SoundDocument& doc) const
{
    const size_t frameSize = static_cast<size_t> (doc.frameSize());
    const ByteRange sel = resolveSelection (doc);

    Peak out;
    for (int i : doc.selectedTrackIndices())
    {
        const auto& buf = doc.tracks[static_cast<size_t> (i)].data;
        const size_t hi = std::min (sel.end, buf.size() - (buf.size() % frameSize));
        for (size_t p = sel.start; p + frameSize <= hi; p += frameSize)
        {
            const int64_t v = std::abs (static_cast<int64_t> (getSample (buf.data(), p, doc.bps)));
            if (v > out.peak)
            {
                out.peak = v;
                out.pos = p;
            }
        }
    }
    return out;
}

Host::Limits Host::getSampleValueLimits (int bps) noexcept
{
    // §2.2: the value occupies the full range of its BYTE CONTAINER, so a 12-bit
    // sound's samples come back as 16-bit values. The exact behaviour for odd bps
    // is still [ASM]; this is the byte-container reading, and the one place to fix
    // if the app-level routine is ever disassembled and disagrees.
    const int bits = valueBytes (bps) * 8;
    const int64_t magnitude = int64_t (1) << (bits - 1);
    return { -magnitude, magnitude - 1 };
}

// --- document restructuring -------------------------------------------------

ModResult Host::addNewTrack (SoundDocument& doc, int where)
{
    doc.addTrack (where);
    return kModNoError;
}

ModResult Host::delTrack (SoundDocument& doc, int track)
{
    return doc.delTrack (track) ? kModNoError : kModGenericError;
}

bool Host::getTrackPan (const SoundDocument& doc, int track, bool leftOne) const
{
    if (track < 0 || track >= doc.numTracks()) return false;
    const auto& t = doc.tracks[static_cast<size_t> (track)];
    return leftOne ? t.outLeft : t.outRight;
}

void Host::setTrackPan (SoundDocument& doc, int track, bool leftPan, bool rightPan)
{
    if (track < 0 || track >= doc.numTracks()) return;
    auto& t = doc.tracks[static_cast<size_t> (track)];
    t.outLeft = leftPan;
    t.outRight = rightPan;
}

ModResult Host::silenceOut (SoundDocument& doc, int trackSt, int trackEnd, size_t st, size_t end)
{
    const uint8_t silent = silentByte (doc.bps);
    for (int t = trackSt; t <= trackEnd; ++t)
    {
        if (t < 1 || t > doc.numTracks()) continue;
        auto& data = doc.tracks[static_cast<size_t> (t - 1)].data;
        const size_t hi = std::min (end, data.size());
        for (size_t p = std::min (st, data.size()); p < hi; ++p)
            data[p] = silent;
    }
    return kModNoError;
}

ModResult Host::resampleSoundBits (SoundDocument& doc, int newBps)
{
    se::resampleSoundBits (doc, newBps);
    return kModNoError;
}

ModResult Host::resampleSoundRate (SoundDocument& doc, double newRateHz)
{
    se::resampleSoundRate (doc, newRateHz);
    return kModNoError;
}

SoundDocument& Host::newDocument (const SoundDocument& like)
{
    DocumentInit init;
    init.name = "untitled sound";
    init.rateHz = like.rateHz;
    init.bps = like.bps;
    init.baseFreq = like.baseFreq;
    init.numTracks = 0;

    spawnedDocuments.push_back (std::make_unique<SoundDocument> (init));
    return *spawnedDocuments.back();
}

} // namespace se
