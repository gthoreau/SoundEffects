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
    for (auto* v : { &capL, &capR, &tapL, &tapR, &exL, &exR })
        v->assign (static_cast<size_t> (std::max (1, maxBlockSize)), 0.0f);
    live.reserve (kMaxWindows);

    // Pre-audio (prepareToPlay): safe to touch every known transport.
    for (auto& kv : registry)
        if (kv.second->transport) kv.second->transport->setDeviceRate (rate);
}

// ============================ message thread ================================

uint64_t Engine::addWindow (std::unique_ptr<SoundDocument> doc)
{
    if (registry.size() >= kMaxWindows) return 0;

    const uint64_t id = nextId++;

    auto* w = new Window();
    w->id = id;
    w->audioDoc = new SoundDocument (doc->clone());     // audio-side copy
    w->transport = std::make_unique<Transport> (deviceRate);
    w->uiDoc = std::move (doc);                          // UI-side authoritative
    w->slot = lowestFreeSlot();                          // a new Sound takes the lowest free slot

    Command c; c.kind = Command::Kind::AddWindow; c.id = id; c.window = w;
    if (! post (c)) { delete w; --nextId; return 0; }

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

bool Engine::moveToSlot (uint64_t id, int slot)
{
    auto* w = windowById (id);
    if (w == nullptr || slot < 0) return false;
    if (w->slot == slot) return true;

    // An occupied target trades places: the two strips swap slots.
    if (const uint64_t other = windowInSlot (slot); other != 0)
        windowById (other)->slot = w->slot;
    w->slot = slot;
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
}

bool Engine::anyWindowSoloed() const noexcept
{
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

bool Engine::play (uint64_t id, const PlayOptions& opts)
{ Command c; c.kind=Command::Kind::Transport; c.tx=Command::Tx::Play; c.id=id; c.play=opts; return post (c); }
bool Engine::stop (uint64_t id)
{ Command c; c.kind=Command::Kind::Transport; c.tx=Command::Tx::Stop; c.id=id; return post (c); }
bool Engine::pause (uint64_t id)
{ Command c; c.kind=Command::Kind::Transport; c.tx=Command::Tx::Pause; c.id=id; return post (c); }
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
        case Command::Tx::Play:        w.transport->play (d, c.play);   break;
        case Command::Tx::Stop:        w.transport->stop();             break;
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
            w.transport->setDirection (PlayDirection::forward, d);
            w.transport->play (d, c.play);
            break;
        case Command::Tx::RecordPause:
            capActive = false;
            capRunning.store (false, std::memory_order_release);
            if (w.transport->state() == TransportState::playing) w.transport->pause();
            break;
        case Command::Tx::RecordResume:
            capActive = true;
            capRunning.store (true, std::memory_order_release);
            w.transport->excludeMask = c.excludeMask;
            if (w.transport->state() == TransportState::paused) w.transport->pause();
            else if (w.transport->state() == TransportState::stopped) w.transport->play (d, c.play);
            break;
        case Command::Tx::RecordStop:
            capActive = false;
            capRunning.store (false, std::memory_order_release);
            w.transport->stop();
            w.transport->excludeMask = 0;
            break;
    }
}

void Engine::drainCommands() noexcept
{
    Command c;
    while (incoming.pop (c))
    {
        switch (c.kind)
        {
            case Command::Kind::AddWindow:
                if (live.size() < live.capacity()) live.push_back (c.window);
                else                               retiredWins.push (c.window);
                break;

            case Command::Kind::RemoveWindow:
                for (size_t i = 0; i < live.size(); ++i)
                    if (live[i]->id == c.id)
                    {
                        Window* w = live[i];
                        live.erase (live.begin() + static_cast<std::ptrdiff_t> (i));
                        retiredWins.push (w);
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

            case Command::Kind::Transport:
                if (Window* w = liveById (c.id)) applyTransport (*w, c);
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

    // VU fall-off, ~-60 dB over 300 ms, applied once per block. Doing the decay
    // here rather than in the UI keeps the meters honest at any repaint rate: a
    // Window that stops being rendered still falls instead of freezing lit.
    const float meterDecay = std::exp (-static_cast<float> (frames)
                                        / static_cast<float> (deviceRate * 0.043));

    // Solo is a filter over the whole desktop, so it has to be resolved before any
    // Window is rendered: one pass to ask whether ANY channel is soloed, then the
    // per-Window test below. Cheap — a handful of relaxed loads over `live`.
    bool anySolo = false;
    for (Window* w : live)
        if (w->soloed.load (std::memory_order_relaxed)) { anySolo = true; break; }

    for (Window* w : live)
    {
        float peakL = 0.0f, peakR = 0.0f;

        const bool playable = w->transport != nullptr && w->audioDoc != nullptr;
        const bool audible  = ! w->muted.load (std::memory_order_relaxed)
                              && (! anySolo || w->soloed.load (std::memory_order_relaxed));
        const float g = w->gain.load (std::memory_order_relaxed);
        const auto  bal = balanceGains (w->balance.load (std::memory_order_relaxed));
        const float gl = g * bal.left, gr = g * bal.right;

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

            if (audible && g > 0.0f)
            {
                for (int i = 0; i < written; ++i)
                {
                    const float l = scratchL[static_cast<size_t> (i)] * gl;
                    const float r = scratchR[static_cast<size_t> (i)] * gr;
                    left[i]  += l;
                    right[i] += r;
                    peakL = std::max (peakL, std::abs (l));
                    peakR = std::max (peakR, std::abs (r));
                }
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
                        tapL[static_cast<size_t> (i)] = scratchL[static_cast<size_t> (i)] * gl;
                        tapR[static_cast<size_t> (i)] = scratchR[static_cast<size_t> (i)] * gr;
                    }
                if (w->id == cap.excludeWindow && audible && g > 0.0f)
                    for (int i = 0; i < written; ++i)
                    {
                        exL[static_cast<size_t> (i)] = scratchL[static_cast<size_t> (i)] * gl;
                        exR[static_cast<size_t> (i)] = scratchR[static_cast<size_t> (i)] * gr;
                    }
            }
        }

        const float heldL = w->meterLeft.load (std::memory_order_relaxed) * meterDecay;
        const float heldR = w->meterRight.load (std::memory_order_relaxed) * meterDecay;
        w->meterLeft.store  (std::max (peakL, heldL), std::memory_order_relaxed);
        w->meterRight.store (std::max (peakR, heldR), std::memory_order_relaxed);
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
            if (heard) { left[i] += capL[k]; right[i] += capR[k]; }
        }
        ring.push (capL.data(), capR.data(), static_cast<size_t> (n));
    }

    for (int i = 0; i < frames; ++i)
    {
        left[i]  = std::clamp (left[i],  -1.0f, 1.0f);
        right[i] = std::clamp (right[i], -1.0f, 1.0f);
    }
}

} // namespace se
