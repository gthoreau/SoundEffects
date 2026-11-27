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

#include "engine/Project.h"
#include "engine/Wav.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
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
    engine.setDirection (a, se::PlayDirection::pingPong);

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
    check (wa->slot == 1 && wb->slot == 0, "mixer slot (v2): order kept, the gap closed on load");
    check (wa->direction.load() == (uint8_t) se::PlayDirection::pingPong
             && wb->direction.load() == (uint8_t) se::PlayDirection::forward,
           "playback direction per Window (v3)");
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

    // --- v1 (0.3.x) and v2 (0.4 dev) blobs still open ---------------------------
    // v2 appended balance + slot to each Window, v3 a header byte (audio mode) and
    // the direction. With one Window those are at known places, so cutting them and
    // stamping the older version gives exactly what the older writer produced.
    se::EngineState one;
    one.windows.push_back (state.windows[0]);
    const auto v3 = se::serializeState (one);
    auto toV2 = [] (std::vector<uint8_t> b)
    {
        b.pop_back();              // direction
        b.erase (b.begin() + 8);   // audio mode
        b[4] = 2;
        return b;
    };
    {
        auto v2 = toV2 (v3);
        se::EngineState old;
        check (se::parseEngineState (v2.data(), v2.size(), old) && old.windows.size() == 1,
               "a v2 blob parses");
        check (! old.windows.empty() && old.windows[0].balance == -0.5f && old.windows[0].slot == 4
                 && old.windows[0].direction == 0 && ! old.externalAudio,
               "v2: balance and slot kept, direction Forward");

        auto v1 = v2;
        v1.resize (v1.size() - 8);
        v1[4] = 1;
        se::EngineState older;
        check (se::parseEngineState (v1.data(), v1.size(), older) && older.windows.size() == 1,
               "a v1 blob parses");
        check (! older.windows.empty() && older.windows[0].balance == 0.0f
                 && older.windows[0].slot == se::Window::kNoSlot
                 && older.windows[0].gain == 0.25f && older.sourceVersion == 1,
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
        for (int i = 0; i < 8; ++i) huge[13 + (size_t) i] = 0xff;   // the first name's length
        checkRejects (huge, "an absurd length field");
    }
    {
        auto header = blob;
        header.resize (13);   // header promising windows that are not there
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

    // --- WAV assets: lossless at every depth ---------------------------------------
    {
        bool all = true;
        for (int bps : { 1, 8, 12, 16, 17, 24, 32 })
        {
            const int fs = se::frameSizeFor (bps);
            se::Track t;
            for (size_t i = 0; i < (size_t) fs * 97 + 1; ++i)      // a ragged tail on purpose
                t.data.push_back ((uint8_t) (i * 37 + 11));
            std::vector<uint8_t> wav, back;
            se::encodeTrackWav (t, 22254.5454, bps, wav);
            all = all && se::decodeTrackWav (wav.data(), wav.size(), bps, t.data.size(), back) && back == t.data;
            all = all && ! se::decodeTrackWav (wav.data(), wav.size(), bps, t.data.size() + 1, back);
            if (fs != 3) all = all && ! se::decodeTrackWav (wav.data(), wav.size(), fs == 1 ? 16 : 8, t.data.size(), back);
        }
        check (all, "wav: every depth round-trips byte for byte; a wrong length or depth is refused");

        se::Track empty;
        std::vector<uint8_t> wav, back { 1 };
        se::encodeTrackWav (empty, 44100.0, 16, wav);
        check (se::decodeTrackWav (wav.data(), wav.size(), 16, 0, back) && back.empty(), "wav: an empty Track");
        check (wav[24] == 0x44 && wav[25] == 0xac, "wav: the header says 44100 Hz");
    }

    // --- the project folder, through a real directory ------------------------------
    namespace fsys = std::filesystem;
    const fsys::path root = fsys::temp_directory_path() / "se-state-tests-project";
    fsys::remove_all (root);
    {
        se::EngineState canvas = state;
        canvas.windows[0].rateHz = 22254.5454;                // not an integer
        canvas.windows[1].bps = 12;                            // a depth with a padded container
        canvas.windows[1].tracks[0].data.resize (128 * 2);
        for (size_t i = 0; i < canvas.windows[1].tracks[0].data.size(); ++i)
            canvas.windows[1].tracks[0].data[i] = (uint8_t) (i * 5 + 3);

        const fsys::path dir = root / "My Canvas";
        std::string err;
        check (se::writeProject (dir, "My Canvas", canvas, err), "project: written (" + err + ")");
        check (fsys::exists (dir / "My Canvas.sefx") && fsys::is_directory (dir / "Assets"),
               "project: <Name>/<Name>.sefx and Assets/");
        size_t wavs = 0;
        for (auto& e : fsys::directory_iterator (dir / "Assets")) wavs += e.path().extension() == ".wav";
        check (wavs == 3, "project: one WAV per Track (2 + 1)");
        check (se::isProjectFile (dir / "My Canvas.sefx"), "project: recognised as a project");
        check (fsys::file_size (dir / "My Canvas.sefx") < 1000, "project: the .sefx holds no audio");

        se::EngineState back;
        check (se::readSefx (dir / "My Canvas.sefx", back, err), "project: read back (" + err + ")");
        bool same = back.windows.size() == canvas.windows.size();
        for (size_t i = 0; same && i < back.windows.size(); ++i)
        {
            const auto& a = canvas.windows[i];
            const auto& b = back.windows[i];
            same = same && a.name == b.name && a.rateHz == b.rateHz && a.bps == b.bps
                 && a.tracks.size() == b.tracks.size() && a.direction == b.direction
                 && a.slot == b.slot && a.tint == b.tint && a.gain == b.gain;
            for (size_t t = 0; same && t < a.tracks.size(); ++t)
                same = a.tracks[t].data == b.tracks[t].data && a.tracks[t].volume == b.tracks[t].volume
                    && a.tracks[t].outRight == b.tracks[t].outRight;
        }
        check (same, "project: exact rate, true depth, ragged lengths, audio bytes and mixer all identical");

        // Saving again under a new Sound name replaces its stems and removes the old ones.
        canvas.windows[1].name = "snare";
        check (se::writeProject (dir, "My Canvas", canvas, err), "project: saved over itself");
        bool old = false, fresh = false;
        for (auto& e : fsys::directory_iterator (dir / "Assets"))
        {
            old = old || e.path().filename().string().find ("hat") != std::string::npos;
            fresh = fresh || e.path().filename().string().find ("snare") != std::string::npos;
        }
        check (! old && fresh, "project: a renamed Sound's old stem is removed, the new one written");
        { std::ofstream(dir / "Assets" / "mine.wav") << "x"; }
        se::writeProject (dir, "My Canvas", canvas, err);
        check (fsys::exists (dir / "Assets" / "mine.wav"), "project: a file it did not write is left alone");

        // Damaged projects say so instead of opening empty.
        const fsys::path any = dir / "Assets" / "01 kick loop - track 1.wav";
        fsys::remove (any);
        check (! se::readSefx (dir / "My Canvas.sefx", back, err) && err.find ("missing") != std::string::npos,
               "project: a missing asset is reported");
        se::writeProject (dir, "My Canvas", canvas, err);
        { std::ofstream (any, std::ios::binary | std::ios::trunc) << "RIFF"; }
        check (! se::readSefx (dir / "My Canvas.sefx", back, err), "project: a damaged asset is refused");

        // A hand-edited .sefx pointing out of Assets/ is not followed.
        se::EngineState evil = canvas;
        evil.externalAudio = true;
        for (auto& w : evil.windows)
        {
            w.assetPaths.assign (w.tracks.size(), "Assets/../../secret.wav");
            w.assetBytes.assign (w.tracks.size(), 0);
        }
        const auto eb = se::serializeState (evil);
        { std::ofstream f (dir / "Evil.sefx", std::ios::binary); f.write ((const char*) eb.data(), (std::streamsize) eb.size()); }
        check (! se::readSefx (dir / "Evil.sefx", back, err), "project: an asset path that climbs out of Assets/ is refused");
    }

    // --- asset ids (0.5.0 step 8): stems keep their names ---------------------------
    {
        const auto stems = [] (const fsys::path& dir)
        {
            std::set<std::string> names;
            for (auto& e : fsys::directory_iterator (dir / "Assets")) names.insert (e.path().filename().string());
            return names;
        };
        const auto has = [] (const std::set<std::string>& s, const std::string& n) { return s.count (n) == 1; };
        std::string err;
        const fsys::path dir = root / "Ids";

        se::Engine engine;
        const uint64_t a = engine.addWindow (rampDocument ("alpha", 1, 64));
        const uint64_t b = engine.addWindow (rampDocument ("beta", 2, 64));
        const uint64_t c = engine.addWindow (rampDocument ("gamma", 1, 64));
        check (engine.windowById (a)->assetId == 1 && engine.windowById (b)->assetId == 2
                 && engine.windowById (c)->assetId == 3 && engine.nextAssetId() == 4,
               "asset ids: new Sounds get 1, 2, 3 from the canvas's counter");

        check (se::writeProject (dir, "Ids", se::captureEngineState (engine), err), "asset ids: first save");
        const auto first = stems (dir);
        check (first.size() == 4 && has (first, "01 alpha - track 1.wav") && has (first, "02 beta - track 2.wav")
                 && has (first, "03 gamma - track 1.wav"),
               "asset ids: stems are named <id> <name> - track T");

        // Reorder the mixer strips: gamma to the top, alpha to the bottom.
        engine.moveToSlot (c, 0);
        engine.moveToSlot (a, 99);
        check (engine.windowById (c)->slot == 0 && engine.windowById (a)->slot == 2, "asset ids: strips reordered");
        check (se::writeProject (dir, "Ids", se::captureEngineState (engine), err) && stems (dir) == first,
               "asset ids: reordering mixer strips then saving renames nothing");

        // Close beta: the others keep their names, beta's stems go; a new Sound never reuses 2.
        engine.removeWindow (b);
        const uint64_t d = engine.addWindow (rampDocument ("delta", 1, 64));
        check (engine.windowById (d)->assetId == 4, "asset ids: a closed Sound's id is not reused");
        check (se::writeProject (dir, "Ids", se::captureEngineState (engine), err), "asset ids: saved after a close");
        const auto after = stems (dir);
        check (after.size() == 3 && has (after, "01 alpha - track 1.wav") && has (after, "03 gamma - track 1.wav")
                 && has (after, "04 delta - track 1.wav") && ! has (after, "02 beta - track 1.wav"),
               "asset ids: closing a Sound renames none of the others and removes its own stems");

        // Open it again: ids and the counter come back with the canvas.
        se::EngineState back;
        check (se::readSefx (dir / "Ids.sefx", back, err) && back.nextAssetId == 5, "asset ids: the counter is saved");
        se::Engine reopened;
        se::applyEngineState (reopened, back);
        std::vector<uint32_t> ids;
        for (auto id : reopened.windowIds()) ids.push_back (reopened.windowById (id)->assetId);
        check (ids == std::vector<uint32_t> { 1, 3, 4 } && reopened.nextAssetId() == 5, "asset ids: restored on open");
        check (reopened.windowById (reopened.addWindow (rampDocument ("epsilon", 1, 8)))->assetId == 5,
               "asset ids: the next new Sound after an open carries on from the saved counter");
        check (se::writeProject (dir, "Ids", se::captureEngineState (reopened), err)
                 && stems (dir).size() == 4 && has (stems (dir), "01 alpha - track 1.wav"),
               "asset ids: saving the reopened canvas renames nothing");

        // Import Sound: the imported one is new to this canvas, whatever id it had.
        se::EngineState imp = back;
        imp.windows.resize (1);   // alpha, id 1
        se::importEngineState (reopened, imp);
        check (reopened.windowById (reopened.windowIds().back())->assetId == 6, "asset ids: an import gets a fresh id");

        // An older file (no ids, no counter): ids are handed out in file order, so
        // they match the 01, 02... its stems were written with: nothing renames.
        se::EngineState old = back;
        old.nextAssetId = 0;
        for (auto& w : old.windows) w.assetId = 0;
        se::Engine migrated;
        se::applyEngineState (migrated, old);
        ids.clear();
        for (auto id : migrated.windowIds()) ids.push_back (migrated.windowById (id)->assetId);
        check (ids == std::vector<uint32_t> { 1, 2, 3 } && migrated.nextAssetId() == 4,
               "asset ids: an older project gets ids 1..n in file order on open");

        // A hand-edited file repeating an id: two Sounds never share stems.
        se::EngineState twin = back;
        twin.windows[1].assetId = twin.windows[0].assetId;
        se::Engine twins;
        se::applyEngineState (twins, twin);
        check (twins.windowById (twins.windowIds()[0])->assetId != twins.windowById (twins.windowIds()[1])->assetId,
               "asset ids: a repeated id gets a fresh one on open");
        const fsys::path tdir = root / "Twins";
        check (se::writeProject (tdir, "Twins", twin, err) && stems (tdir).size() == 3,
               "asset ids: a state repeating an id still writes one stem per Track");

        // A blob claiming an absurd id is read as none.
        se::EngineState huge = back;
        huge.windows[0].assetId = 0xFFFFFFFFu;
        huge.nextAssetId = 0xFFFFFFFFu;
        const auto hb = se::serializeState (huge);
        se::EngineState hp;
        check (se::parseEngineState (hb.data(), hb.size(), hp) && hp.windows[0].assetId == 0 && hp.nextAssetId == 0,
               "asset ids: an out-of-range id or counter is read as none");
    }

    // --- Main Level (v5): the master gain is saved with the canvas ----------------
    {
        se::Engine e;
        e.addWindow (rampDocument ("alpha", 1, 64));
        check (se::captureEngineState (e).masterGain == 1.0f, "master: a new canvas is at unity");

        e.setMasterGain (0.4f);
        const auto blob5 = se::serializeEngine (e);
        se::EngineState back5;
        check (se::parseEngineState (blob5.data(), blob5.size(), back5) && back5.masterGain == 0.4f
                 && back5.sourceVersion == se::kEngineStateVersion,
               "master: the gain round-trips through the blob");

        e.setMasterMuted (true);
        check (se::serializeEngine (e) == blob5, "main mute: not saved");

        se::Engine again;
        again.setMasterGain (1.0f);
        again.setMasterMuted (true);
        check (se::applyEngineState (again, back5) && again.masterGain() == 0.4f,
               "master: opening a canvas restores the gain");
        check (! again.masterMuted(), "main mute: a canvas opens unmuted");

        // A canvas with no Sound (a new one, 0.6.0) round-trips and empties the
        // canvas it is opened over.
        {
            se::Engine none;
            const auto blobNone = se::serializeEngine (none);
            se::EngineState backNone;
            check (se::parseEngineState (blobNone.data(), blobNone.size(), backNone) && backNone.windows.empty(),
                   "empty canvas: parses with no Sound");
            se::Engine full;
            full.addWindow (std::make_unique<se::SoundDocument> (se::newSoundInit()));
            check (se::applyEngineState (full, backNone) && full.numWindows() == 0 && full.windowIds().empty(),
                   "empty canvas: opening it leaves no Sound");
        }

        // Opening a canvas saved at unity puts a lowered one back to unity.
        se::Engine lowered;
        lowered.setMasterGain (0.2f);
        se::EngineState unity = back5;
        unity.masterGain = 1.0f;
        check (se::applyEngineState (lowered, unity) && lowered.masterGain() == 1.0f,
               "master: a canvas saved at unity opens at unity");

        // A v4 blob (written by 8.1) has no gain: it opens at unity, ids intact.
        se::EngineState v4s = back5;
        v4s.windows[0].assetId = 7;
        v4s.nextAssetId = 8;
        auto b4 = se::serializeState (v4s);
        b4.resize (b4.size() - 4);   // the gain
        b4[4] = 4;
        se::EngineState p4;
        check (se::parseEngineState (b4.data(), b4.size(), p4) && p4.masterGain == 1.0f
                 && p4.nextAssetId == 8 && p4.windows[0].assetId == 7,
               "master: a v4 file opens at unity with its asset ids");

        // Importing a Sound never touches the Main Level.
        se::Engine importer;
        importer.setMasterGain (0.3f);
        se::importEngineState (importer, back5);
        check (importer.masterGain() == 0.3f, "master: Import Sound leaves Main Level alone");

        // Nonsense in the file reads as unity.
        for (float bad : { -1.0f, 9.0f, std::numeric_limits<float>::quiet_NaN() })
        {
            se::EngineState junk = back5;
            junk.masterGain = bad;
            const auto jb = se::serializeState (junk);
            se::EngineState jp;
            check (se::parseEngineState (jb.data(), jb.size(), jp) && jp.masterGain == 1.0f,
                   "master: an out-of-range gain in the file reads as unity");
        }
    }

    // --- New Sound defaults (10.4) --------------------------------------------------
    {
        se::SoundDocument fresh (se::newSoundInit());
        check (fresh.numTracks() == 1 && fresh.rateHz == 44100.0 && fresh.bps == 16
                 && fresh.tracks[0].data.empty() && fresh.name == "untitled sound",
               "new Sound: one empty Track, 44.1 kHz, 16 bit");
        se::DocumentInit legacy;
        check (legacy.rateHz == 22254.5454 && legacy.bps == 8, "DocumentInit's own defaults are untouched");
    }

    // --- Export Sound / Import Sound -----------------------------------------------
    {
        std::string err;
        const fsys::path file = root / "kick.sefx";
        check (se::writeSoundExport (file, state.windows[0], err), "export: written");
        check (! se::isProjectFile (file), "export: not a project (audio embedded)");

        se::EngineState ex;
        check (se::readSefx (file, ex, err) && ex.windows.size() == 1, "export: reads as a one-Sound .sefx");
        check (ex.windows[0].tracks[1].data.size() == 211 && ex.windows[0].slot == se::Window::kNoSlot
                 && ! ex.windows[0].soloed && ex.windows[0].direction == state.windows[0].direction,
               "export: audio embedded, slot and solo dropped, direction kept");

        // Import onto a canvas whose slots 0 and 2 are taken.
        se::Engine engine;
        engine.prepare (kRate, kBlock);
        const uint64_t p = engine.addWindow (rampDocument ("p", 1, 16));
        const uint64_t q = engine.addWindow (rampDocument ("q", 1, 16));
        const uint64_t r = engine.addWindow (rampDocument ("r", 1, 16));
        (void) q;
        engine.windowById (p)->slot = 0;
        engine.windowById (q)->slot = 2;
        engine.windowById (r)->slot = 3;

        se::EngineState two = ex;
        two.windows.push_back (ex.windows[0]);
        two.windows[1].name = "kick 2";
        check (se::importEngineState (engine, two), "import: applied");
        check (engine.windowIds().size() == 5, "import: added to the canvas, nothing replaced");
        auto* a = engine.windowById (engine.windowIds()[3]);
        auto* b = engine.windowById (engine.windowIds()[4]);
        check (a && b && a->slot == 1 && b->slot == 4, "import: lands in the first free slots (1, then 4)");
        check (a && a->x == 0.0f && a->gridCol == se::Window::kNoCell, "import: unplaced, the canvas places it");
    }
    fsys::remove_all (root);

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

        // Manage Setup's rename (0.6.0 step 5.3).
        const auto named = [] (std::initializer_list<const char*> names)
        {
            std::vector<se::Setup> v;
            int n = 0;
            for (const char* name : names) v.push_back ({ name, { { "n", se::Value ((int64_t) n++) } } });
            return v;
        };
        const auto reads = [] (const std::vector<se::Setup>& v, std::initializer_list<std::pair<const char*, int>> want)
        {
            if (v.size() != want.size()) return false;
            size_t i = 0;
            for (const auto& [name, n] : want)
            {
                if (v[i].name != name || v[i].values.at ("n").asInt() != n) return false;
                ++i;
            }
            return true;
        };
        auto list = named ({ "a", "b", "c" });
        check (se::renameSetup (list, 1, "d") == 1 && reads (list, { { "a", 0 }, { "d", 1 }, { "c", 2 } }),
               "setup: rename keeps its place and values");
        check (se::renameSetup (list, 2, "a") == 1 && reads (list, { { "d", 1 }, { "a", 2 } }),
               "setup: rename onto an earlier name replaces it");
        check (se::renameSetup (list, 0, "a") == 0 && reads (list, { { "a", 1 } }),
               "setup: rename onto a later name replaces it");
        check (se::renameSetup (list, 0, "a") == 0 && list.size() == 1, "setup: renaming to itself changes nothing");
        check (se::renameSetup (list, 0, "") == -1 && se::renameSetup (list, 3, "x") == -1
                   && reads (list, { { "a", 1 } }),
               "setup: an empty name or a bad index changes nothing");
    }

    std::printf ("\n%s\n", failures == 0 ? "state: all checks passed"
                                         : (std::to_string (failures) + " FAILED").c_str());
    return failures == 0 ? 0 : 1;
}
