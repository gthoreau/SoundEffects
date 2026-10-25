// EngineState tests — the canvas survives a save/load round trip, and a blob we
// did not write cannot make us do anything.
//
// This is the test that stands behind the Options menu's "Save Canvas…" /
// "Open Canvas…": before the state format existed those items wrote an
// empty file and restored nothing, which is the worst thing a Save item can do.
// So the round trip is asserted field by field rather than by eyeball.
//
// JUCE-free, like everything else under Source/engine/.

#include "engine/Engine.h"
#include "engine/EngineState.h"
#include "engine/SampleFormat.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check (bool ok, const std::string& what)
{
    std::printf ("%-4s %s\n", ok ? "ok" : "FAIL", what.c_str());
    if (! ok) ++failures;
}

constexpr double kRate = 22254.5454;
constexpr int    kBlock = 64;

/** Ramp bytes, so a buffer that came back shifted or truncated is visible. */
std::unique_ptr<se::SoundDocument> rampDocument (const char* name, int numTracks, size_t bytes)
{
    se::DocumentInit init;
    init.name = name;
    init.numTracks = numTracks;
    init.bps = 8;
    init.rateHz = kRate;
    init.sizeBytes = bytes;

    auto doc = std::make_unique<se::SoundDocument> (init);
    for (size_t t = 0; t < doc->tracks.size(); ++t)
    {
        auto& data = doc->tracks[t].data;
        for (size_t i = 0; i < data.size(); ++i)
            se::setSample (data.data(), i, (int32_t) ((i + t * 7) % 100) - 50, 8);
    }
    return doc;
}

/** The canvas the round trip has to carry: two Windows, nothing at its default. */
void buildCanvas (se::Engine& engine)
{
    const uint64_t a = engine.addWindow (rampDocument ("kick loop", 2, 300));
    const uint64_t b = engine.addWindow (rampDocument ("hat", 1, 128));
    check (a != 0 && b != 0, "two Windows on the canvas to save");

    auto* doc = engine.editDoc (a);
    doc->baseFreq = 48;
    doc->selSt = 16;  doc->selEnd = 96;
    doc->firstSelTrack = 2; doc->lastSelTrack = 2;
    doc->loopSt = 32; doc->loopEnd = 160;
    doc->chunks.push_back ({ "verse", 8, 40, 1, se::Chunk::Kind::Chunk });
    doc->chunks.push_back ({ "clip",  50, 4, 2, se::Chunk::Kind::Clipping });

    // Independent track lengths are the load-bearing weirdness (Track.h): a
    // round trip that quietly padded them to match would break the model.
    doc->tracks[1].data.resize (211);
    doc->tracks[0].outRight = false;          // left only
    doc->tracks[1].volume = 0x0180;           // 1.5 in 8.8 fixed
    engine.commitEdit (a);

    auto* wa = engine.windowById (a);
    wa->x = 512.0f; wa->y = 640.0f; wa->width = 380.0f; wa->height = 220.0f;
    wa->gridCol = -2; wa->gridRow = 3; wa->tint = 7;
    wa->gain = 0.25f; wa->muted = true; wa->soloed = false;

    auto* wb = engine.windowById (b);
    wb->x = 900.0f; wb->y = 128.0f; wb->width = 240.0f; wb->height = 180.0f;
    wb->gridCol = 1; wb->gridRow = 0; wb->tint = 23;   // past the 0.3 palette's 12: the 0.4 table has 24
    wb->gain = 1.75f; wb->muted = false; wb->soloed = true;
}

void checkRestored (se::Engine& engine)
{
    const auto& ids = engine.windowIds();
    check (ids.size() == 2, "two Windows came back");
    if (ids.size() != 2) return;

    auto* wa = engine.windowById (ids[0]);
    auto* wb = engine.windowById (ids[1]);
    auto* da = engine.editDoc (ids[0]);
    auto* db = engine.editDoc (ids[1]);
    check (wa && wb && da && db, "and each has a Window and a UI document");
    if (! (wa && wb && da && db)) return;

    check (da->name == "kick loop" && db->name == "hat", "names, in creation order");
    check (da->rateHz == kRate && da->bps == 8 && da->baseFreq == 48, "rate / depth / base note");

    check (da->numTracks() == 2 && db->numTracks() == 1, "track counts");
    check (da->tracks[0].data.size() == 300 && da->tracks[1].data.size() == 211,
           "tracks keep their OWN independent lengths");

    bool bytesMatch = true;
    for (size_t t = 0; t < da->tracks.size(); ++t)
        for (size_t i = 0; i < da->tracks[t].data.size(); ++i)
            if (i < 128 && da->tracks[t].data[i]
                            != (uint8_t) (((int32_t) ((i + t * 7) % 100) - 50) ^ 0x80))
                bytesMatch = false;
    check (bytesMatch, "the audio itself is byte-identical");

    check (! da->tracks[0].outRight && da->tracks[0].outLeft, "output channel routing");
    check (da->tracks[1].volume == 0x0180, "per-track volume");

    check (da->selSt == 16 && da->selEnd == 96
            && da->firstSelTrack == 2 && da->lastSelTrack == 2, "selection rectangle");
    check (da->loopSt == 32 && da->loopEnd == 160, "loop region");
    check (da->chunks.size() == 2 && da->chunks[0].name == "verse"
            && da->chunks[1].kind == se::Chunk::Kind::Clipping, "chunks");

    check (wa->x == 512.0f && wa->y == 640.0f && wa->width == 380.0f && wa->height == 220.0f,
           "canvas placement and size");
    check (wa->gridCol == -2 && wa->gridRow == 3, "grid cell, negative column and all");
    check (wa->tint == 7 && wb->tint == 23, "tints");

    check (wa->gain.load() == 0.25f && wa->muted.load() && ! wa->soloed.load(),
           "mixer channel: gain and mute");
    check (wb->gain.load() == 1.75f && wb->soloed.load(), "mixer channel: solo");
}

