#include "Engine.h"

#include <algorithm>

namespace se {

Engine::~Engine()
{
    // Audio thread assumed stopped. Reclaim every Window/doc reachable from any of
    // the queues or the live set, each exactly once.
    Command c;
    while (incoming.pop (c))
    {
        if (c.kind == Command::Kind::AddWindow) delete c.window;  // deletes its audioDoc
        else if (c.kind == Command::Kind::SwapDoc) delete c.doc;
    }

    for (Window* w : live) delete w;
    live.clear();

    Window* w = nullptr;
    while (retiredWins.pop (w)) delete w;

    SoundDocument* d = nullptr;
    while (retiredDocs.pop (d)) delete d;
}

void Engine::prepare (double rate, int maxBlockSize)
{
    deviceRate = rate;
    scratchL.assign (static_cast<size_t> (std::max (1, maxBlockSize)), 0.0f);
    scratchR.assign (static_cast<size_t> (std::max (1, maxBlockSize)), 0.0f);
    for (auto* v : { &capL, &capR, &tapL, &tapR, &exL, &exR, &pexL, &pexR, &tcapL, &tcapR, &ttapL, &ttapR, &tplayL, &tplayR })
        v->assign (static_cast<size_t> (std::max (1, maxBlockSize)), 0.0f);
    // Room for any fade-out the tape is asked for (kDeclickMs, with room to tune it).
    for (auto* v : { &tfadeL, &tfadeR, &tsnapL, &tsnapR })
        v->assign (static_cast<size_t> (declickFrames (rate, 50.0)), 0.0f);
    tfadeAt = tfadeEnd = 0;
    live.reserve (kMaxWindows);
    scrubVelCoeff  = 1.0 - std::exp (-1.0 / (rate * 0.010));   // the tape's speed settles in ~10 ms
    scrubGainCoeff = 1.0 - std::exp (-1.0 / (rate * 0.005));   // its level in ~5 ms

    // Pre-audio (prepareToPlay): safe to touch every known transport.
    for (auto& kv : registry)
        if (kv.second->transport) kv.second->transport->setDeviceRate (rate);
}

// ============================ message thread ================================

uint64_t Engine::addWindow (std::unique_ptr<SoundDocument> doc, uint32_t assetId)
{
    if (registry.size() >= kMaxWindows) return 0;

    const uint64_t id = nextId++;

    auto* w = new Window();
    w->id = id;
    w->audioDoc = new SoundDocument (doc->clone());     // audio-side copy
    w->transport = std::make_unique<Transport> (deviceRate, declickMs);
    w->uiDoc = std::move (doc);                          // UI-side authoritative
    w->slot = lowestFreeSlot();                          // a new Sound takes the lowest free slot
    w->assetId = assetId != 0 ? assetId : assetCounter;

    Command c; c.kind = Command::Kind::AddWindow; c.id = id; c.window = w;
    if (! post (c)) { delete w; --nextId; return 0; }

    assetCounter = std::max (assetCounter, w->assetId + 1);

    registry.emplace (id, w);
    order.push_back (id);
    return id;
}

bool Engine::removeWindow (uint64_t id)
{
    auto it = registry.find (id);
    if (it == registry.end()) return false;

    Command c; c.kind = Command::Kind::RemoveWindow; c.id = id;
    if (! post (c)) return false;

    registry.erase (it);
    order.erase (std::remove (order.begin(), order.end(), id), order.end());
    compactSlots();   // the strips below move up: no empty slots
    return true;
}

// --- mixer slots ----------------------------------------------------------------

int Engine::lowestFreeSlot() const
{
    std::vector<bool> taken (registry.size() + 1, false);
    for (const auto& kv : registry)
        if (const int s = kv.second->slot; s >= 0 && s < (int) taken.size())
            taken[(size_t) s] = true;
    return (int) (std::find (taken.begin(), taken.end(), false) - taken.begin());
}

uint64_t Engine::windowInSlot (int slot) const noexcept
{
    for (const auto& kv : registry)
        if (kv.second->slot == slot) return kv.first;
    return 0;
}

int Engine::highestSlot() const noexcept
{
    int top = Window::kNoSlot;
    for (const auto& kv : registry) top = std::max (top, kv.second->slot);
    return top;
}

std::vector<Window*> Engine::seatedInSlotOrder() const
{
    std::vector<Window*> seated;
    for (uint64_t id : order)
        if (auto* w = registry.at (id); w->slot >= 0) seated.push_back (w);
    std::stable_sort (seated.begin(), seated.end(), [] (const Window* a, const Window* b) { return a->slot < b->slot; });
    return seated;
}

void Engine::compactSlots()
{
    int next = 0;
    for (auto* w : seatedInSlotOrder()) w->slot = next++;
}

bool Engine::moveToSlot (uint64_t id, int slot)
{
    auto* w = windowById (id);
    if (w == nullptr || slot < 0) return false;

    // An insert, not a swap: the Window leaves its place, the others close up, and
    // it goes in at `slot` (past the end = last), pushing the rest down.
    auto seated = seatedInSlotOrder();
    seated.erase (std::remove (seated.begin(), seated.end(), w), seated.end());
    seated.insert (seated.begin() + static_cast<std::ptrdiff_t> (std::min ((size_t) slot, seated.size())), w);
    int next = 0;
    for (auto* s : seated) s->slot = next++;
    return true;
}

void Engine::normaliseSlots()
{
    // First come, first served, in creation order: a Window keeps its slot unless
    // an earlier one already holds it; the losers and the unseated take the lowest
    // free slots, in order.
    std::vector<int> seen;
    std::vector<Window*> unseated;
    for (uint64_t id : order)
    {
        auto* w = registry.at (id);
        if (w->slot < 0 || std::find (seen.begin(), seen.end(), w->slot) != seen.end())
            unseated.push_back (w);
        else
            seen.push_back (w->slot);
    }
    for (auto* w : unseated) { w->slot = Window::kNoSlot; w->slot = lowestFreeSlot(); }
    compactSlots();
}

bool Engine::anySoloed() const noexcept
{
    if (tapeSoloed()) return true;
    for (const auto& kv : registry)
        if (kv.second->soloed.load (std::memory_order_relaxed)) return true;
    return false;
}

void Engine::unmuteAll() noexcept
{
    for (const auto& kv : registry) kv.second->muted.store (false, std::memory_order_relaxed);
}

void Engine::clearAllSolos() noexcept
{
    setTapeSoloed (false);
    for (const auto& kv : registry) kv.second->soloed.store (false, std::memory_order_relaxed);
}

void Engine::clickSolo (uint64_t id, bool command, const std::vector<uint64_t>& group) noexcept
{
    auto* w = windowById (id);
    if (w == nullptr) return;
    const bool lit = w->soloed.load (std::memory_order_relaxed);

    if (command && lit) { clearAllSolos(); return; }

    const bool on = command || ! lit;
    const auto inGroup = [&group, id] (uint64_t other)
    { return other == id || std::find (group.begin(), group.end(), other) != group.end(); };

    // The new solos go on BEFORE the others go off, so no audio block can see the
    // filter off and let every Window through.
    for (const auto& kv : registry)
        if (inGroup (kv.first)) kv.second->soloed.store (on, std::memory_order_relaxed);
    if (! command) return;
    for (const auto& kv : registry)
        if (! inGroup (kv.first)) kv.second->soloed.store (false, std::memory_order_relaxed);
    setTapeSoloed (false);
}

void Engine::clickMute (uint64_t id, bool command, const std::vector<uint64_t>& group) noexcept
{
    auto* w = windowById (id);
    if (w == nullptr) return;
    const bool lit = w->muted.load (std::memory_order_relaxed);

    if (command && lit) { unmuteAll(); return; }

    w->muted.store (! lit, std::memory_order_relaxed);
    for (const uint64_t other : group)
        if (auto* o = windowById (other)) o->muted.store (! lit, std::memory_order_relaxed);
}

void Engine::sendBatch() noexcept
{
    if (batched.empty()) return;
    if (! incoming.pushAll (batched.data(), batched.size()))
    {
        // The queue is full (the audio thread is not running): none of it goes.
        for (const Command& c : batched)
        {
            if (c.kind == Command::Kind::SwapDoc) delete c.doc;
            if (c.kind == Command::Kind::Transport
                && (c.tx == Command::Tx::Play || c.tx == Command::Tx::Stop || c.tx == Command::Tx::Pause))
                if (auto* w = windowById (c.id)) --w->rollSent;
        }
    }
    batched.clear();
}

void Engine::clickTapeSolo (bool command) noexcept
{
    const bool lit = tapeSoloed();

    if (! command) { setTapeSoloed (! lit); return; }
    if (lit)       { clearAllSolos(); return; }

    setTapeSoloed (true);   // on before the others go off, as in clickSolo()
    for (const auto& kv : registry) kv.second->soloed.store (false, std::memory_order_relaxed);
}

Window* Engine::windowById (uint64_t id) noexcept
{
    auto it = registry.find (id);
    return it == registry.end() ? nullptr : it->second;
}

SoundDocument* Engine::editDoc (uint64_t id) noexcept
{
    auto* w = windowById (id);
    return w ? w->uiDoc.get() : nullptr;
}

bool Engine::commitEdit (uint64_t id)
{
    auto* w = windowById (id);
    if (w == nullptr || ! w->uiDoc) return false;

    auto* fresh = new SoundDocument (w->uiDoc->clone());   // immutable snapshot
    Command c; c.kind = Command::Kind::SwapDoc; c.id = id; c.doc = fresh;
    if (! post (c)) { delete fresh; return false; }
    return true;
}

bool Engine::commitMeta (uint64_t id)
{
    auto* w = windowById (id);
    if (w == nullptr || ! w->uiDoc) return false;
    const SoundDocument& d = *w->uiDoc;
    if (d.numTracks() > kPhaseTracks) return commitEdit (id);   // the masks hold 64 tracks

    Command c; c.kind = Command::Kind::SetMeta; c.id = id;
    c.meta.selSt = d.selSt;   c.meta.selEnd = d.selEnd;
    c.meta.loopSt = d.loopSt; c.meta.loopEnd = d.loopEnd;
    c.meta.firstSelTrack = d.firstSelTrack; c.meta.lastSelTrack = d.lastSelTrack;
    c.meta.numTracks = d.numTracks();
    for (int t = 0; t < d.numTracks(); ++t)
    {
        if (d.tracks[(size_t) t].outLeft)  c.meta.outLeft  |= uint64_t (1) << t;
        if (d.tracks[(size_t) t].outRight) c.meta.outRight |= uint64_t (1) << t;
    }
    return post (c);
}

bool Engine::play (uint64_t id, const PlayOptions& opts)
{ Command c; c.kind=Command::Kind::Transport; c.tx=Command::Tx::Play; c.id=id; c.play=opts; return postRoll (c, TransportState::playing); }
bool Engine::stop (uint64_t id)
{ Command c; c.kind=Command::Kind::Transport; c.tx=Command::Tx::Stop; c.id=id; return postRoll (c, TransportState::stopped); }
bool Engine::pause (uint64_t id)
{
    Command c; c.kind=Command::Kind::Transport; c.tx=Command::Tx::Pause; c.id=id;
    const auto now = rollState (id);   // the command toggles; a stopped one stays so
    return postRoll (c, now == TransportState::playing ? TransportState::paused
                        : now == TransportState::paused ? TransportState::playing : TransportState::stopped);
}
bool Engine::postRoll (Command& c, TransportState leadsTo) noexcept
{
    auto* w = windowById (c.id);
    if (w == nullptr) return post (c);
    c.gen = w->rollSent + 1;
    if (! post (c)) return false;
    w->rollSent = c.gen;
    w->sentState = leadsTo;
    return true;
}
TransportState Engine::rollState (uint64_t id) noexcept
{
    auto* w = windowById (id);
    if (w == nullptr || w->transport == nullptr) return TransportState::stopped;
    if (w->rollApplied.load (std::memory_order_acquire) != w->rollSent) return w->sentState;
    return w->transport->uiState();
}
bool Engine::advance (uint64_t id)
{ Command c; c.kind=Command::Kind::Transport; c.tx=Command::Tx::Advance; c.id=id; return post (c); }
bool Engine::normalSpeed (uint64_t id)
{ Command c; c.kind=Command::Kind::Transport; c.tx=Command::Tx::NormalSpeed; c.id=id; return post (c); }
bool Engine::setRateScale (uint64_t id, double scale)
{ Command c; c.kind=Command::Kind::Transport; c.tx=Command::Tx::RateScale; c.id=id; c.play.rateScale=scale; return post (c); }
bool Engine::seek (uint64_t id, size_t bytes)
{ Command c; c.kind=Command::Kind::Transport; c.tx=Command::Tx::Seek; c.id=id; c.seekBytes=bytes; return post (c); }

bool Engine::setDirection (uint64_t id, PlayDirection d)
{
    auto* w = windowById (id);
    if (w == nullptr) return false;
    Command c; c.kind=Command::Kind::Transport; c.tx=Command::Tx::Direction; c.id=id; c.direction=d;
    if (! post (c)) return false;
    w->direction.store (static_cast<uint8_t> (d), std::memory_order_relaxed);
    return true;
}
bool Engine::recordStart (uint64_t id, const CaptureSetup& setup, const PlayOptions& play, uint64_t mask)
{
    Command c; c.kind=Command::Kind::Transport; c.tx=Command::Tx::RecordStart; c.id=id;
    c.capture=setup; c.play=play; c.excludeMask=mask;
    if (! post (c)) return false;
    if (auto* w = windowById (id)) w->direction.store (static_cast<uint8_t> (PlayDirection::forward), std::memory_order_relaxed);
    return true;
}
bool Engine::recordPause (uint64_t id)
{ Command c; c.kind=Command::Kind::Transport; c.tx=Command::Tx::RecordPause; c.id=id; return post (c); }
bool Engine::recordResume (uint64_t id, const PlayOptions& play, uint64_t mask)
{ Command c; c.kind=Command::Kind::Transport; c.tx=Command::Tx::RecordResume; c.id=id; c.play=play; c.excludeMask=mask; return post (c); }
bool Engine::recordStop (uint64_t id)
{ Command c; c.kind=Command::Kind::Transport; c.tx=Command::Tx::RecordStop; c.id=id; return post (c); }
bool Engine::recordJoin (uint64_t id, const CaptureSetup& setup, const PlayOptions& play, uint64_t mask, int refTrack)
{
    Command c; c.kind=Command::Kind::Transport; c.tx=Command::Tx::RecordJoin; c.id=id;
    c.capture=setup; c.play=play; c.excludeMask=mask; c.refTrack=refTrack;
    if (! post (c)) return false;
    if (auto* w = windowById (id)) w->direction.store (static_cast<uint8_t> (PlayDirection::forward), std::memory_order_relaxed);
    return true;
}
bool Engine::recordEnd (uint64_t id)
{ Command c; c.kind=Command::Kind::Transport; c.tx=Command::Tx::RecordEnd; c.id=id; return post (c); }
bool Engine::recordRelease (uint64_t id)
{ Command c; c.kind=Command::Kind::Transport; c.tx=Command::Tx::RecordRelease; c.id=id; return post (c); }

PlayDirection Engine::direction (uint64_t id) noexcept
{
    auto* w = windowById (id);
    return w != nullptr ? static_cast<PlayDirection> (w->direction.load (std::memory_order_relaxed))
                        : PlayDirection::forward;
}

bool Engine::startCapture (const CaptureSetup& setup)
{
    Command c; c.kind = Command::Kind::CaptureStart; c.capture = setup;
    return post (c);
}
bool Engine::stopCapture()
{
    Command c; c.kind = Command::Kind::CaptureStop;
    return post (c);
}

bool Engine::probeStart (const CaptureSetup& source)
{
    Command c; c.kind = Command::Kind::ProbeStart; c.capture = source;
    return post (c);
}
bool Engine::probeStop()
{
    Command c; c.kind = Command::Kind::ProbeStop;
    return post (c);
}

bool Engine::tapeCaptureStart (const TapeSource& source)
{
    Command c; c.kind = Command::Kind::TapeCaptureStart; c.tape = source;
    return post (c);
}
bool Engine::tapeCaptureStop()
{
    Command c; c.kind = Command::Kind::TapeCaptureStop;
    return post (c);
}
bool Engine::tapePlay (size_t frame, uint32_t gen)
{
    Command c; c.kind = Command::Kind::TapePlay; c.seekBytes = frame; c.gen = gen;
    return post (c);
}
bool Engine::tapeScrub (size_t frame, uint32_t gen)
{
    tapeScrubTo (frame);
    Command c; c.kind = Command::Kind::TapeScrub; c.seekBytes = frame; c.gen = gen;
    return post (c);
}
bool Engine::tapeScrubEnd (uint32_t gen)
{
    Command c; c.kind = Command::Kind::TapeScrubEnd; c.gen = gen;
    return post (c);
}
bool Engine::tapeStop (uint32_t gen)
{
    Command c; c.kind = Command::Kind::TapeStop; c.gen = gen;
    return post (c);
}

void Engine::collectRetired()
{
    Window* w = nullptr;
    while (retiredWins.pop (w)) delete w;
    SoundDocument* d = nullptr;
    while (retiredDocs.pop (d)) delete d;
}

// ============================== audio thread ================================

Window* Engine::liveById (uint64_t id) noexcept
{
    for (Window* w : live) if (w->id == id) return w;
    return nullptr;
}

void Engine::applyTransport (Window& w, const Command& c) noexcept
{
    if (! w.transport || w.audioDoc == nullptr) return;
    const SoundDocument& d = *w.audioDoc;
    switch (c.tx)
    {
        // A plain Play or Stop ends what a take that was disarmed left silent.
        case Command::Tx::Play:        w.transport->excludeMask = 0; w.transport->takeOpen = false; w.transport->play (d, c.play); break;
        case Command::Tx::Stop:        w.transport->excludeMask = 0; w.transport->takeOpen = false; w.transport->stop();           break;
        case Command::Tx::Pause:       w.transport->pause();            break;
        case Command::Tx::Advance:     w.transport->advance (d);        break;
        case Command::Tx::NormalSpeed: w.transport->normalSpeed();      break;
        case Command::Tx::Seek:        w.transport->seek (d, c.seekBytes); break;
        case Command::Tx::RateScale:   w.transport->setRateScale (c.play.rateScale); break;
        case Command::Tx::Direction:   w.transport->setDirection (c.direction, d); break;

        case Command::Tx::RecordStart:
            // Capture and play begin in this very block; recording is always Forward.
            cap = c.capture;
            capActive = true;
            capRunning.store (true, std::memory_order_release);
            w.transport->excludeMask = c.excludeMask;
            w.transport->takeOpen = true;
            w.transport->setDirection (PlayDirection::forward, d);
            w.transport->play (d, c.play);
            capStartFrame.store (0, std::memory_order_relaxed);
            capStartGen.fetch_add (1, std::memory_order_release);
            break;
        case Command::Tx::RecordJoin:
            // The transport is not restarted: it turns Forward where it is (re-anchored
            // under the usual crossfade) and the take begins on the frame it reads.
            cap = c.capture;
            capActive = true;
            capRunning.store (true, std::memory_order_release);
            w.transport->excludeMask = c.excludeMask;
            w.transport->takeOpen = true;
            w.transport->setDirection (PlayDirection::forward, d);
            if (w.transport->state() == TransportState::paused)       w.transport->pause();
            else if (w.transport->state() == TransportState::stopped) w.transport->play (d, c.play);
            capStartFrame.store (w.transport->readingFrame (d, c.refTrack), std::memory_order_relaxed);
            capStartGen.fetch_add (1, std::memory_order_release);
            break;
        case Command::Tx::RecordEnd:
            // Disarm: the capture ends, the transport carries on untouched; the tracks
            // written stay excluded until the take is published (RecordRelease).
            capActive = false;
            capRunning.store (false, std::memory_order_release);
            w.transport->takeOpen = false;   // a loop follows the selection again
            break;
        case Command::Tx::RecordRelease:
            // Queued after the take's SwapDoc, so the tracks it lets sound hold the take.
            if (! w.transport->takeOpen) w.transport->releaseExcluded();
            break;
        case Command::Tx::RecordPause:
            capActive = false;
            capRunning.store (false, std::memory_order_release);
            if (w.transport->state() == TransportState::playing) w.transport->pause (true);
            break;
        case Command::Tx::RecordResume:
            capActive = true;
            capRunning.store (true, std::memory_order_release);
            w.transport->excludeMask = c.excludeMask;
            w.transport->takeOpen = true;
            if (w.transport->state() == TransportState::paused) w.transport->pause();
            else if (w.transport->state() == TransportState::stopped) w.transport->play (d, c.play);
            break;
        case Command::Tx::RecordStop:
            capActive = false;
            capRunning.store (false, std::memory_order_release);
            w.transport->stop();
            w.transport->excludeMask = 0;
            w.transport->takeOpen = false;
            break;
    }
}

void Engine::retire (size_t i) noexcept
{
    Window* w = live[i];
    live.erase (live.begin() + static_cast<std::ptrdiff_t> (i));
    retiredWins.push (w);
}

/** The tape's last frames ramp to zero, so a playback that runs out does not cut. */
static float tapeEndGain (size_t frame, size_t length, int ramp) noexcept
{
    if (ramp <= 0) return 1.0f;
    const size_t after = frame + 1 < length ? length - frame - 1 : 0;
    return std::min (1.0f, static_cast<float> (after) / static_cast<float> (ramp));
}

void Engine::fadeOutTape() noexcept
{
    const float from = tapeLevel;
    const int ramp = declickFrames (deviceRate, declickMs);
    tapeLevel = ramp > 0 ? 0.0f : 1.0f;
    const size_t n = std::min (static_cast<size_t> (ramp), tfadeL.size());
    if (! tapePlayActive || from <= 0.0f || n == 0) return;

    // A fade still being heard carries on underneath.
    const size_t held = tfadeEnd - tfadeAt;
    for (auto* v : { &tfadeL, &tfadeR })
    {
        std::copy (v->begin() + static_cast<std::ptrdiff_t> (tfadeAt), v->begin() + static_cast<std::ptrdiff_t> (tfadeEnd), v->begin());
        std::fill (v->begin() + static_cast<std::ptrdiff_t> (held), v->end(), 0.0f);
    }
    const size_t len = tape.length();
    const size_t got = tape.read (tapePos, tsnapL.data(), tsnapR.data(), n);
    for (size_t k = 0; k < got; ++k)
    {
        const float g = from * static_cast<float> (n - k) / static_cast<float> (n) * tapeEndGain (tapePos + k, len, ramp);
        tfadeL[k] += tsnapL[k] * g;
        tfadeR[k] += tsnapR[k] * g;
    }
    tfadeAt = 0;
    tfadeEnd = std::max (held, got);
}

void Engine::drainCommands() noexcept
{
    Command c;
    while (incoming.pop (c))
    {
        switch (c.kind)
        {
            case Command::Kind::AddWindow:
                // No room: a closed Window still fading out gives up its place.
                for (size_t i = 0; live.size() >= live.capacity() && i < live.size(); ++i)
                    if (live[i]->leaving) retire (i);
                if (live.size() < live.capacity()) live.push_back (c.window);
                else                               retiredWins.push (c.window);
                break;

            case Command::Kind::RemoveWindow:
                for (size_t i = 0; i < live.size(); ++i)
                    if (live[i]->id == c.id)
                    {
                        // A Window that is sounding stops first and goes once it is silent.
                        Window* w = live[i];
                        if (w->transport != nullptr) w->transport->stop();
                        if (w->transport != nullptr && w->transport->audible()) w->leaving = true;
                        else retire (i);
                        break;
                    }
                break;

            case Command::Kind::SwapDoc:
                if (Window* w = liveById (c.id))
                {
                    SoundDocument* old = w->audioDoc;
                    w->audioDoc = c.doc;      // O(1) pointer swap; no alloc
                    retiredDocs.push (old);   // freed on the message thread
                }
                else
                {
                    retiredDocs.push (c.doc); // window gone before swap applied
                }
                break;

            case Command::Kind::SetMeta:
                // In place: the audio thread owns audioDoc, and these are plain
                // values. A document whose tracks differ (it cannot happen while
                // every track edit goes through commitEdit) is left alone.
                if (Window* w = liveById (c.id); w != nullptr && w->audioDoc != nullptr
                                                 && w->audioDoc->numTracks() == c.meta.numTracks)
                {
                    SoundDocument& d = *w->audioDoc;
                    d.selSt = c.meta.selSt;   d.selEnd = c.meta.selEnd;
                    d.loopSt = c.meta.loopSt; d.loopEnd = c.meta.loopEnd;
                    d.firstSelTrack = c.meta.firstSelTrack; d.lastSelTrack = c.meta.lastSelTrack;
                    for (int t = 0; t < c.meta.numTracks; ++t)
                    {
                        d.tracks[(size_t) t].outLeft  = ((c.meta.outLeft  >> t) & 1u) != 0;
                        d.tracks[(size_t) t].outRight = ((c.meta.outRight >> t) & 1u) != 0;
                    }
                }
                break;

            case Command::Kind::Transport:
                if (Window* w = liveById (c.id))
                {
                    applyTransport (*w, c);
                    // After the state it published, so a reader that sees this sees that.
                    if (c.tx == Command::Tx::Play || c.tx == Command::Tx::Stop || c.tx == Command::Tx::Pause)
                        w->rollApplied.store (c.gen, std::memory_order_release);
                }
                break;

            case Command::Kind::CaptureStart:
                cap = c.capture;
                capActive = true;
                capRunning.store (true, std::memory_order_release);
                break;

            case Command::Kind::CaptureStop:
                capActive = false;
                capRunning.store (false, std::memory_order_release);
                break;

            case Command::Kind::ProbeStart:
                probe = c.capture;
                probeActive = true;
                probeMeterL.store (0.0f, std::memory_order_relaxed);
                probeMeterR.store (0.0f, std::memory_order_relaxed);
                break;

            case Command::Kind::ProbeStop:
                probeActive = false;
                probeMeterL.store (0.0f, std::memory_order_relaxed);
                probeMeterR.store (0.0f, std::memory_order_relaxed);
                break;

            case Command::Kind::TapeCaptureStart:
                tapeCap = c.tape;
                tapeCapActive = true;
                tapeCapRunning.store (true, std::memory_order_release);
                break;

            case Command::Kind::TapeCaptureStop:
                tapeCapActive = false;
                tapeCapRunning.store (false, std::memory_order_release);
                break;

            case Command::Kind::TapePlay:
                fadeOutTape();
                tapePos = c.seekBytes;
                tapePlayActive = true;
                scrubActive = false;
                tapePlayPos.store (tapePos, std::memory_order_relaxed);
                tapePlayingFlag.store (true, std::memory_order_release);
                tapeAck.store (c.gen, std::memory_order_release);
                break;

            case Command::Kind::TapeStop:
                fadeOutTape();
                tapePlayActive = false;
                tapePlayingFlag.store (scrubActive, std::memory_order_release);
                tapeAck.store (c.gen, std::memory_order_release);
                break;

            case Command::Kind::TapeScrub:
                fadeOutTape();
                if (! scrubActive) { scrubPos = (double) c.seekBytes; scrubVel = 0.0; scrubGain = 0.0f; }
                scrubActive = true;
                scrubReleasing = false;
                tapePlayActive = false;
                tapePlayingFlag.store (true, std::memory_order_release);
                tapeAck.store (c.gen, std::memory_order_release);
                break;

            case Command::Kind::TapeScrubEnd:
                scrubReleasing = scrubActive;
                tapeAck.store (c.gen, std::memory_order_release);
                break;
        }
    }
}

void Engine::render (float* left, float* right, int frames,
                     const float* const* inputs, int numInputs) noexcept
{
    drainCommands();

    std::fill (left,  left  + frames, 0.0f);
    std::fill (right, right + frames, 0.0f);

    const int cap_ = std::min (frames, static_cast<int> (scratchL.size()));
    const bool capturing = capActive && capL.size() >= scratchL.size();
    if (capturing)
        for (auto* v : { &tapL, &tapR, &exL, &exR })
            std::fill (v->begin(), v->begin() + cap_, 0.0f);
    const bool tapeCapturing = tapeCapActive && tcapL.size() >= scratchL.size();
    const bool tapeTapping = tapeCapturing && tapeCap.kind == AudioSourceKind::canvas && tapeCap.tapWindow != 0;
    if (tapeTapping)
        for (auto* v : { &ttapL, &ttapR })
            std::fill (v->begin(), v->begin() + cap_, 0.0f);

    const bool probing = probeActive && pexL.size() >= scratchL.size();
    const bool probingCanvas = probing && probe.kind == AudioSourceKind::canvas;
    if (probingCanvas)
        for (auto* v : { &pexL, &pexR })
            std::fill (v->begin(), v->begin() + cap_, 0.0f);
    float probePeakL = 0.0f, probePeakR = 0.0f;

    // VU fall-off, ~-60 dB over 300 ms, applied once per block. Doing the decay
    // here rather than in the UI keeps the meters honest at any repaint rate: a
    // Window that stops being rendered still falls instead of freezing lit.
    const float meterDecay = std::exp (-static_cast<float> (frames)
                                        / static_cast<float> (deviceRate * 0.043));

    // Solo is a filter over the whole desktop, so it has to be resolved before any
    // Window is rendered: one pass to ask whether ANY channel is soloed, then the
    // per-Window test below. Cheap — a handful of relaxed loads over `live`.
    const bool tapeSolo = tapeSoloValue.load (std::memory_order_relaxed);
    bool anySolo = tapeSolo;
    for (Window* w : live)
        if (! w->leaving && w->soloed.load (std::memory_order_relaxed)) { anySolo = true; break; }

    // Every gain change, mute and solo included, is ramped (Declick.h).
    const int ramp = declickFrames (deviceRate, declickMs);

    for (Window* w : live)
    {
        float peakL = 0.0f, peakR = 0.0f;

        const bool playable = w->transport != nullptr && w->audioDoc != nullptr;
        const bool audible  = ! w->muted.load (std::memory_order_relaxed)
                              && (! anySolo || w->soloed.load (std::memory_order_relaxed));
        const float g = w->gain.load (std::memory_order_relaxed);
        const auto  bal = balanceGains (w->balance.load (std::memory_order_relaxed));
        if (! w->rampsSet)
        {
            w->faderLeft.jump (g * bal.left);
            w->faderRight.jump (g * bal.right);
            w->gate.jump (audible ? 1.0f : 0.0f);
            w->rampsSet = true;
        }
        w->faderLeft.aim (g * bal.left, ramp);
        w->faderRight.aim (g * bal.right, ramp);
        w->gate.aim (audible ? 1.0f : 0.0f, ramp);

        if (playable)
        {
            // Render even when this channel will not be heard, and throw the samples
            // away. It is the render call that ADVANCES the transport, and the
            // free-running loops of different lengths phasing against each other is
            // the whole point of running several Windows at once — a channel that
            // froze while muted, un-soloed, or faded to zero would come back at the
            // wrong point and take that phase relationship with it.
            //
            // It costs nothing for a channel that is not rolling: a stopped
            // transport returns 0 frames before doing any work.
            const int written = w->transport->render (*w->audioDoc, scratchL.data(), scratchR.data(), cap_);

            for (int i = 0; i < written; ++i)
            {
                const float open = w->gate.at (i);
                const float l = scratchL[static_cast<size_t> (i)] * w->faderLeft.at (i) * open;
                const float r = scratchR[static_cast<size_t> (i)] * w->faderRight.at (i) * open;
                left[i]  += l;
                right[i] += r;
                peakL = std::max (peakL, std::abs (l));
                peakR = std::max (peakR, std::abs (r));
            }

            // The Canvas source's taps. A Window's own tap is post-fader and
            // post-balance but ignores mute and solo (a Sound can be sampled while
            // it is silent in the mix); the main mix leaves out the Window being
            // recorded into, as much of it as the mix actually carried.
            if (capturing && cap.kind == AudioSourceKind::canvas)
            {
                if (w->id == cap.tapWindow)
                    for (int i = 0; i < written; ++i)
                    {
                        tapL[static_cast<size_t> (i)] = scratchL[static_cast<size_t> (i)] * w->faderLeft.at (i);
                        tapR[static_cast<size_t> (i)] = scratchR[static_cast<size_t> (i)] * w->faderRight.at (i);
                    }
                if (w->id == cap.excludeWindow)
                    for (int i = 0; i < written; ++i)
                    {
                        exL[static_cast<size_t> (i)] = scratchL[static_cast<size_t> (i)] * w->faderLeft.at (i) * w->gate.at (i);
                        exR[static_cast<size_t> (i)] = scratchR[static_cast<size_t> (i)] * w->faderRight.at (i) * w->gate.at (i);
                    }
            }

            // The source meter: one Sound's own tap, or the share of the one the
            // main mix leaves out.
            if (probingCanvas && probe.tapWindow != 0 && w->id == probe.tapWindow)
                for (int i = 0; i < written; ++i)
                {
                    probePeakL = std::max (probePeakL, std::abs (scratchL[static_cast<size_t> (i)] * w->faderLeft.at (i)));
                    probePeakR = std::max (probePeakR, std::abs (scratchR[static_cast<size_t> (i)] * w->faderRight.at (i)));
                }
            if (probingCanvas && w->id == probe.excludeWindow)
                for (int i = 0; i < written; ++i)
                {
                    pexL[static_cast<size_t> (i)] = scratchL[static_cast<size_t> (i)] * w->faderLeft.at (i) * w->gate.at (i);
                    pexR[static_cast<size_t> (i)] = scratchR[static_cast<size_t> (i)] * w->faderRight.at (i) * w->gate.at (i);
                }

            // The Magnetophon's own tap on one Sound: post-fader, post-balance, deaf
            // to mute and solo, like the Recorder's.
            if (tapeTapping && w->id == tapeCap.tapWindow)
                for (int i = 0; i < written; ++i)
                {
                    ttapL[static_cast<size_t> (i)] = scratchL[static_cast<size_t> (i)] * w->faderLeft.at (i);
                    ttapR[static_cast<size_t> (i)] = scratchR[static_cast<size_t> (i)] * w->faderRight.at (i);
                }
        }
        w->faderLeft.advance (cap_);
        w->faderRight.advance (cap_);
        w->gate.advance (cap_);

        const float heldL = w->meterLeft.load (std::memory_order_relaxed) * meterDecay;
        const float heldR = w->meterRight.load (std::memory_order_relaxed) * meterDecay;
        w->meterLeft.store  (std::max (peakL, heldL), std::memory_order_relaxed);
        w->meterRight.store (std::max (peakR, heldR), std::memory_order_relaxed);
    }

    // The closed Windows whose fade-out is over.
    for (size_t i = live.size(); i-- > 0;)
        if (live[i]->leaving && ! live[i]->transport->audible()) retire (i);

    // Main Level: the master fader over the summed Windows, ramped so neither a drag
    // nor a jump is heard as one. It scales the mix only; every transport has already
    // been rendered above, so no gain value, zero included, can stop or re-aim one.
    // The excluded Window's share is scaled alike, so "the mix without it" stays
    // exact for the Canvas capture, which therefore hears what the speakers get.
    // Main Mute is the same fader pulled to zero. What is added to the output after
    // this point (monitors, the tape) follows the same two ramps sample by sample.
    const bool mainMuted = masterMutedValue.load (std::memory_order_relaxed);
    masterRamp.aim (mainMuted ? 0.0f : masterGainValue.load (std::memory_order_relaxed), ramp);
    muteRamp.aim (mainMuted ? 0.0f : 1.0f, ramp);
    const auto masterAt = [this] (size_t i) { return masterRamp.at (static_cast<int> (i)); };
    const auto muteAt   = [this] (size_t i) { return muteRamp.at (static_cast<int> (i)); };
    if (masterRamp.left > 0 || masterRamp.target != 1.0f)
    {
        const bool scaleEx = capturing && cap.kind == AudioSourceKind::canvas;
        for (int i = 0; i < frames; ++i)
        {
            const float m = masterRamp.at (i);
            left[i] *= m;
            right[i] *= m;
            if (scaleEx && i < cap_)
            {
                exL[static_cast<size_t> (i)] *= m;
                exR[static_cast<size_t> (i)] *= m;
            }
        }
    }

    // The source meter reads here, where the captures do: after Main Level, before
    // anything is added to the output.
    if (probing)
    {
        if (probingCanvas && probe.tapWindow == 0)
        {
            for (int i = 0; i < cap_; ++i)
            {
                const size_t k = static_cast<size_t> (i);
                const float m = masterRamp.at (i);
                probePeakL = std::max (probePeakL, std::abs (left[i]  - pexL[k] * m));
                probePeakR = std::max (probePeakR, std::abs (right[i] - pexR[k] * m));
            }
        }
        else if (! probingCanvas)
        {
            const int ch = probe.kind == AudioSourceKind::daw ? 0 : std::max (0, probe.firstChannel);
            const float* a = (inputs != nullptr && ch < numInputs) ? inputs[ch] : nullptr;
            const float* b = (probe.stereo && inputs != nullptr && ch + 1 < numInputs) ? inputs[ch + 1] : a;
            for (int i = 0; i < cap_; ++i)
            {
                probePeakL = std::max (probePeakL, std::abs (a != nullptr ? a[i] : 0.0f));
                probePeakR = std::max (probePeakR, std::abs (b != nullptr ? b[i] : 0.0f));
            }
        }
        const float gain = probeGainValue.load (std::memory_order_relaxed);
        probeMeterL.store (std::max (std::min (probePeakL * gain, 1.0f),
                                     probeMeterL.load (std::memory_order_relaxed) * meterDecay), std::memory_order_relaxed);
        probeMeterR.store (std::max (std::min (probePeakR * gain, 1.0f),
                                     probeMeterR.load (std::memory_order_relaxed) * meterDecay), std::memory_order_relaxed);
    }

    if (capturing)
    {
        const float gain = inputGainValue.load (std::memory_order_relaxed);
        const int n = cap_;
        bool heard = false;   // the source is NOT already part of the mix

        if (cap.kind == AudioSourceKind::canvas)
        {
            const bool tapped = cap.tapWindow != 0;
            for (int i = 0; i < n; ++i)
            {
                const size_t k = static_cast<size_t> (i);
                capL[k] = (tapped ? tapL[k] : left[i]  - exL[k]) * gain;
                capR[k] = (tapped ? tapR[k] : right[i] - exR[k]) * gain;
            }
        }
        else
        {
            // External: the chosen device channel(s); DAW: the host's input pair.
            const int ch = cap.kind == AudioSourceKind::daw ? 0 : std::max (0, cap.firstChannel);
            const float* a = (inputs != nullptr && ch < numInputs) ? inputs[ch] : nullptr;
            const float* b = (cap.stereo && inputs != nullptr && ch + 1 < numInputs) ? inputs[ch + 1] : a;
            for (int i = 0; i < n; ++i)
            {
                const size_t k = static_cast<size_t> (i);
                capL[k] = (a != nullptr ? a[i] : 0.0f) * gain;
                capR[k] = (b != nullptr ? b[i] : 0.0f) * gain;
            }
            heard = monitorOn.load (std::memory_order_relaxed);
        }

        for (int i = 0; i < n; ++i)
        {
            const size_t k = static_cast<size_t> (i);
            capL[k] = std::clamp (capL[k], -1.0f, 1.0f);
            capR[k] = std::clamp (capR[k], -1.0f, 1.0f);
            if (heard) { left[i] += capL[k] * muteAt (k); right[i] += capR[k] * muteAt (k); }
        }
        ring.push (capL.data(), capR.data(), static_cast<size_t> (n));
    }

    // The Magnetophon's capture: after Main Level and after the Recorder's monitor,
    // so Main is what the speakers get so far, and BEFORE the tape's own playback is
    // added below, so the tape never records itself.
    if (tapeCapturing)
    {
        const float gain = tapeGainValue.load (std::memory_order_relaxed);
        const int n = cap_;
        bool heard = false;
        if (tapeCap.kind == AudioSourceKind::canvas)
        {
            const bool tapped = tapeCap.tapWindow != 0;
            for (int i = 0; i < n; ++i)
            {
                const size_t k = static_cast<size_t> (i);
                tcapL[k] = (tapped ? ttapL[k] : left[i])  * gain;
                tcapR[k] = (tapped ? ttapR[k] : right[i]) * gain;
            }
        }
        else
        {
            const int ch = tapeCap.kind == AudioSourceKind::daw ? 0 : std::max (0, tapeCap.firstChannel);
            const float* a = (inputs != nullptr && ch < numInputs) ? inputs[ch] : nullptr;
            const float* b = (tapeCap.stereo && inputs != nullptr && ch + 1 < numInputs) ? inputs[ch + 1] : a;
            for (int i = 0; i < n; ++i)
            {
                const size_t k = static_cast<size_t> (i);
                tcapL[k] = (a != nullptr ? a[i] : 0.0f) * gain;
                tcapR[k] = (b != nullptr ? b[i] : 0.0f) * gain;
            }
            heard = tapeMonitorOn.load (std::memory_order_relaxed);
        }

        float peak = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const size_t k = static_cast<size_t> (i);
            tcapL[k] = std::clamp (tcapL[k], -1.0f, 1.0f);
            tcapR[k] = std::clamp (tcapR[k], -1.0f, 1.0f);
            peak = std::max (peak, std::max (std::abs (tcapL[k]), std::abs (tcapR[k])));
            if (heard) { left[i] += tcapL[k] * muteAt (k); right[i] += tcapR[k] * muteAt (k); }
        }
        tapeMeterValue.store (std::max (peak, tapeMeterValue.load (std::memory_order_relaxed) * meterDecay),
                              std::memory_order_relaxed);
        tapeRingBuf.push (tcapL.data(), tcapR.data(), static_cast<size_t> (n));
    }
    else
    {
        tapeMeterValue.store (tapeMeterValue.load (std::memory_order_relaxed) * meterDecay, std::memory_order_relaxed);
    }

