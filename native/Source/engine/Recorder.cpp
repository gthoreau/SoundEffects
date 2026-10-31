#include "Recorder.h"

#include "SampleFormat.h"

#include <algorithm>

namespace se {

bool Recorder::canRecord (const SoundDocument& doc) noexcept
{
    if (doc.selSt == doc.selEnd) return true;                 // no selection: new tracks
    return doc.lastSelTrack - doc.firstSelTrack + 1 <= 2;
}

uint64_t Recorder::mask() const noexcept
{
    uint64_t m = 0;
    for (int t : targets) if (t >= 0 && t < 64) m |= uint64_t (1) << t;
    return m;
}

bool Recorder::start (uint64_t window, const RecordSetup& s)
{
    if (st != State::idle) return false;
    SoundDocument* doc = engine.editDoc (window);
    if (doc == nullptr || ! canRecord (*doc)) return false;

    setup = s;
    win = window;
    targets.clear();
    written = cursor = 0;
    dirtyFrom = static_cast<size_t> (-1); dirtyTo = 0;
    punchedOut = false;
    sumToMono = false;
    const size_t fs = static_cast<size_t> (doc->frameSize());

    if (doc->selSt == doc->selEnd)
    {
        where = Where::newTracks;
        const int need = setup.incomingStereo ? 2 : 1;

        // Empty tracks first: a Sound that has only blank lanes records into them.
        bool allEmpty = ! doc->tracks.empty();
        for (const auto& t : doc->tracks) if (! t.data.empty()) allEmpty = false;
        if (allEmpty)
            for (int i = 0; i < doc->numTracks() && static_cast<int> (targets.size()) < need; ++i)
                targets.push_back (i);
        while (static_cast<int> (targets.size()) < need)
        {
            doc->addTrack();
            targets.push_back (doc->numTracks() - 1);
        }
        for (int t : targets) doc->tracks[static_cast<size_t> (t)].data.clear();

        // Two tracks from a stereo source are the two output channels already.
        if (need == 2)
        {
            auto& a = doc->tracks[static_cast<size_t> (targets[0])];
            auto& b = doc->tracks[static_cast<size_t> (targets[1])];
            a.outLeft = true;  a.outRight = false;
            b.outLeft = false; b.outRight = true;
        }
    }
    else
    {
        where = Where::selection;
        for (int t = doc->firstSelTrack; t <= doc->lastSelTrack; ++t) targets.push_back (t - 1);
        sumToMono = targets.size() == 1;
        startFrame = doc->selSt / fs;
        spanFrames = (doc->selEnd - doc->selSt) / fs;
        // A track shorter than the selection's end is padded with silence so there
        // is room to write.
        for (int t : targets)
        {
            auto& d = doc->tracks[static_cast<size_t> (t)].data;
            if (d.size() < doc->selEnd) d.resize (doc->selEnd, silentByte (doc->bps));
        }
    }

    conv = std::make_unique<RecordConverter> (engine.sampleRate(), doc->rateHz, doc->bps, setup.mode);

    CaptureSetup cs = setup.source;
    if (cs.kind == AudioSourceKind::canvas && cs.tapWindow == 0) cs.excludeWindow = window;   // the Window it records into is left out of the mix
    PlayOptions play;
    play.loop = setup.loop && where == Where::selection;

    engine.captureRing().discard();
    engine.captureRing().resetDropped();
    // Publish the (still empty) new tracks first, so the transport's document has
    // them excluded rather than missing; then capture and play begin together.
    engine.commitEdit (window);
    if (! engine.recordStart (window, cs, play, mask())) return false;

    st = State::recording;
    if (onChanged) onChanged (win);
    return true;
}

void Recorder::pause()
{
    if (st != State::recording) return;
    engine.recordPause (win);
    st = State::finishing; after = State::paused; waitTicks = 0;
}

void Recorder::resume()
{
    if (st != State::paused) return;
    // The tracks written so far were published at the pause; capture and play carry on.
    PlayOptions play;
    play.loop = setup.loop && where == Where::selection;
    engine.captureRing().discard();
    if (! engine.recordResume (win, play, mask())) return;
    st = State::recording;
}

void Recorder::stop()
{
    if (st == State::idle || (st == State::finishing && after == State::idle)) return;
    if (st == State::finishing)
    {
        // Pausing when the stop came in: the pause settles, then the take ends.
        engine.recordStop (win);
        after = State::idle;
        return;
    }
    if (st == State::paused)
    {
        // Nothing is capturing: the take was published at the pause. Just end it.
        engine.recordStop (win);
        st = State::idle;
        if (onFinished) onFinished (win);
        return;
    }
    engine.recordStop (win);
    st = State::finishing; after = State::idle; waitTicks = 0;
}

void Recorder::write (const std::vector<int32_t>& l, const std::vector<int32_t>& r)
{
    SoundDocument* doc = engine.editDoc (win);
    if (doc == nullptr || l.empty()) return;
    const size_t fs = static_cast<size_t> (doc->frameSize());
    const int bps = doc->bps;

    for (size_t i = 0; i < l.size(); ++i)
    {
        size_t frame;
        if (where == Where::newTracks)
            frame = cursor;
        else
        {
            if (! setup.loop && cursor >= spanFrames) { punchedOut = true; break; }   // punch-out
            frame = startFrame + (cursor % std::max<size_t> (1, spanFrames));
        }

        for (size_t k = 0; k < targets.size() && k < 2; ++k)
        {
            auto& d = doc->tracks[static_cast<size_t> (targets[k])].data;
            if (d.size() < (frame + 1) * fs) d.resize ((frame + 1) * fs, silentByte (bps));
            setSample (d.data(), frame * fs, k == 0 ? l[i] : r[i], bps);
        }
        dirtyFrom = std::min (dirtyFrom, frame);
        dirtyTo = std::max (dirtyTo, frame + 1);
        ++cursor; ++written;
    }
}

void Recorder::drain (bool all)
{
    constexpr size_t kChunk = 4096;
    bufL.resize (kChunk); bufR.resize (kChunk);
    // A tick takes at most this many chunks (~1.4 s at 48 kHz), so a source that
    // outruns the converter cannot hold the message thread; the ring keeps the rest.
    // Finishing (`all`) drains whatever is left, which is finite: capture has stopped.
    for (int chunks = 0; all || chunks < 16; ++chunks)
    {
        const size_t n = engine.captureRing().pop (bufL.data(), bufR.data(), kChunk);
        if (n == 0) break;
        if (sumToMono)
            for (size_t i = 0; i < n; ++i) bufL[i] = bufR[i] = 0.5f * (bufL[i] + bufR[i]);

        outL.clear(); outR.clear();
        conv->process (bufL.data(), bufR.data(), n, outL, outR);
        write (outL, outR);
        if (! all && n < kChunk) break;
    }
}

void Recorder::settle()
{
    drain (true);
    outL.clear(); outR.clear();
    conv->flush (outL, outR);
    write (outL, outR);
    engine.commitEdit (win);
    if (onChanged) onChanged (win);
}

void Recorder::service()
{
    switch (st)
    {
        case State::idle:
        case State::paused:
            return;

        case State::recording:
            drain (false);
            if (onChanged) onChanged (win);
            if (punchedOut)
            {
                // The selection's end: the take is over. Same road as a Stop.
                engine.recordStop (win);
                st = State::finishing; after = State::idle; waitTicks = 0;
            }
            return;

        case State::finishing:
            // Wait for the audio thread to say it has stopped capturing; if it never
            // does (no device running), give up after a second.
            if (engine.captureRunning() && ++waitTicks < 30) { drain (false); return; }
            settle();
            st = after;
            if (st == State::idle && onFinished) onFinished (win);
            return;
    }
}

} // namespace se
