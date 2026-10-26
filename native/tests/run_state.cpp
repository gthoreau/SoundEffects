// EngineState tests — the canvas survives a save/load round trip, and a blob we
// did not write cannot make us do anything.
//
// This is the test that stands behind the Options menu's "Save Canvas…" /
// "Open Canvas…": before the state format existed those items wrote an
// empty file and restored nothing, which is the worst thing a Save item can do.
// So the round trip is asserted field by field rather than by eyeball.
//
// JUCE-free, like everything else under Source/engine/.

#include "effects/Setups.h"
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
    wa->balance = -0.5f; wa->slot = 4;

    auto* wb = engine.windowById (b);
    wb->x = 900.0f; wb->y = 128.0f; wb->width = 240.0f; wb->height = 180.0f;
    wb->gridCol = 1; wb->gridRow = 0; wb->tint = 23;   // past the 0.3 palette's 12: the 0.4 table has 24
    wb->gain = 1.75f; wb->muted = false; wb->soloed = true;
    wb->balance = 1.0f; wb->slot = 0;
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
    check (wa->balance.load() == -0.5f && wb->balance.load() == 1.0f, "mixer channel: balance (v2)");
    check (wa->slot == 4 && wb->slot == 0, "mixer slot (v2)");
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

    // --- a v1 blob (0.3.x) still opens -----------------------------------------
    // v2 only APPENDS balance + slot to each Window. With one Window those are the
    // blob's last 8 bytes, so dropping them and stamping version 1 gives exactly
    // what 0.3 wrote; it must come back centred and unseated.
    {
        se::EngineState one;
        one.windows.push_back (state.windows[0]);
        auto v1 = se::serializeState (one);
        v1.resize (v1.size() - 8);
        v1[4] = 1;

        se::EngineState old;
        check (se::parseEngineState (v1.data(), v1.size(), old) && old.windows.size() == 1,
               "a v1 blob parses");
        check (! old.windows.empty() && old.windows[0].balance == 0.0f
                 && old.windows[0].slot == se::Window::kNoSlot
                 && old.windows[0].gain == 0.25f,
               "v1: centred balance, no slot, the rest intact");
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

    // --- effect setups (step 6): a duration is a time, not a byte count ---------------
    // 0.9.2 saved durations as bytes, so a setup changed speed with the sound's rate and
    // depth. Here it is kept in seconds and comes back as the nearest whole sample.
    {
        se::SetupKeys keys;
        keys.keys = { "flangePercent", "delay", "delayUnits", "rate.on", "a" };
        keys.durations = { "delay" };

        se::Settings working { { "flangePercent", se::Value ((int64_t) 70) },
                               { "delay", se::Value ((int64_t) 245) },          // 8-bit, 22254.5454 Hz
                               { "delayUnits", se::Value ((int64_t) se::Unit::Milliseconds) },
                               { "a", se::Value (0.4) },
                               { "unsaved", se::Value ((int64_t) 9) } };
        const se::Setup setup { "Test", se::captureSetup (working, keys, se::kRate22K, 8) };

        check (setup.values.count ("unsaved") == 0, "setup: only the module's saveable keys");
        check (setup.values.count ("rate.on") == 1 && setup.values.at ("rate.on").asBool(),
               "setup: an absent '.on' box is saved as on");
        check (std::abs (setup.values.at ("delay").asDouble() - 245.0 / se::kRate22K) < 1e-12,
               "setup: a duration is saved in seconds");
        check (se::setupMatches (setup, working, keys, se::kRate22K, 8), "setup: matches the values it came from");

        se::Settings other;
        se::applySetup (setup, other, keys, 44100.0, 16);
        check (other.at ("delay").asInt() == 2 * std::llround (245.0 / se::kRate22K * 44100.0),
               "setup: recalled at 44.1 kHz 16-bit, the same time in whole frames");
        check (other.at ("flangePercent").asInt() == 70 && other.at ("a").asDouble() == 0.4,
               "setup: other values recalled as they were");
        check (se::setupMatches (setup, other, keys, 44100.0, 16), "setup: and it matches there");

        // Every rate the app offers: seconds -> bytes -> seconds -> bytes is stable.
        bool stable = true;
        for (double rate : { 8000.0, 11025.0, 22050.0, se::kRate22K, 32000.0, 44100.0, 48000.0, 96000.0 })
            for (int bps : { 8, 16, 24 })
                for (int64_t frames : { 0, 1, 45, 245, 22255, 105710, 4410000 })
                {
                    const int64_t bytes = frames * se::frameSizeFor (bps);
                    stable = stable && se::setupBytes (se::setupSeconds (bytes, rate, bps), rate, bps) == bytes;
                }
        check (stable, "setup: durations round-trip to the same byte count at every rate and depth");

        other["flangePercent"] = se::Value ((int64_t) 71);
        check (! se::setupMatches (setup, other, keys, 44100.0, 16), "setup: an edited value is Custom again");
        other["flangePercent"] = se::Value ((int64_t) 70);
        other["delay"] = se::Value (other.at ("delay").asInt() + 2);
        check (! se::setupMatches (setup, other, keys, 44100.0, 16), "setup: so is a duration one sample off");

        const auto flanger = se::factorySetups ("Flanger");
        check (flanger.size() == 9 && flanger.front().name == "Plain Flange - Fast"
                   && flanger.back().name == "Distorto - Low",
               "setup: Flanger ships its nine 0.9.2 setups, in their order");
        se::Settings f;
        se::applySetup (flanger[1], f, { { "period" }, { "period" } }, se::kRate22K, 8);
        check (f.at ("period").asInt() == 22255, "setup: Plain Flange - Medium's period is 22255 bytes at 22 kHz 8-bit");
        check (se::factorySetups ("Low Pass IIR").size() == 3 && se::factorySetups ("High Pass IIR")[2].name == "Lower"
                   && se::factorySetups ("High Pass IIR")[2].values.at ("b").asDouble() == 0.7,
               "setup: the IIR filters ship Medium / Low / Lower");
        check (se::factorySetups ("Echo").empty(), "setup: the other modules ship none");
    }

    std::printf ("\n%s\n", failures == 0 ? "state: all checks passed"
                                         : (std::to_string (failures) + " FAILED").c_str());
    return failures == 0 ? 0 : 1;
}
