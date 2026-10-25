#include "EngineState.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>

namespace se {

namespace {

// The blob is little-endian and byte-explicit rather than a memcpy of structs:
// plugin state travels between machines and between builds, so nothing here may
// depend on this compiler's padding, enum size, or endianness.

constexpr uint8_t kMagic[4] = { 'S', 'E', 'F', 'X' };

// --- writing ----------------------------------------------------------------

void putU8 (std::vector<uint8_t>& out, uint8_t v) { out.push_back (v); }

void putU32 (std::vector<uint8_t>& out, uint32_t v)
{
    for (int i = 0; i < 4; ++i) out.push_back (static_cast<uint8_t> ((v >> (i * 8)) & 0xff));
}

void putU64 (std::vector<uint8_t>& out, uint64_t v)
{
    for (int i = 0; i < 8; ++i) out.push_back (static_cast<uint8_t> ((v >> (i * 8)) & 0xff));
}

void putI32 (std::vector<uint8_t>& out, int32_t v) { putU32 (out, static_cast<uint32_t> (v)); }
void putSize (std::vector<uint8_t>& out, size_t v) { putU64 (out, static_cast<uint64_t> (v)); }

void putF32 (std::vector<uint8_t>& out, float v)
{
    uint32_t bits = 0;
    static_assert (sizeof (bits) == sizeof (v), "float is not 32 bits");
    std::memcpy (&bits, &v, sizeof (bits));
    putU32 (out, bits);
}

void putF64 (std::vector<uint8_t>& out, double v)
{
    uint64_t bits = 0;
    static_assert (sizeof (bits) == sizeof (v), "double is not 64 bits");
    std::memcpy (&bits, &v, sizeof (bits));
    putU64 (out, bits);
}

void putString (std::vector<uint8_t>& out, const std::string& s)
{
    putSize (out, s.size());
    out.insert (out.end(), s.begin(), s.end());
}

void putBytes (std::vector<uint8_t>& out, const std::vector<uint8_t>& b)
{
    putSize (out, b.size());
    out.insert (out.end(), b.begin(), b.end());
}

// --- reading ----------------------------------------------------------------

/** A cursor that can only fail closed: once `ok` is false every read is a no-op,
    so the reader below can run straight through and be checked once at the end. */
struct Reader
{
    const uint8_t* p = nullptr;
    size_t remaining = 0;
    bool ok = true;

    bool take (size_t n)
    {
        if (! ok || remaining < n) { ok = false; return false; }
        return true;
    }

    void advance (size_t n) { p += n; remaining -= n; }

    uint8_t u8()
    {
        if (! take (1)) return 0;
        const uint8_t v = *p;
        advance (1);
        return v;
    }

    uint32_t u32()
    {
        if (! take (4)) return 0;
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t> (p[i]) << (i * 8);
        advance (4);
        return v;
    }

    uint64_t u64()
    {
        if (! take (8)) return 0;
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t> (p[i]) << (i * 8);
        advance (8);
        return v;
    }

    int32_t i32() { return static_cast<int32_t> (u32()); }

    float f32()
    {
        const uint32_t bits = u32();
        float v = 0.0f;
        std::memcpy (&v, &bits, sizeof (v));
        return v;
    }

    double f64()
    {
        const uint64_t bits = u64();
        double v = 0.0;
        std::memcpy (&v, &bits, sizeof (v));
        return v;
    }

    /** A length, refused unless the rest of the blob could actually contain it.
        This is the allocation guard: a corrupt 2^60 never reaches a resize. */
    size_t length()
    {
        const uint64_t n = u64();
        if (! ok) return 0;
        if (n > static_cast<uint64_t> (remaining)) { ok = false; return 0; }
        return static_cast<size_t> (n);
    }

    std::string str()
    {
        const size_t n = length();
        if (! ok) return {};
        std::string s (reinterpret_cast<const char*> (p), n);
        advance (n);
        return s;
    }

    std::vector<uint8_t> bytes()
    {
        const size_t n = length();
        if (! ok) return {};
        std::vector<uint8_t> b (p, p + n);
        advance (n);
        return b;
    }

