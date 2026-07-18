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
bool Engine::seek (uint64_t id, size_t bytes)
{ Command c; c.kind=Command::Kind::Transport; c.tx=Command::Tx::Seek; c.id=id; c.seekBytes=bytes; return post (c); }

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
        }
    }
}

void Engine::render (float* left, float* right, int frames) noexcept
{
    drainCommands();

    std::fill (left,  left  + frames, 0.0f);
    std::fill (right, right + frames, 0.0f);

    const int cap = std::min (frames, static_cast<int> (scratchL.size()));

    for (Window* w : live)
    {
        if (w->muted.load (std::memory_order_relaxed) || ! w->transport || w->audioDoc == nullptr) continue;
        const float g = w->gain.load (std::memory_order_relaxed);
        if (g <= 0.0f) continue;

        const int written = w->transport->render (*w->audioDoc, scratchL.data(), scratchR.data(), cap);
        for (int i = 0; i < written; ++i)
        {
            left[i]  += scratchL[static_cast<size_t> (i)] * g;
            right[i] += scratchR[static_cast<size_t> (i)] * g;
        }
    }

    for (int i = 0; i < frames; ++i)
    {
        left[i]  = std::clamp (left[i],  -1.0f, 1.0f);
        right[i] = std::clamp (right[i], -1.0f, 1.0f);
    }
}

} // namespace se