/** A blob the reader must refuse — without reading past it and without crashing. */
void checkRejects (const std::vector<uint8_t>& blob, const std::string& what)
{
    se::EngineState out;
    out.windows.resize (3);   // must be left alone on refusal
    const bool parsed = se::parseEngineState (blob.data(), blob.size(), out);
    check (! parsed && out.windows.size() == 3, "refuses " + what);
}

} // namespace

int main()
{
    // --- capture -> serialize -> parse -> apply, into a DIFFERENT engine ------
    std::vector<uint8_t> blob;
    {
        se::Engine engine;
        engine.prepare (kRate, kBlock);
        buildCanvas (engine);
        blob = se::serializeEngine (engine);
    }
    check (! blob.empty(), "the state blob is not empty");

    se::EngineState state;
    check (se::parseEngineState (blob.data(), blob.size(), state), "it parses back");
    check (state.windows.size() == 2, "two Windows in the parsed state");

    {
        se::Engine engine;
        engine.prepare (kRate, kBlock);

        // The processor seeds one Window at construction; a load must REPLACE the
        // canvas, not append to it. This is the stray-untitled-window bug.
        engine.addWindow (rampDocument ("untitled sound #1", 1, 0));
        check (se::applyEngineState (engine, state), "applies onto a busy canvas");
        checkRestored (engine);
    }

    // Serializing what we parsed must give the same bytes back: the format has no
    // field the writer knows and the reader drops.
    {
        check (se::serializeState (state) == blob, "re-serializing is byte-identical");
    }

    // --- a load leaves the engine usable ------------------------------------
    {
        se::Engine engine;
        engine.prepare (kRate, kBlock);
        se::applyEngineState (engine, state);

        se::PlayOptions loop;
        loop.loop = true;
        for (uint64_t id : engine.windowIds()) engine.play (id, loop);

        std::vector<float> l ((size_t) kBlock, 0.0f), r ((size_t) kBlock, 0.0f);
        for (int i = 0; i < 8; ++i) engine.render (l.data(), r.data(), kBlock);

        auto* w = engine.windowById (engine.windowIds()[1]);   // "hat": audible
        check (w->transport->uiPlayhead() > 0, "a restored Window plays");
        engine.collectRetired();
    }

    // --- blobs we did not write ---------------------------------------------
    checkRejects ({}, "an empty blob");
    checkRejects ({ 1, 2, 3 }, "a blob too short to hold a header");
    checkRejects ({ 'N','O','P','E', 1,0,0,0, 0,0,0,0 }, "a bad magic");

    {
        auto future = blob;
        future[4] = (uint8_t) (se::kEngineStateVersion + 1);
        checkRejects (future, "a version from the future");
    }
    {
        auto zero = blob;
        zero[4] = 0;
        checkRejects (zero, "version zero");
    }
    {
        std::vector<uint8_t> truncated (blob.begin(), blob.begin() + (long) blob.size() / 2);
        checkRejects (truncated, "a truncated blob");
    }
    {
        // A length field claiming more than the blob could possibly hold must be
        // refused BEFORE anything is resized to match it.
        auto huge = blob;
        for (int i = 0; i < 8; ++i) huge[12 + (size_t) i] = 0xff;   // the first name's length
        checkRejects (huge, "an absurd length field");
    }
    {
        auto header = blob;
        header.resize (12);   // header promising windows that are not there
        checkRejects (header, "a header with no Windows behind it");
    }

    // Corruption is not always tidy: a state file can be truncated by a full disk
    // or scrambled by whatever else wrote to it. Every damaged blob has to end in
    // "no" or in a state we could have written — never in a crash or a wild
    // allocation. Deterministic PRNG so a failure here is reproducible.
    {
        uint32_t seed = 0x5eed1234u;
        auto nextRandom = [&seed] { seed = seed * 1664525u + 1013904223u; return seed >> 8; };

        bool survived = true;
        for (int trial = 0; trial < 4000; ++trial)
        {
            auto damaged = blob;
            const int edits = 1 + (int) (nextRandom() % 4);
            for (int e = 0; e < edits; ++e)
                damaged[nextRandom() % damaged.size()] = (uint8_t) (nextRandom() & 0xff);

            if (nextRandom() % 4 == 0)                     // and sometimes cut it short
                damaged.resize (nextRandom() % damaged.size());

            se::EngineState scrambled;
            if (se::parseEngineState (damaged.data(), damaged.size(), scrambled))
                for (const auto& w : scrambled.windows)    // if it says yes, it must mean it
                    if (w.bps < 1 || w.bps > 32 || w.tracks.empty())
                        survived = false;
        }
        check (survived, "4000 damaged blobs: refused or sane, never in between");
    }

    std::printf ("\n%s\n", failures == 0 ? "state: all checks passed"
                                         : (std::to_string (failures) + " FAILED").c_str());
    return failures == 0 ? 0 : 1;
}