    /** A count of fixed-size records, bounded the same way `length()` bounds bytes:
        each record costs at least `minBytesEach`, so no count can promise more
        records than the remaining blob could hold. */
    size_t count (size_t minBytesEach)
    {
        const uint32_t n = u32();
        if (! ok) return 0;
        if (static_cast<uint64_t> (n) * minBytesEach > static_cast<uint64_t> (remaining))
        {
            ok = false;
            return 0;
        }
        return static_cast<size_t> (n);
    }
};

} // namespace

// ============================== blob <-> state ==============================

std::vector<uint8_t> serializeState (const EngineState& state)
{
    std::vector<uint8_t> out;

    // Rough reserve: the track bytes dominate everything else by orders of
    // magnitude, so counting them alone spares the vector its regrowth.
    size_t audioBytes = 0;
    for (const auto& w : state.windows)
        for (const auto& t : w.tracks)
            audioBytes += t.data.size();
    out.reserve (audioBytes + 256 * state.windows.size() + 64);

    out.insert (out.end(), kMagic, kMagic + 4);
    putU32 (out, kEngineStateVersion);
    putU32 (out, static_cast<uint32_t> (state.windows.size()));

    for (const auto& w : state.windows)
    {
        putString (out, w.name);
        putF64 (out, w.rateHz);
        putI32 (out, w.bps);
        putI32 (out, w.baseFreq);

        putSize (out, w.selSt);
        putSize (out, w.selEnd);
        putI32 (out, w.firstSelTrack);
        putI32 (out, w.lastSelTrack);
        putSize (out, w.loopSt);
        putSize (out, w.loopEnd);

        putU32 (out, static_cast<uint32_t> (w.chunks.size()));
        for (const auto& c : w.chunks)
        {
            putString (out, c.name);
            putSize (out, c.start);
            putSize (out, c.length);
            putI32 (out, c.track);
            putU8 (out, c.kind == Chunk::Kind::Clipping ? 1 : 0);
        }

        putU32 (out, static_cast<uint32_t> (w.tracks.size()));
        for (const auto& t : w.tracks)
        {
            putU8 (out, t.outLeft ? 1 : 0);
            putU8 (out, t.outRight ? 1 : 0);
            putI32 (out, t.volume);
            putBytes (out, t.data);
        }

        putF32 (out, w.x);
        putF32 (out, w.y);
        putF32 (out, w.width);
        putF32 (out, w.height);
        putI32 (out, w.gridCol);
        putI32 (out, w.gridRow);
        putI32 (out, w.tint);

        putF32 (out, w.gain);
        putU8 (out, w.muted ? 1 : 0);
        putU8 (out, w.soloed ? 1 : 0);
        putF32 (out, w.balance);
        putI32 (out, w.slot);
    }

    return out;
}

bool parseEngineState (const void* data, size_t sizeBytes, EngineState& out)
{
    if (data == nullptr || sizeBytes < 12) return false;

    Reader r { static_cast<const uint8_t*> (data), sizeBytes, true };

    if (std::memcmp (r.p, kMagic, 4) != 0) return false;
    r.advance (4);

    const uint32_t version = r.u32();
    if (! r.ok || version == 0 || version > kEngineStateVersion) return false;

    EngineState parsed;

    // A Window costs far more than 8 bytes, but the floor only has to be non-zero
    // to stop a bogus count from reserving the world; the reads do the rest.
    const size_t numWindows = r.count (8);
    if (! r.ok) return false;
    parsed.windows.reserve (numWindows);

    for (size_t i = 0; i < numWindows && r.ok; ++i)
    {
        WindowState w;
        w.name     = r.str();
        w.rateHz   = r.f64();
        w.bps      = r.i32();
        w.baseFreq = r.i32();

        w.selSt         = static_cast<size_t> (r.u64());
        w.selEnd        = static_cast<size_t> (r.u64());
        w.firstSelTrack = r.i32();
        w.lastSelTrack  = r.i32();
        w.loopSt        = static_cast<size_t> (r.u64());
        w.loopEnd       = static_cast<size_t> (r.u64());

        const size_t numChunks = r.count (8);
        if (! r.ok) return false;
        w.chunks.reserve (numChunks);
        for (size_t c = 0; c < numChunks && r.ok; ++c)
        {
            Chunk ch;
            ch.name   = r.str();
            ch.start  = static_cast<size_t> (r.u64());
            ch.length = static_cast<size_t> (r.u64());
            ch.track  = r.i32();
            ch.kind   = r.u8() == 1 ? Chunk::Kind::Clipping : Chunk::Kind::Chunk;
            w.chunks.push_back (std::move (ch));
        }

        const size_t numTracks = r.count (8);
        if (! r.ok) return false;
        w.tracks.reserve (numTracks);
        for (size_t t = 0; t < numTracks && r.ok; ++t)
        {
            Track tr;
            tr.outLeft  = r.u8() != 0;
            tr.outRight = r.u8() != 0;
            tr.volume   = r.i32();
            tr.data     = r.bytes();
            w.tracks.push_back (std::move (tr));
        }

        w.x       = r.f32();
        w.y       = r.f32();
        w.width   = r.f32();
        w.height  = r.f32();
        w.gridCol = r.i32();
        w.gridRow = r.i32();
        w.tint    = r.i32();

        w.gain   = r.f32();
        w.muted  = r.u8() != 0;
        w.soloed = r.u8() != 0;
        if (version >= 2)
        {
            w.balance = r.f32();
            w.slot    = r.i32();
        }

        // A depth outside 1..32 would make frameSize/getSample nonsense, so a blob
        // claiming one is not a blob we wrote. Same for a document with no track.
        if (w.bps < 1 || w.bps > 32 || w.tracks.empty()) return false;

        parsed.windows.push_back (std::move (w));
    }

    if (! r.ok) return false;

    out = std::move (parsed);
    return true;
}

// ============================ Engine <-> state ==============================

EngineState captureEngineState (Engine& engine)
{
    EngineState state;
    state.windows.reserve (engine.numWindows());

    for (uint64_t id : engine.windowIds())
    {
        auto* window = engine.windowById (id);
        const auto* doc = engine.editDoc (id);   // the UI document: ours to read
        if (window == nullptr || doc == nullptr) continue;

        WindowState w;
        w.name     = doc->name;
        w.rateHz   = doc->rateHz;
        w.bps      = doc->bps;
        w.baseFreq = doc->baseFreq;
        w.tracks   = doc->tracks;

        w.selSt         = doc->selSt;
        w.selEnd        = doc->selEnd;
        w.firstSelTrack = doc->firstSelTrack;
        w.lastSelTrack  = doc->lastSelTrack;
        w.loopSt        = doc->loopSt;
        w.loopEnd       = doc->loopEnd;
        w.chunks        = doc->chunks;

        w.x       = window->x;
        w.y       = window->y;
        w.width   = window->width;
        w.height  = window->height;
        w.gridCol = window->gridCol;
        w.gridRow = window->gridRow;
        w.tint    = window->tint;

        w.gain   = window->gain.load (std::memory_order_relaxed);
        w.muted  = window->muted.load (std::memory_order_relaxed);
        w.soloed = window->soloed.load (std::memory_order_relaxed);
        w.balance = window->balance.load (std::memory_order_relaxed);
        w.slot    = window->slot;

        state.windows.push_back (std::move (w));
    }

    return state;
}

bool applyEngineState (Engine& engine, const EngineState& state)
{
    // Close what is open first. windowIds() hands back the very vector
    // removeWindow() erases from, so take a copy before walking it.
    //
    // Worst case this posts kMaxWindows removes and kMaxWindows adds — 512
    // commands into a 1024-slot queue — so a load cannot overrun the queue even
    // when the audio thread is not yet running to drain it.
    const std::vector<uint64_t> existing = engine.windowIds();
    bool complete = true;
    for (uint64_t id : existing)
        complete = engine.removeWindow (id) && complete;

    for (const auto& w : state.windows)
    {
        DocumentInit init;
        init.name      = w.name;
        init.rateHz    = w.rateHz;
        init.bps       = w.bps;
        init.baseFreq  = w.baseFreq;
        init.numTracks = 0;          // the saved tracks go in below, lengths intact

        auto doc = std::make_unique<SoundDocument> (init);
        doc->tracks = w.tracks;
        doc->selSt  = w.selSt;
        doc->selEnd = w.selEnd;
        doc->loopSt = w.loopSt;
        doc->loopEnd = w.loopEnd;
        doc->chunks = w.chunks;

        // Clamp rather than trust: the track selection is a 1-based closed range
        // and everything downstream indexes with it.
        const int numTracks = doc->numTracks();
        doc->firstSelTrack = std::max (1, std::min (w.firstSelTrack, numTracks));
        doc->lastSelTrack  = std::max (doc->firstSelTrack, std::min (w.lastSelTrack, numTracks));

        const uint64_t id = engine.addWindow (std::move (doc));
        if (id == 0) { complete = false; continue; }   // full: keep what we can

        auto* window = engine.windowById (id);
        if (window == nullptr) { complete = false; continue; }

        window->x       = w.x;
        window->y       = w.y;
        window->width   = w.width;
        window->height  = w.height;
        window->gridCol = w.gridCol;
        window->gridRow = w.gridRow;
        window->tint    = w.tint;

        window->gain.store (w.gain, std::memory_order_relaxed);
        window->muted.store (w.muted, std::memory_order_relaxed);
        window->soloed.store (w.soloed, std::memory_order_relaxed);
        window->balance.store (std::clamp (w.balance, -1.0f, 1.0f), std::memory_order_relaxed);
        window->slot = w.slot >= 0 ? w.slot : Window::kNoSlot;
    }

    return complete;
}

} // namespace se