    // The solo filter on the tape. With the tape not being read there is nothing to
    // ramp: a playback starts at the gate's setting.
    const float tapeOpen = ! anySolo || tapeSolo ? 1.0f : 0.0f;
    if (! tapePlayActive && ! scrubActive && tfadeAt >= tfadeEnd) tapeGate.jump (tapeOpen);
    else                                                          tapeGate.aim (tapeOpen, ramp);
    const auto tapeAt = [&] (size_t i) { return masterAt (i) * tapeGate.at (static_cast<int> (i)); };

    // The tape's playback, last of all: heard through Main Level, captured by nothing.
    if (tapePlayActive && tplayL.size() >= scratchL.size())
    {
        const size_t want = static_cast<size_t> (cap_);
        const size_t len = tape.length();
        const size_t got = tape.read (tapePos, tplayL.data(), tplayR.data(), want);
        const float rise = ramp > 0 ? 1.0f / static_cast<float> (ramp) : 1.0f;
        for (size_t k = 0; k < got; ++k)
        {
            const float g = tapeLevel * tapeEndGain (tapePos + k, len, ramp) * tapeAt (k);
            left[k]  += tplayL[k] * g;
            right[k] += tplayR[k] * g;
            tapeLevel = std::min (1.0f, tapeLevel + rise);
        }
        tapePos += got;
        tapePlayPos.store (tapePos, std::memory_order_relaxed);
        if (got < want)
        {
            // The end of the tape: playback stops there by itself.
            tapePlayActive = false;
            tapePlayingFlag.store (scrubActive, std::memory_order_release);
        }
    }

