#include "Tape.h"

#include "SampleFormat.h"

#include <algorithm>

namespace se {

// ================================ Tape =======================================

size_t Tape::takeStart (int i) const noexcept
{
    return i >= 0 && i < numTakes() ? list[(size_t) i].start : 0;
}

size_t Tape::takeEnd (int i) const noexcept
{
    if (i < 0 || i >= numTakes()) return 0;
    return i + 1 < numTakes() ? list[(size_t) i + 1].start : len;
}

int Tape::takeAt (size_t frame) const noexcept
{
    int found = -1;
    for (int i = 0; i < numTakes(); ++i)
    {
        if (list[(size_t) i].start <= frame) found = i;
        else break;
    }
    return found;
}

bool Tape::hasUnsent() const noexcept
{
    for (int i = 0; i < numTakes(); ++i)
        if (! list[(size_t) i].sent && takeEnd (i) > takeStart (i)) return true;
    return false;
}

bool Tape::record (double deviceRate)
{
    // Record while recording starts a new take where the head is, seamlessly (none
    // when nothing has been recorded in this one yet).
    if (md == Mode::recording && pos == recStart) return false;
    if (len > 0 && ! (deviceRate <= tapeRate && deviceRate >= tapeRate)) return false;
    if (len == 0) tapeRate = deviceRate;

    md = Mode::recording;
    recStart = pos;

    // The take being recorded over (if any) lends its "sent" flag to whatever of it
    // survives after the new take; a marker exactly here is replaced, and put back
    // should nothing be recorded.
    const int under = takeAt (pos);
    tailSent = under >= 0 && list[(size_t) under].sent;
    replaced = under >= 0 && list[(size_t) under].start == pos;
    replacedSent = replaced && list[(size_t) under].sent;
    if (replaced) list.erase (list.begin() + under);

    auto at = std::find_if (list.begin(), list.end(), [this] (const Take& t) { return t.start > pos; });
    list.insert (at, Take { pos, false });
    return true;
}

void Tape::recorded (size_t n)
{
    if (md != Mode::recording || n == 0) return;
    pos += n;
    len = std::max (len, pos);

    // Markers the head has run over go; the last of them owned the audio after it.
    for (auto it = list.begin(); it != list.end();)
    {
        if (it->start > recStart && it->start < pos) { tailSent = it->sent; it = list.erase (it); }
        else ++it;
    }
}

bool Tape::stop()
{
    if (md == Mode::stopped) return false;

    if (md == Mode::recording)
    {
        const int mine = takeAt (recStart);
        if (pos == recStart)
        {
            // Nothing was recorded: as if Record had not been pressed.
            if (mine >= 0) list.erase (list.begin() + mine);
            if (replaced)
            {
                auto at = std::find_if (list.begin(), list.end(), [this] (const Take& t) { return t.start > pos; });
                list.insert (at, Take { pos, replacedSent });
            }
        }
        else if (pos < len && takeAt (pos) == mine)
        {
            // Stopped inside older audio: its tail becomes a take of its own.
            auto at = std::find_if (list.begin(), list.end(), [this] (const Take& t) { return t.start > pos; });
            list.insert (at, Take { pos, tailSent });
        }
    }

    md = Mode::stopped;
    return true;
}

bool Tape::play()
{
    if (md != Mode::stopped || len == 0) return false;
    if (pos >= len) pos = takeStart (numTakes() - 1);
    md = Mode::playing;
    return true;
}

void Tape::played (size_t frame)
{
    if (md != Mode::playing) return;
    pos = std::min (frame, len);
    if (pos >= len) md = Mode::stopped;
}

bool Tape::nextTake()
{
    if (md == Mode::recording || len == 0) return false;
    size_t to = len;
    for (const auto& t : list)
        if (t.start > pos) { to = t.start; break; }
    if (to == pos) return false;
    pos = to;
    if (md == Mode::playing && pos >= len) md = Mode::stopped;
    return true;
}

bool Tape::previousTake (size_t graceFrames)
{
    if (md == Mode::recording) return false;
    const int i = takeAt (pos);
    if (i < 0) return false;
    size_t to = takeStart (i);
    if (pos <= to + graceFrames && i > 0) to = takeStart (i - 1);
    if (to == pos) return false;
    pos = to;
    return true;
}

bool Tape::seek (size_t frame)
{
    if (md == Mode::recording) return false;
    const size_t to = std::min (frame, len);
    if (to == pos) return false;
    pos = to;
    if (md == Mode::playing && pos >= len) md = Mode::stopped;
    return true;
}

void Tape::clear()
{
    md = Mode::stopped;
    len = pos = recStart = 0;
    list.clear();
    tapeRate = 0.0;
    tailSent = replaced = replacedSent = false;
}

// ============================== TapeDeck ======================================

TapeDeck::~TapeDeck()
{
    if (model.mode() == Tape::Mode::recording || stopping) engine.tapeCaptureStop();
    if (model.mode() == Tape::Mode::playing) engine.tapeStop (++gen);
}

bool TapeDeck::setSource (const TapeSource& s)
{
    if (model.mode() == Tape::Mode::recording || stopping) return false;
    src = s;
    return true;
}

void TapeDeck::followPlayback()
{
    if (model.mode() != Tape::Mode::playing || engine.tapeAckGen() != gen) return;
    model.played (engine.tapePlayFrame());
    if (model.mode() == Tape::Mode::playing && ! engine.tapePlaying()) model.stop();
}

bool TapeDeck::record()
{
    if (stopping || clearPending) return false;
    if (model.mode() == Tape::Mode::recording) return model.record (engine.sampleRate());   // a new take, no gap
    if (model.mode() == Tape::Mode::playing)
    {
        followPlayback();
        engine.tapeStop (++gen);
        model.stop();
    }
    if (model.length() >= TapeStore::kMaxFrames && model.position() >= model.length()) return false;
    if (! model.record (engine.sampleRate())) return false;

    engine.tapeRing().discard();
    engine.tapeRing().resetDropped();
    if (! engine.tapeCaptureStart (src)) { model.stop(); return false; }
    return true;
}

bool TapeDeck::stop()
{
    if (model.mode() == Tape::Mode::recording)
    {
        if (! stopping) { engine.tapeCaptureStop(); stopping = true; }
        return true;
    }
    if (model.mode() == Tape::Mode::playing)
    {
        followPlayback();
        engine.tapeStop (++gen);
        model.stop();
        return true;
    }
    return false;
}

bool TapeDeck::play()
{
    if (stopping || clearPending) return false;
    if (! model.play()) return false;
    if (! engine.tapePlay (model.position(), ++gen)) { model.stop(); return false; }
    return true;
}

bool TapeDeck::jumped (bool wasPlaying)
{
    if (model.mode() == Tape::Mode::playing) engine.tapePlay (model.position(), ++gen);
    else if (wasPlaying) engine.tapeStop (++gen);
    return true;
}

bool TapeDeck::nextTake()
{
    if (stopping) return false;
    const bool wasPlaying = model.mode() == Tape::Mode::playing;
    followPlayback();
    if (! model.nextTake()) return false;
    return jumped (wasPlaying);
}

bool TapeDeck::previousTake()
{
    if (stopping) return false;
    const bool wasPlaying = model.mode() == Tape::Mode::playing;
    followPlayback();
    const double rate = model.rate() > 0.0 ? model.rate() : engine.sampleRate();
    if (! model.previousTake (static_cast<size_t> (rate * kPreviousGraceSeconds))) return false;
    return jumped (wasPlaying);
}

bool TapeDeck::seek (size_t frame)
{
    if (stopping) return false;
    const bool wasPlaying = model.mode() == Tape::Mode::playing;
    followPlayback();
    if (! model.seek (frame)) return false;
    return jumped (wasPlaying);
}

void TapeDeck::clear()
{
    if (model.mode() == Tape::Mode::recording || stopping)
    {
        engine.tapeCaptureStop();
        stopping = true;
        discardOnSettle = true;
    }
    if (model.mode() == Tape::Mode::playing) engine.tapeStop (++gen);
    model.clear();
    dirtyFrom = static_cast<size_t> (-1); dirtyTo = 0;
    clearPending = true;
    service();
}

void TapeDeck::service()
{
    if (clearPending && ! engine.tapePlaying() && engine.tapeAckGen() == gen)
    {
        // The audio thread has let go of the tape: its memory can go.
        engine.tapeStore().release();
        clearPending = false;
    }

    if (model.mode() == Tape::Mode::recording || stopping)
    {
        if (! discardOnSettle) drain();
        if (stopping && ! engine.tapeCaptureRunning())
        {
            if (discardOnSettle) engine.tapeRing().discard();
            else drain();
            model.stop();
            stopping = false;
            discardOnSettle = false;
        }
    }

    followPlayback();
}

void TapeDeck::drain()
{
    constexpr size_t kBatch = 4096;
    if (bufL.size() < kBatch) { bufL.resize (kBatch); bufR.resize (kBatch); }

    auto& store = engine.tapeStore();
    for (;;)
    {
        if (model.mode() != Tape::Mode::recording) { engine.tapeRing().discard(); return; }

        size_t n = engine.tapeRing().pop (bufL.data(), bufR.data(), kBatch);
        if (n == 0) return;

        const size_t at = model.position();
        if (at + n > TapeStore::kMaxFrames) n = TapeStore::kMaxFrames - at;
        if (n == 0 || ! store.reserve (at + n))
        {
            // The tape is full: the take ends here.
            if (! stopping) { engine.tapeCaptureStop(); stopping = true; }
            engine.tapeRing().discard();
            return;
        }
        store.write (at, bufL.data(), bufR.data(), n);
        model.recorded (n);
        store.setLength (model.length());
        dirtyFrom = std::min (dirtyFrom, at);
        dirtyTo = std::max (dirtyTo, at + n);
    }
}

// ============================ Send to canvas ===================================

std::unique_ptr<SoundDocument> soundFromTape (const TapeStore& store, size_t from, size_t to, double rate,
                                              const std::string& name, ConvertMode mode)
{
    to = std::min (to, store.length());
    if (from >= to || rate <= 0.0) return nullptr;

    DocumentInit init;
    init.name = name;
    init.rateHz = rate;
    init.bps = 16;
    init.numTracks = 2;
    init.sizeBytes = 0;
    auto doc = std::make_unique<SoundDocument> (init);
    doc->tracks[0].outLeft = true;  doc->tracks[0].outRight = false;
    doc->tracks[1].outLeft = false; doc->tracks[1].outRight = true;

    RecordConverter conv (rate, rate, 16, mode);
    std::vector<int32_t> outL, outR;
    outL.reserve (to - from); outR.reserve (to - from);
    constexpr size_t kBatch = 8192;
    std::vector<float> l (kBatch), r (kBatch);
    for (size_t f = from; f < to;)
    {
        const size_t n = store.read (f, l.data(), r.data(), std::min (kBatch, to - f));
        if (n == 0) break;
        conv.process (l.data(), r.data(), n, outL, outR);
        f += n;
    }
    conv.flush (outL, outR);

    const size_t frames = std::min ({ outL.size(), outR.size(), to - from });
    const size_t fs = (size_t) frameSizeFor (16);
    doc->tracks[0].data.assign (frames * fs, 0);
    doc->tracks[1].data.assign (frames * fs, 0);
    for (size_t i = 0; i < frames; ++i)
    {
        setSample (doc->tracks[0].data.data(), i * fs, outL[i], 16);
        setSample (doc->tracks[1].data.data(), i * fs, outR[i], 16);
    }
    return doc;
}

} // namespace se