    // What a stop or a jump left fading out.
    for (size_t k = 0; tfadeAt < tfadeEnd && k < static_cast<size_t> (cap_); ++k, ++tfadeAt)
    {
        left[k]  += tfadeL[tfadeAt] * tapeAt (k);
        right[k] += tfadeR[tfadeAt] * tapeAt (k);
    }

    // Scrub: the read point chases the dragged target, varispeed and interpolated;
    // the level follows the speed, so the tape at rest is silent and nothing clicks.
    if (scrubActive)
    {
        const size_t len = tape.length();
        const double target = (double) tapeScrubTarget.load (std::memory_order_relaxed);
        const double lag = deviceRate * 0.05;
        constexpr double kMaxSpeed = 8.0;
        for (int i = 0; i < cap_; ++i)
        {
            const double want = std::clamp ((target - scrubPos) / lag, -kMaxSpeed, kMaxSpeed);
            scrubVel += (want - scrubVel) * scrubVelCoeff;
            const double hi = len > 1 ? (double) (len - 1) : 0.0;
            scrubPos = std::clamp (scrubPos + scrubVel, 0.0, hi);
            // Below 2 % of normal speed the tape is as good as still: silent. Full
            // level from a quarter of normal speed.
            const double speed = std::abs (scrubVel);
            const float gWant = scrubReleasing ? 0.0f : (float) std::clamp ((speed - 0.02) * 4.4, 0.0, 1.0);
            scrubGain += (gWant - scrubGain) * (float) scrubGainCoeff;
            if (len < 2 || scrubGain < 1.0e-5f) continue;
            const size_t f0 = (size_t) scrubPos;
            const size_t f1 = std::min (f0 + 1, len - 1);
            const float t = (float) (scrubPos - (double) f0);
            float l0, r0, l1, r1;
            tape.frameAt (f0, l0, r0);
            tape.frameAt (f1, l1, r1);
            const float m = tapeAt (static_cast<size_t> (i));
            left[i]  += (l0 + (l1 - l0) * t) * scrubGain * m;
            right[i] += (r0 + (r1 - r0) * t) * scrubGain * m;
        }
        if (scrubReleasing && scrubGain < 1.0e-5f)
        {
            scrubActive = scrubReleasing = false;
            tapePlayingFlag.store (tapePlayActive, std::memory_order_release);
        }
    }

    tapeGate.advance (cap_);
    masterRamp.advance (frames);
    muteRamp.advance (frames);

    float peakL = 0.0f, peakR = 0.0f;
    for (int i = 0; i < frames; ++i)
    {
        left[i]  = std::clamp (left[i],  -1.0f, 1.0f);
        right[i] = std::clamp (right[i], -1.0f, 1.0f);
        peakL = std::max (peakL, std::abs (left[i]));
        peakR = std::max (peakR, std::abs (right[i]));
    }
    masterMeterL.store (std::max (peakL, masterMeterL.load (std::memory_order_relaxed) * meterDecay),
                        std::memory_order_relaxed);
    masterMeterR.store (std::max (peakR, masterMeterR.load (std::memory_order_relaxed) * meterDecay),
                        std::memory_order_relaxed);
}

} // namespace se
