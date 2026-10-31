// Engine tests — the desktop-wide mixer: per-Window gain, balance, mute, the
// CUMULATIVE solo filter and the stereo VU feed, plus the rule that none of
// them may stop a transport.
//
// These are the parts of the Engine that have no golden vector to check them
// against (there is no 1994 original for a mixer that never existed), so they are
// asserted directly. JUCE-free, like everything else under Source/engine/.

#include "engine/Edit.h"
#include "engine/Engine.h"
#include "engine/RecordConvert.h"
#include "engine/Recorder.h"
#include "engine/Resample.h"
#include "engine/SampleFormat.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <thread>
#include <vector>

namespace {

int failures = 0;

void check (bool ok, const std::string& what)
{
    std::printf ("%-4s %s\n", ok ? "ok" : "FAIL", what.c_str());
    if (! ok) ++failures;
}

void checkClose (float got, float want, float tol, const std::string& what)
{
    const bool ok = std::fabs (got - want) <= tol;
    std::printf ("%-4s %-52s got %+.5f want %+.5f\n", ok ? "ok" : "FAIL", what.c_str(), got, want);
    if (! ok) ++failures;
}

constexpr double kRate = 22254.5454;   // device rate == document rate: 1 byte per frame
constexpr int    kBlock = 64;

/** A one-track Window holding constant DC at `level` (8-bit sample units). */
std::unique_ptr<se::SoundDocument> dcDocument (int level, const char* name)
{
    se::DocumentInit init;
    init.name = name;
    init.numTracks = 1;
    init.bps = 8;
    init.rateHz = kRate;
    init.sizeBytes = 512;

    auto doc = std::make_unique<se::SoundDocument> (init);
    auto& data = doc->tracks[0].data;
    for (size_t i = 0; i < data.size(); ++i)
        se::setSample (data.data(), i, level, 8);
    return doc;
}

/** Render one block and return the first left-channel sample. The audio is DC, so
    one sample IS the level — no windowing or averaging to get wrong. */
float renderLevel (se::Engine& engine)
{
    std::vector<float> l ((size_t) kBlock, 0.0f), r ((size_t) kBlock, 0.0f);
    engine.render (l.data(), r.data(), kBlock);
    return l[0];
}

void renderBlocks (se::Engine& engine, int blocks)
{
    std::vector<float> l ((size_t) kBlock, 0.0f), r ((size_t) kBlock, 0.0f);
    for (int i = 0; i < blocks; ++i) engine.render (l.data(), r.data(), kBlock);
}

// --- playback direction ------------------------------------------------------
// Driven straight through a Transport at the document's own rate (one frame of
// output per frame of audio), over a ramp so the sample READS BACK as its index.

constexpr int kRamp = 100;

/** One ramp track per entry of `lens` (8-bit, sample i holds i - 50); track 0 is
    routed left only, track 1 right only, so each can be read apart. */
std::unique_ptr<se::SoundDocument> rampDocument (std::initializer_list<size_t> lens)
{
    se::DocumentInit init;
    init.numTracks = (int) lens.size();
    init.bps = 8;
    init.rateHz = kRate;
    init.sizeBytes = 1;
    auto doc = std::make_unique<se::SoundDocument> (init);
    int t = 0;
    for (size_t len : lens)
    {
        auto& tr = doc->tracks[(size_t) t];
        tr.data.assign (len, 0);
        for (size_t i = 0; i < len; ++i) se::setSample (tr.data.data(), i, (int) i - 50, 8);
        tr.outLeft  = t == 0 || lens.size() == 1;
        tr.outRight = t == 1 || lens.size() == 1;
        ++t;
    }
    return doc;
}

int indexOf (float v) { return (int) std::lround (v * 128.0f) + 50; }

/** Render `frames` frames and return the frame index each sample came from
    (-1 where the track was silent, which a ramp at index 50 would not hide:
    index 50 reads 0.0, so silence is told apart by the rendered count instead). */
struct Rendered { std::vector<int> left, right; std::vector<float> rawL; int frames = 0; };
Rendered renderTransport (se::Transport& tr, const se::SoundDocument& doc, int frames)
{
    std::vector<float> l ((size_t) frames, 0.0f), r ((size_t) frames, 0.0f);
    Rendered out;
    out.frames = tr.render (doc, l.data(), r.data(), frames);
    out.rawL = l;
    for (int i = 0; i < out.frames; ++i) { out.left.push_back (indexOf (l[(size_t) i])); out.right.push_back (indexOf (r[(size_t) i])); }
    return out;
}

bool isSequence (const std::vector<int>& got, const std::vector<int>& want)
{
    return got == want;
}

/** Expected indices: `n` frames of a span [lo, lo+S), starting at span index `from`,
    heading by `dir` (+1/-1), wrapping (loop) or reflecting (pingpong). */
std::vector<int> walk (int lo, int S, int from, int heading, bool pingPong, int n)
{
    std::vector<int> v; int i = from;
    for (int k = 0; k < n; ++k)
    {
        v.push_back (lo + i);
        i += heading;
        if (pingPong) { if (i >= S) { i = S - 1; heading = -1; } else if (i < 0) { i = 0; heading = 1; } }
        else          { if (i >= S) i = 0; else if (i < 0) i = S - 1; }
    }
    return v;
}

void directionTests()
{
    using se::PlayDirection;
    se::PlayOptions loop; loop.loop = true;

    // Forward is what it always was.
    {
        auto doc = rampDocument ({ kRamp });
        se::Transport tr (kRate);
        tr.play (*doc, loop);
        check (isSequence (renderTransport (tr, *doc, 250).left, walk (0, kRamp, 0, 1, false, 250)),
               "direction: forward loops 0..99 as always");
    }
    // Backward and ping-pong, looping the whole sound.
    for (int pp = 0; pp < 2; ++pp)
    {
        auto doc = rampDocument ({ kRamp });
        se::Transport tr (kRate);
        tr.setDirection (pp ? PlayDirection::pingPong : PlayDirection::backward, *doc);   // stopped: kept
        tr.play (*doc, loop);
        const auto want = pp ? walk (0, kRamp, 0, 1, true, 450) : walk (0, kRamp, kRamp - 1, -1, false, 250);
        check (isSequence (renderTransport (tr, *doc, (int) want.size()).left, want),
               pp ? "direction: ping-pong runs up, bounces, runs down, bounces" : "direction: backward loops 99..0");
    }
    // Tracks of different lengths each keep their own length and phase.
    {
        auto doc = rampDocument ({ 100, 60 });
        se::Transport tr (kRate);
        tr.setDirection (PlayDirection::backward, *doc);
        tr.play (*doc, loop);
        const auto o = renderTransport (tr, *doc, 300);
        check (isSequence (o.left, walk (0, 100, 99, -1, false, 300)) && isSequence (o.right, walk (0, 60, 59, -1, false, 300)),
               "direction: backward, two tracks of 100 and 60 loop over their own lengths");
    }
    {
        auto doc = rampDocument ({ 100, 60 });
        se::Transport tr (kRate);
        tr.setDirection (PlayDirection::pingPong, *doc);
        tr.play (*doc, loop);
        const auto o = renderTransport (tr, *doc, 400);
        check (isSequence (o.left, walk (0, 100, 0, 1, true, 400)) && isSequence (o.right, walk (0, 60, 0, 1, true, 400)),
               "direction: ping-pong, two tracks each bounce at their own ends");
    }
    // A selection loop, in each direction.
    for (int d = 0; d < 3; ++d)
    {
        auto doc = rampDocument ({ kRamp });
        doc->selSt = 20; doc->selEnd = 40;
        se::Transport tr (kRate);
        tr.setDirection ((PlayDirection) d, *doc);
        tr.play (*doc, loop);
        const auto want = d == 0 ? walk (20, 20, 0, 1, false, 70) : d == 1 ? walk (20, 20, 19, -1, false, 70)
                                                                           : walk (20, 20, 0, 1, true, 70);
        check (isSequence (renderTransport (tr, *doc, 70).left, want),
               d == 0 ? "direction: selection loop, forward" : d == 1 ? "direction: selection loop, backward"
                                                                      : "direction: selection loop, ping-pong");
    }
    // Play once: through the span, in the direction, then stop.
    {
        auto doc = rampDocument ({ kRamp });
        se::Transport tr (kRate);
        tr.setDirection (PlayDirection::backward, *doc);
        tr.play (*doc, {});
        const auto o = renderTransport (tr, *doc, 300);
        check (o.frames == kRamp && isSequence (o.left, walk (0, kRamp, kRamp - 1, -1, false, kRamp)) && tr.state() == se::TransportState::stopped,
               "direction: once backward plays 99..0 then stops");
    }
    {
        auto doc = rampDocument ({ kRamp });
        se::Transport tr (kRate);
        tr.setDirection (PlayDirection::pingPong, *doc);
        tr.play (*doc, {});
        const auto o = renderTransport (tr, *doc, 500);
        check (o.frames == 2 * kRamp && isSequence (o.left, walk (0, kRamp, 0, 1, true, 2 * kRamp)) && tr.state() == se::TransportState::stopped,
               "direction: once ping-pong is one round trip, then stops");
    }
    {
        auto doc = rampDocument ({ kRamp });
        doc->selSt = 30; doc->selEnd = 50;
        se::Transport tr (kRate);
        tr.setDirection (PlayDirection::backward, *doc);
        tr.play (*doc, {});
        const auto o = renderTransport (tr, *doc, 100);
        check (o.frames == 20 && isSequence (o.left, walk (30, 20, 19, -1, false, 20)),
               "direction: once backward over a selection plays just it, reversed");
    }

    // Switching while it rolls: nothing jumps. The playhead of every track carries
    // on from where it was, and what is left of the step is crossfaded.
    const auto maxStep = [] (const std::vector<float>& a, const std::vector<float>& b)
    {
        // Over the 2 ms the crossfade lasts; the loop's own wrap is not what is asked about.
        float worst = 0.0f, prev = a.empty() ? 0.0f : a.back();
        for (size_t i = 0; i < b.size() && i < 30; ++i) { worst = std::max (worst, std::fabs (b[i] - prev)); prev = b[i]; }
        return worst * 128.0f;   // in sample units
    };
    {
        auto doc = rampDocument ({ 100, 60 });
        se::Transport tr (kRate);
        tr.play (*doc, loop);
        const auto before = renderTransport (tr, *doc, 37);            // pos 37: next reads 37 and 37
        size_t p0 = 0, p1 = 0;
        tr.setDirection (PlayDirection::backward, *doc);
        check (tr.uiTrackPosition (0, p0) && tr.uiTrackPosition (1, p1) && p0 == 37 && p1 == 37,
               "switch: forward -> backward, every track stays on the frame it was on");
        const auto after = renderTransport (tr, *doc, 120);
        check (maxStep (before.rawL, after.rawL) < 2.5f, "switch: no step in the signal (micro-crossfade)");
        // Once the 2 ms crossfade is over, it is plainly walking down from 37 (wrapping).
        const int skip = 60;
        const auto want = walk (0, 100, 37, -1, false, 120);
        check (std::vector<int> (after.left.begin() + skip, after.left.end()) == std::vector<int> (want.begin() + skip, want.end()),
               "switch: backward carries on from 37 downward");

        tr.setDirection (PlayDirection::pingPong, *doc);   // heading down: keeps heading down, bounces at 0
        const size_t pos1 = 37 + 120;                     // track 0: 37 - 120 mod 100 -> index 17 after the wrap
        (void) pos1;
        const auto a2 = renderTransport (tr, *doc, 160);
        const auto w2 = walk (0, 100, 17, -1, true, 160);   // heading down from 17, bounce at 0, then up
        check (std::vector<int> (a2.left.begin() + skip, a2.left.end()) == std::vector<int> (w2.begin() + skip, w2.end()),
               "switch: backward -> ping-pong keeps heading the same way, then bounces");

        tr.setDirection (PlayDirection::forward, *doc);
        const auto a3 = renderTransport (tr, *doc, 40);
        check (maxStep (a2.rawL, a3.rawL) < 2.5f, "switch: ping-pong -> forward, no step either");
    }
    // Rolling a play-once: a direction change finishes the pass the other way.
    {
        auto doc = rampDocument ({ kRamp });
        se::Transport tr (kRate);
        tr.play (*doc, {});
        renderTransport (tr, *doc, 60);
        tr.setDirection (PlayDirection::backward, *doc);
        const auto o = renderTransport (tr, *doc, 200);
        check (o.frames == 61 && o.left.back() == 0 && tr.state() == se::TransportState::stopped,
               "switch: play-once forward -> backward turns round and runs back to the start");
    }

    // The loop is live: Set Loop and the selection re-aim it while it plays.
    {
        auto doc = rampDocument ({ kRamp });
        doc->selSt = 20; doc->selEnd = 40;
        se::Transport tr (kRate);
        tr.play (*doc, loop);
        renderTransport (tr, *doc, 25);                       // inside: reading frame 25 next
        doc->selSt = 10; doc->selEnd = 90;                    // new range holds the playhead
        const auto a = renderTransport (tr, *doc, 100);       // 2 ms of crossfade, then the new loop alone
        const auto wantA = walk (10, 80, 15, 1, false, 100);
        check (a.left.front() == 25 && std::vector<int> (a.left.begin() + 60, a.left.end()) == std::vector<int> (wantA.begin() + 60, wantA.end()),
               "live loop: a new range around the playhead does not move it");
        check (tr.uiPlaySt() == 10 && tr.uiPlayEnd() == 90, "live loop: the published region follows");

        doc->selSt = 60; doc->selEnd = 70;                    // elsewhere: restart at its start
        const auto b = renderTransport (tr, *doc, 120);
        check (std::vector<int> (b.left.begin() + 60, b.left.end()) == walk (60, 10, (60 + 60 - 60) % 10, 1, false, 60),
               "live loop: a range elsewhere takes over (after the crossfade) from its start");

        doc->selSt = doc->selEnd = 5;                         // a caret: the whole sound loops
        renderTransport (tr, *doc, 3);
        size_t p = 0;
        check (tr.uiLoopMode() == se::LoopMode::all && tr.uiTrackPosition (0, p),
               "live loop: no selection any more, the whole sound loops");
    }
    {
        auto doc = rampDocument ({ kRamp });
        doc->selSt = 20; doc->selEnd = 40;
        se::PlayOptions region; region.loop = true; region.useLoopRegion = true;
        se::Transport tr (kRate);
        tr.play (*doc, region);                                // no region yet: the selection
        renderTransport (tr, *doc, 5);
        doc->loopSt = 30; doc->loopEnd = 50;                   // Set Loop: the region wins
        renderTransport (tr, *doc, 100);                       // outside 20..40's reading point? re-aimed either way
        check (tr.uiPlaySt() == 30 && tr.uiPlayEnd() == 50, "live loop: Set Loop while it plays re-aims the loop at the region");
        size_t p = 0;
        check (tr.uiTrackPosition (0, p) && p >= 30 && p < 50, "live loop: and the playhead is inside the region");
    }
    // The Keyboard's re-pitch of a rolling transport still works with a direction.
    {
        auto doc = rampDocument ({ kRamp });
        se::Transport tr (kRate);
        tr.setDirection (PlayDirection::backward, *doc);
        tr.play (*doc, loop);
        tr.setRateScale (2.0);
        const auto o = renderTransport (tr, *doc, 10);
        check (o.left[0] == 99 && o.left[1] == 97 && o.left[2] == 95, "direction: a re-pitched backward loop steps by two");
        tr.advance (*doc);
        check (true, "direction: advance accepted");
    }
}

std::unique_ptr<se::SoundDocument> sineDocument (size_t len);

// 12.8: deselecting (and every live re-aim) — a track that cannot keep its own frame
// follows the topmost track that had one. Cases as numbered in
// docs/0.4.0/12.8-deselect-sync.md. Ramps are 8-bit mono, so bytes == frames.
void deselectSyncTests()
{
    using se::PlayDirection;
    se::PlayOptions loop; loop.loop = true;
    const auto at = [] (se::Transport& tr, int t) { size_t p = 0; return tr.uiTrackPosition (t, p) ? (long) p : -1L; };
    const auto selectTracks = [] (se::SoundDocument& d, int first, int last, size_t st, size_t end)
    {
        d.firstSelTrack = first; d.lastSelTrack = last; d.selSt = st; d.selEnd = end;
    };

    // The bug itself (+ case 6, a caret is a deselection): looping track 2's selection,
    // then clearing it. Tracks 1 and 3 used to restart at 0.
    {
        auto doc = rampDocument ({ 100, 100, 100 });
        selectTracks (*doc, 2, 2, 20, 60);
        se::Transport tr (kRate);
        tr.play (*doc, loop);
        renderTransport (tr, *doc, 25);                    // track 2 reads 45 next
        doc->selSt = doc->selEnd = 10;                     // a caret
        renderTransport (tr, *doc, 1);
        check (tr.uiLoopMode() == se::LoopMode::all, "deselect: the whole sound loops");
        check (at (tr, 0) == 46 && at (tr, 1) == 46 && at (tr, 2) == 46,
               "deselect: the tracks outside the old selection pick up its frame (playheads 46, 46, 46)");
        const auto o = renderTransport (tr, *doc, 100);
        check (o.left.back() == (46 + 99) % 100 && o.right.back() == (46 + 99) % 100,
               "deselect: and run on from there, wrapping at their own ends");
    }
    // Backward and ping-pong: R's heading is taken too.
    for (int pp = 0; pp < 2; ++pp)
    {
        auto doc = rampDocument ({ 100, 100 });
        selectTracks (*doc, 2, 2, 20, 60);
        se::Transport tr (kRate);
        tr.setDirection (pp ? PlayDirection::pingPong : PlayDirection::backward, *doc);
        tr.play (*doc, loop);
        renderTransport (tr, *doc, pp ? 50 : 10);          // backward: 59 down to 50; ping-pong: up 40, down 10 -> 49
        const long r = at (tr, 1);
        doc->selSt = doc->selEnd = 0;
        renderTransport (tr, *doc, 1);
        check (at (tr, 0) == r - 1 && at (tr, 1) == r - 1,
               pp ? "deselect: ping-pong on its way down, the others come in heading down too"
                  : "deselect: backward, the others come in on its frame, heading backward");
    }
    // Case 1: selected tracks out of phase keep their own frames; the others follow
    // the topmost of them.
    {
        auto doc = rampDocument ({ 100, 70, 100 });
        se::Transport tr (kRate);
        tr.play (*doc, loop);
        renderTransport (tr, *doc, 80);                    // 80, 10, 80
        selectTracks (*doc, 1, 2, 0, 100);
        renderTransport (tr, *doc, 1);                     // 81, 11; track 3 out
        doc->selSt = doc->selEnd = 0;
        renderTransport (tr, *doc, 1);
        check (at (tr, 0) == 82 && at (tr, 1) == 12 && at (tr, 2) == 82,
               "deselect: out-of-phase selected tracks keep their frames, the others take the topmost's");
    }
    // Case 2: a formerly-selected track that was absent follows R, wrapped to its length.
    {
        auto doc = rampDocument ({ 100, 30, 100 });
        selectTracks (*doc, 2, 3, 20, 60);
        se::Transport tr (kRate);
        tr.play (*doc, loop);
        renderTransport (tr, *doc, 25);                    // track 2 (30 long) absent at 45, track 3 at 45
        doc->selSt = doc->selEnd = 0;
        renderTransport (tr, *doc, 1);
        check (at (tr, 2) == 46 && at (tr, 0) == 46 && at (tr, 1) == 16,
               "deselect: an absent selected track follows the reference, mod its own length (45 mod 30 -> 16)");
    }
    // Case 3: no reference at all — every selected track absent: span start, as before.
    {
        auto doc = rampDocument ({ 100, 30 });
        selectTracks (*doc, 2, 2, 40, 80);
        se::Transport tr (kRate);
        tr.play (*doc, loop);
        renderTransport (tr, *doc, 5);                     // track 2 absent at 45
        doc->selSt = doc->selEnd = 0;
        renderTransport (tr, *doc, 1);
        check (at (tr, 0) == 1 && at (tr, 1) == 1, "deselect: no reference, every track starts over (today's rule) got " + std::to_string (at (tr, 0)) + "," + std::to_string (at (tr, 1)));
    }
    // Case 4: a new selection over other tracks — the newcomers take F inside the span.
    {
        auto doc = rampDocument ({ 100, 100 });
        selectTracks (*doc, 1, 1, 20, 60);
        se::Transport tr (kRate);
        tr.play (*doc, loop);
        renderTransport (tr, *doc, 15);                    // track 1 reads 35 next
        selectTracks (*doc, 1, 2, 20, 60);
        renderTransport (tr, *doc, 1);
        check (at (tr, 0) == 36 && at (tr, 1) == 36, "reselect: a track entering the selection takes the reference's frame");
        selectTracks (*doc, 1, 2, 70, 90);                 // F (37) outside the new span: its start
        renderTransport (tr, *doc, 1);
        check (at (tr, 0) == 71 && at (tr, 1) == 71, "reselect: outside the new span, the span's start as before");
    }
    // Case 5: dragging the selection along: nothing that has a frame jumps.
    {
        auto doc = rampDocument ({ 100, 100 });
        selectTracks (*doc, 1, 1, 10, 60);
        se::Transport tr (kRate);
        tr.play (*doc, loop);
        renderTransport (tr, *doc, 20);                    // 30 next
        bool steady = true;
        long last = at (tr, 0);
        for (int k = 0; k < 10; ++k)
        {
            doc->selSt += 1; doc->selEnd += 1;
            if (k == 5) doc->lastSelTrack = 2;             // the drag takes in track 2 halfway
            renderTransport (tr, *doc, 1);
            steady = steady && at (tr, 0) == last + 1;
            last = at (tr, 0);
        }
        check (steady && at (tr, 1) == last, "drag: the playhead never jumps, and the track taken in joins it");
    }
    // Case 7: a Set Loop region is the aim: deselecting changes nothing.
    {
        auto doc = rampDocument ({ 100, 100 });
        doc->loopSt = 20; doc->loopEnd = 60;
        selectTracks (*doc, 1, 1, 30, 40);
        se::PlayOptions region; region.loop = true; region.useLoopRegion = true;
        se::Transport tr (kRate);
        tr.play (*doc, region);
        renderTransport (tr, *doc, 10);
        const long before = at (tr, 0);
        doc->selSt = doc->selEnd = 0;
        renderTransport (tr, *doc, 1);
        check (tr.uiLoopMode() == se::LoopMode::selection && at (tr, 0) == before + 1 && at (tr, 1) == before + 1,
               "Set Loop: deselecting does not touch a region loop");
    }
    // Case 8: play-once has no live re-aim.
    {
        auto doc = rampDocument ({ 100, 100 });
        selectTracks (*doc, 1, 1, 20, 60);
        se::PlayOptions once;
        se::Transport tr (kRate);
        tr.play (*doc, once);
        renderTransport (tr, *doc, 5);
        doc->selSt = doc->selEnd = 0;
        renderTransport (tr, *doc, 1);
        check (tr.uiLoopMode() == se::LoopMode::once && at (tr, 0) == 26, "play once: deselecting mid-pass changes nothing (unchanged)");
    }
    // Case 9: paused, the re-aim lands on resume, from where it was paused.
    {
        auto doc = rampDocument ({ 100, 100 });
        selectTracks (*doc, 2, 2, 20, 60);
        se::Transport tr (kRate);
        tr.play (*doc, loop);
        renderTransport (tr, *doc, 25);
        tr.pause();
        doc->selSt = doc->selEnd = 0;
        const auto held = renderTransport (tr, *doc, 10);
        check (held.frames == 0 && tr.uiLoopMode() == se::LoopMode::selection, "paused: nothing re-aims while paused");
        tr.pause();
        renderTransport (tr, *doc, 1);
        check (tr.uiLoopMode() == se::LoopMode::all && at (tr, 0) == 46 && at (tr, 1) == 46,
               "paused: on resume the deselection lands on the paused frame");
    }
    // Case 10: while a take runs (excludeMask) the aim is frozen; it lands after.
    {
        auto doc = rampDocument ({ 100, 100 });
        selectTracks (*doc, 2, 2, 20, 60);
        se::Transport tr (kRate);
        tr.play (*doc, loop);
        tr.excludeMask = 2;                                // track 2 is being written
        renderTransport (tr, *doc, 25);
        doc->selSt = doc->selEnd = 0;
        renderTransport (tr, *doc, 1);
        check (tr.uiLoopMode() == se::LoopMode::selection && at (tr, 1) == 46, "recording: no re-aim mid-take");
        tr.excludeMask = 0;                                // the take ends
        renderTransport (tr, *doc, 1);
        check (tr.uiLoopMode() == se::LoopMode::all && at (tr, 0) == 47 && at (tr, 1) == 47,
               "recording: the deselection lands when the take ends");
    }
    // Case 11: no click — a sine through the deselection.
    {
        auto doc = sineDocument (400);
        doc->tracks.push_back (doc->tracks[0]);
        doc->tracks[1].outLeft = true;
        selectTracks (*doc, 2, 2, 100, 300);
        se::Transport tr (kRate);
        tr.play (*doc, loop);
        const auto a = renderTransport (tr, *doc, 150);
        doc->selSt = doc->selEnd = 0;
        const auto b = renderTransport (tr, *doc, 200);
        float worst = 0.0f, prevS = a.rawL.back();
        for (float v : b.rawL) { worst = std::max (worst, std::fabs (v - prevS)); prevS = v; }
        check (worst * 128.0f < 5.0f, "deselect: the tracks that come in do so under the crossfade, no click");
    }
    // Case 13: Advance and the Keyboard's rateScale do not change the frames picked up.
    {
        auto doc = rampDocument ({ 100, 100 });
        selectTracks (*doc, 2, 2, 20, 60);
        se::Transport tr (kRate);
        tr.play (*doc, loop);
        tr.setRateScale (2.0);
        renderTransport (tr, *doc, 10);                    // 20 + 2*10: reads 40 next
        doc->selSt = doc->selEnd = 0;
        renderTransport (tr, *doc, 1);
        check (at (tr, 0) == 42 && at (tr, 1) == 42, "deselect at 2x: the frame is picked up all the same");
    }
}

// Case 12 + TSan: several Windows, selections made and cleared while they render.
void deselectConcurrencyTests()
{
    se::Engine engine;
    engine.prepare (kRate, kBlock);
    const uint64_t a = engine.addWindow (rampDocument ({ 700, 450, 300 }));
    const uint64_t b = engine.addWindow (rampDocument ({ 500, 800 }));
    se::PlayOptions loop; loop.loop = true;
    engine.play (a, loop);
    engine.play (b, loop);

    std::atomic<bool> stop { false }, sane { true };
    std::thread audio ([&]
    {
        std::vector<float> l ((size_t) kBlock), r ((size_t) kBlock);
        while (! stop.load())
        {
            engine.render (l.data(), r.data(), kBlock);
            for (int i = 0; i < kBlock; ++i)
                if (! std::isfinite (l[(size_t) i]) || std::fabs (l[(size_t) i]) > 1.0f) sane = false;
        }
    });
    unsigned seed = 777;
    const auto next = [&] (unsigned m) { seed = seed * 1664525u + 1013904223u; return (seed >> 8) % m; };
    long reads = 0;
    for (int i = 0; i < 3000; ++i)
    {
        const uint64_t id = next (2) ? a : b;
        if (auto* d = engine.editDoc (id))
        {
            const size_t len = d->maxTrackSize();
            if (next (3) == 0) { d->selSt = d->selEnd = next ((unsigned) len); }
            else
            {
                d->firstSelTrack = 1 + (int) next ((unsigned) d->numTracks());
                d->lastSelTrack = d->firstSelTrack + (int) next ((unsigned) (d->numTracks() - d->firstSelTrack + 1));
                d->selSt = next ((unsigned) len / 2); d->selEnd = d->selSt + 1 + next ((unsigned) len / 2);
            }
            if (next (7) == 0) engine.setDirection (id, (se::PlayDirection) next (3));
            engine.commitEdit (id);
        }
        if (auto* w = engine.windowById (id))
            for (int t = 0; t < 3; ++t) { size_t p = 0; reads += w->transport->uiTrackPosition (t, p) ? 1 : 0; }
        engine.collectRetired();
        std::this_thread::sleep_for (std::chrono::microseconds (150));
    }
    stop = true;
    audio.join();
    engine.collectRetired();
    check (sane.load(), "deselect: two Windows selecting / deselecting under the audio thread (" + std::to_string (reads) + " reads)");
}

// A smooth signal exposes a click that a ramp hides: one sine cycle, amplitude 100
// (8-bit units), so no honest step between neighbouring frames exceeds ~0.16.
std::unique_ptr<se::SoundDocument> sineDocument (size_t len)
{
    se::DocumentInit init;
    init.numTracks = 1; init.bps = 8; init.rateHz = kRate; init.sizeBytes = len;
    auto doc = std::make_unique<se::SoundDocument> (init);
    for (size_t i = 0; i < len; ++i)
        se::setSample (doc->tracks[0].data.data(), i,
                       (int) std::lround (100.0 * std::sin (6.283185307179586 * (double) i / (double) len)), 8);
    return doc;
}

void switchingTests()
{
    using se::PlayDirection;
    // Flip the direction every 70 frames (more often than the crossfade lasts), in
    // every order, over a loop of the whole sound, over a selection, and with
    // Advance's 2x and the Keyboard's re-pitch in the mix. The worst step between
    // neighbouring output frames is the click; the sine's own slope is ~0.16 units,
    // 8-bit quantisation 1, so anything near a jump would show.
    for (int scenario = 0; scenario < 4; ++scenario)
    {
        auto doc = sineDocument (4000);
        if (scenario == 1) { doc->selSt = 500; doc->selEnd = 1500; }   // same value at both ends
        se::PlayOptions o; o.loop = true;
        se::Transport tr (kRate);
        tr.play (*doc, o);
        if (scenario == 2) tr.advance (*doc);
        if (scenario == 3) tr.setRateScale (1.5);

        float prev = 0.0f, worst = 0.0f;
        bool first = true;
        std::vector<float> l (70, 0.0f), r (70, 0.0f);
        for (int block = 0; block < 300; ++block)
        {
            tr.setDirection ((PlayDirection) ((block * 7 + block / 3) % 3), *doc);
            const int n = tr.render (*doc, l.data(), r.data(), 70);
            for (int i = 0; i < n; ++i)
            {
                if (! first) worst = std::max (worst, std::fabs (l[(size_t) i] - prev) * 128.0f);
                prev = l[(size_t) i]; first = false;
            }
        }
        // Only the 1-unit quantisation and the sine's own slope: flips add nothing.
        const float limit = 3.0f;
        check (worst < limit, "switching: a direction flip every 70 frames, " + std::string (
                              scenario == 0 ? "whole-sound loop" : scenario == 1 ? "selection loop" : scenario == 2 ? "advance (2x)" : "re-pitched")
                              + " (worst step " + std::to_string (worst) + ")");
    }
}

// The message thread flips directions, plays, pauses, edits and reads the playheads
// while the audio thread renders: what TSan is for. Nothing here asserts timing,
// only that the output stays finite and in range and the run is race-free.
void concurrencyTests()
{
    using se::PlayDirection;
    se::Engine engine;
    engine.prepare (kRate, kBlock);
    const uint64_t a = engine.addWindow (sineDocument (4000));
    const uint64_t b = engine.addWindow (rampDocument ({ 700, 450 }));

    std::atomic<bool> stop { false };
    std::atomic<bool> sane { true };
    std::thread audio ([&]
    {
        std::vector<float> l ((size_t) kBlock), r ((size_t) kBlock);
        while (! stop.load())
        {
            engine.render (l.data(), r.data(), kBlock);
            for (int i = 0; i < kBlock; ++i)
                if (! std::isfinite (l[(size_t) i]) || ! std::isfinite (r[(size_t) i])
                    || std::fabs (l[(size_t) i]) > 1.0f || std::fabs (r[(size_t) i]) > 1.0f) sane = false;
        }
    });

    unsigned seed = 12345;
    const auto next = [&] (unsigned m) { seed = seed * 1664525u + 1013904223u; return (seed >> 8) % m; };
    long reads = 0;
    for (int i = 0; i < 4000; ++i)
    {
        const uint64_t id = next (2) ? a : b;
        switch (next (9))
        {
            case 0: engine.setDirection (id, (PlayDirection) next (3)); break;
            case 1: { se::PlayOptions o; o.loop = next (2) != 0; o.useLoopRegion = next (2) != 0; engine.play (id, o); break; }
            case 2: engine.pause (id); break;
            case 3: engine.stop (id); break;
            case 4: engine.advance (id); break;
            case 5: engine.setRateScale (id, 0.5 + 0.25 * (double) next (6)); break;
            case 6:
            {
                if (auto* d = engine.editDoc (id))
                {
                    const size_t len = d->maxTrackSize();
                    d->selSt = next (3) ? len / (2 + next (3)) : 0;
                    d->selEnd = next (3) ? d->selSt + len / (3 + next (3)) : d->selSt;
                    d->loopSt = next (2) ? len / 5 : 0;
                    d->loopEnd = next (2) ? d->loopSt + len / 4 : 0;
                    engine.commitEdit (id);
                }
                break;
            }
            default: break;
        }
        if (auto* w = engine.windowById (id))
        {
            size_t p = 0;
            reads += w->transport->uiTrackPosition ((int) next (2), p) ? 1 : 0;
            reads += (long) (w->transport->uiPlayhead() & 1);
            reads += (long) w->transport->uiDirection() + (long) w->transport->uiPlayEnd() % 2;
        }
        engine.collectRetired();
        std::this_thread::sleep_for (std::chrono::microseconds (200));
    }
    stop = true;
    audio.join();
    engine.collectRetired();
    check (sane.load(), "concurrency: output stays finite and in range under direction / play / edit churn (" + std::to_string (reads) + " reads)");
}

// The Engine's own bridge: the command, and the Window's copy for the UI.
void directionEngineTests()
{
    using se::PlayDirection;
    se::Engine engine;
    engine.prepare (kRate, kBlock);
    const uint64_t id = engine.addWindow (rampDocument ({ kRamp }));
    check (engine.direction (id) == PlayDirection::forward, "engine: a new Window plays forward");
    engine.setDirection (id, PlayDirection::backward);
    check (engine.direction (id) == PlayDirection::backward, "engine: setDirection is visible to the UI at once");
    se::PlayOptions loop; loop.loop = true;
    engine.play (id, loop);
    std::vector<float> l ((size_t) kBlock, 0.0f), r ((size_t) kBlock, 0.0f);
    engine.render (l.data(), r.data(), kBlock);
    check (indexOf (l[0]) == 99 && indexOf (l[1]) == 98, "engine: the command reaches the audio thread, ahead of play");
    engine.setDirection (id, PlayDirection::forward);
    engine.render (l.data(), r.data(), kBlock);
    auto* w = engine.windowById (id);
    size_t p = 0;
    check (w->transport->uiDirection() == PlayDirection::forward && w->transport->uiTrackPosition (0, p),
           "engine: a direction change while rolling reaches the transport");
}

} // namespace

// --- recording conversion (9.2) -------------------------------------------------
// Raw is the 0.9.2 resamplers' arithmetic on a stream; Clean is band-limited with
// dither. Both are streams, so the block size must not matter.

std::vector<float> sineWave (double freq, double rate, size_t n, float amp = 0.5f)
{
    std::vector<float> v (n);
    for (size_t i = 0; i < n; ++i) v[i] = amp * (float) std::sin (2.0 * 3.14159265358979323846 * freq * (double) i / rate);
    return v;
}

struct Converted { std::vector<int32_t> l, r; };

Converted convertAll (se::RecordConverter& c, const std::vector<float>& x, size_t chunk)
{
    Converted out;
    for (size_t at = 0; at < x.size(); at += chunk)
    {
        const size_t n = std::min (chunk, x.size() - at);
        c.process (x.data() + at, x.data() + at, n, out.l, out.r);
    }
    c.flush (out.l, out.r);
    return out;
}

double rms (const std::vector<int32_t>& v, double scale, size_t from, size_t to)
{
    double s = 0.0;
    for (size_t i = from; i < to; ++i) s += (double) v[i] * v[i];
    return std::sqrt (s / (double) (to - from)) / scale;
}

void convertTests()
{
    // Raw, same rate and depth: every sample is floor(x * 2^(bps-1)), no more.
    {
        const auto x = sineWave (440.0, 44100.0, 1000);
        se::RecordConverter c (44100.0, 44100.0, 16, se::ConvertMode::raw);
        const auto o = convertAll (c, x, 1000);
        bool same = o.l.size() == x.size();
        for (size_t i = 0; same && i < x.size(); ++i)
            same = o.l[i] == (int32_t) std::floor ((double) x[i] * 32768.0) && o.r[i] == o.l[i];
        check (same, "raw 1:1 at 16 bit: floor(x * 32768), both sides");
    }

    // Raw reproduces the 0.9.2 resamplers (zero-order hold, then truncation), index
    // for index: build a 16-bit Sound from the same floats, resample it with the
    // old code, and compare.
    for (double outRate : { 22254.5454, 11025.0, 48000.0, 96000.0 })
    {
        const double inRate = 44100.0;
        const auto x = sineWave (1000.0, inRate, 4410, 0.9f);
        for (int bps : { 16, 8, 12 })
        {
            se::DocumentInit init; init.rateHz = inRate; init.bps = 16; init.numTracks = 1; init.sizeBytes = x.size() * 2;
            se::SoundDocument doc (init);
            for (size_t i = 0; i < x.size(); ++i)
                se::setSample (doc.tracks[0].data.data(), i * 2, (int32_t) std::floor ((double) x[i] * 32768.0), 16);
            se::resampleSoundRate (doc, outRate);
            if (bps != 16) se::resampleSoundBits (doc, bps);

            se::RecordConverter c (inRate, outRate, bps, se::ConvertMode::raw);
            const auto o = convertAll (c, x, 333);

            // Compare what the Sound would hold: the old code's own bytes vs ours via setSample.
            const size_t fs = (size_t) se::frameSizeFor (bps);
            const size_t n = std::min (o.l.size(), doc.tracks[0].data.size() / fs);
            // The old code ACCUMULATES its step, so where k * ratio lands exactly on a
            // whole input frame it can come out one frame early; ours multiplies. Allow
            // those boundary frames (under 1 %: only where the ratio lands on whole frames), and the length to differ by one.
            size_t differ = 0;
            for (size_t i = 0; i < n; ++i)
            {
                std::vector<uint8_t> mine (fs);
                se::setSample (mine.data(), 0, o.l[i], bps);
                for (size_t b = 0; b < fs; ++b)
                    if (mine[b] != doc.tracks[0].data[i * fs + b]) { ++differ; break; }
            }
            const size_t oldN = doc.tracks[0].data.size() / fs;
            const bool same = n > 0 && differ * 100 <= n && (o.l.size() == oldN || o.l.size() == oldN + 1);
            if (! same) std::printf ("   n=%zu differ=%zu mine=%zu old=%zu\n", n, differ, o.l.size(), oldN);
            check (same, "raw == resampleSoundRate + resampleSoundBits: " + std::to_string ((int) outRate) + " Hz, " + std::to_string (bps) + " bit");
        }
    }

    // Raw keeps its aliasing: a 15 kHz tone down to 22.05 kHz folds to 7.05 kHz at full level.
    {
        const auto x = sineWave (15000.0, 48000.0, 48000, 0.5f);
        se::RecordConverter c (48000.0, 22050.0, 16, se::ConvertMode::raw);
        const auto o = convertAll (c, x, 1024);
        const double level = rms (o.l, 32768.0, 100, o.l.size() - 100);
        check (level > 0.3, "raw: a tone above the new Nyquist aliases at full level");
    }

    // Clean: the same tone is gone, a tone below it passes, at either direction of conversion.
    {
        const auto hi = sineWave (15000.0, 48000.0, 48000, 0.5f);
        se::RecordConverter c (48000.0, 22050.0, 16, se::ConvertMode::clean);
        const auto o = convertAll (c, hi, 1024);
        const double level = rms (o.l, 32768.0, 200, o.l.size() - 200);
        check (level < 0.003, "clean: a tone above the new Nyquist is rejected (> 40 dB down)");

        const auto lo = sineWave (1000.0, 48000.0, 48000, 0.5f);
        se::RecordConverter c2 (48000.0, 22050.0, 16, se::ConvertMode::clean);
        const auto p = convertAll (c2, lo, 1024);
        checkClose ((float) rms (p.l, 32768.0, 200, p.l.size() - 200), 0.5f / std::sqrt (2.0f), 0.004f, "clean: a 1 kHz tone passes at its level");

        // Aligned in time: a rising zero crossing of the input lands on the same instant.
        size_t zc = 0;
        for (size_t i = 200; i + 1 < p.l.size(); ++i) if (p.l[i] <= 0 && p.l[i + 1] > 0) { zc = i; break; }
        const double t = (double) zc / 22050.0;                  // seconds
        const double cycles = t * 1000.0;
        checkClose ((float) (cycles - std::round (cycles)), 0.0f, 0.03f, "clean: no time shift (zero crossings on the input's grid)");

        // Up: 22.05 kHz -> 48 kHz keeps the tone, the length scales.
        const auto src = sineWave (2000.0, 22050.0, 22050, 0.5f);
        se::RecordConverter up (22050.0, 48000.0, 16, se::ConvertMode::clean);
        const auto u = convertAll (up, src, 777);
        check (u.l.size() == se::RecordConverter::outputLength (22050, 22050.0, 48000.0), "clean up: length = input * ratio");
        checkClose ((float) rms (u.l, 32768.0, 300, u.l.size() - 300), 0.5f / std::sqrt (2.0f), 0.004f, "clean up: the tone passes at its level");
    }

    // The block size must not change a thing, dither included (it follows the output sample).
    for (auto mode : { se::ConvertMode::raw, se::ConvertMode::clean })
    {
        const auto x = sineWave (3000.0, 44100.0, 5000, 0.7f);
        se::RecordConverter a (44100.0, 22050.0, 12, mode, 7), b (44100.0, 22050.0, 12, mode, 7);
        const auto oa = convertAll (a, x, 5000), ob = convertAll (b, x, 37);
        check (oa.l == ob.l && oa.r == ob.r, mode == se::ConvertMode::raw ? "raw: block size does not matter" : "clean: block size does not matter");
    }

    // Dither: silence stays silent in Raw; in Clean it is +-1 LSB of triangular noise, centred.
    {
        std::vector<float> z (20000, 0.0f);
        se::RecordConverter raw (44100.0, 44100.0, 8, se::ConvertMode::raw), cln (44100.0, 44100.0, 8, se::ConvertMode::clean);
        const auto r = convertAll (raw, z, 4096), c = convertAll (cln, z, 4096);
        bool zeros = true; for (auto v : r.l) if (v != 0) zeros = false;
        check (zeros, "raw: silence stays exactly zero (no dither)");
        double sum = 0.0; int big = 0, nonzero = 0;
        for (auto v : c.l) { sum += v; if (std::abs (v) > 1) ++big; if (v != 0) ++nonzero; }
        check (big == 0 && nonzero > 2000, "clean 8 bit: silence becomes +-1 LSB dither");
        checkClose ((float) (sum / (double) c.l.size()), 0.0f, 0.03f, "clean: the dither is centred");
        check (c.l != c.r, "clean: the two sides get independent dither");
    }

    // Depth edges: full scale clamps, left-aligned in the frame.
    {
        std::vector<float> x { 1.0f, -1.0f, 2.0f, -3.0f };
        se::RecordConverter c (44100.0, 44100.0, 12, se::ConvertMode::raw);
        const auto o = convertAll (c, x, 4);
        check (o.l[0] == 2047 * 16 && o.l[1] == -2048 * 16 && o.l[2] == 2047 * 16 && o.l[3] == -2048 * 16,
               "12 bit: clamps at full scale and sits left-aligned in two bytes");
        se::RecordConverter c8 (44100.0, 44100.0, 8, se::ConvertMode::raw);
        const auto p = convertAll (c8, x, 4);
        check (p.l[0] == 127 && p.l[1] == -128, "8 bit: signed, clamped");
    }
}

// --- recording into a Sound (9.3 / 9.4) -----------------------------------------
// The Recorder drives a take on top of the Engine's capture: the test plays the
// audio thread by rendering blocks (with a constant input) and the message thread
// by calling service() between them.

void renderWithInput (se::Engine& e, std::initializer_list<float> levels, std::vector<float>& outL, std::vector<float>& outR);

constexpr int kRecBps = 16;

std::unique_ptr<se::SoundDocument> blankSound (int tracks, double rate, int bps = kRecBps)
{
    se::DocumentInit init; init.name = "rec"; init.numTracks = tracks; init.bps = bps; init.rateHz = rate; init.sizeBytes = 0;
    return std::make_unique<se::SoundDocument> (init);
}

void pump (se::Engine& e, se::Recorder& r, int blocks, std::initializer_list<float> in)
{
    std::vector<float> ol, orr;
    for (int i = 0; i < blocks; ++i) { renderWithInput (e, in, ol, orr); r.service(); }
}

/** Run until the recorder is idle (or `paused`), as the timer would. */
void settleRec (se::Engine& e, se::Recorder& r, std::initializer_list<float> in, se::Recorder::State want = se::Recorder::State::idle)
{
    for (int i = 0; i < 400 && r.state() != want; ++i) pump (e, r, 1, in);
}

int32_t at (const se::SoundDocument& d, int track, size_t frame)
{
    const size_t fs = (size_t) d.frameSize();
    return se::getSample (d.tracks[(size_t) track].data.data(), frame * fs, d.bps);
}

void recordTests()
{
    const int32_t quarter = 8192, half = 16384;   // 0.25 / 0.5 at 16 bit

    // Mono source into a blank Sound: the empty track is used, nothing is added.
    {
        se::Engine e; e.prepare (kRate, kBlock);
        const uint64_t w = e.addWindow (blankSound (1, kRate));
        se::Recorder rec (e);
        se::RecordSetup s; s.source.kind = se::AudioSourceKind::external; s.source.firstChannel = 0; s.source.stereo = false; s.incomingStereo = false;
        check (rec.start (w, s), "record: starts in a blank Sound");
        check (rec.state() == se::Recorder::State::recording, "record: recording");
        pump (e, rec, 10, { 0.25f });
        rec.stop();
        settleRec (e, rec, { 0.25f });
        const auto* d = e.editDoc (w);
        check (d->numTracks() == 1, "record mono: a blank track is used, no track added");
        check (d->tracks[0].data.size() == (size_t) (10 * kBlock) * 2, "record mono: every captured frame is written (rate and depth conform 1:1)");
        check (at (*d, 0, 0) == quarter && at (*d, 0, 639) == quarter, "record mono: the samples are the input at 16 bit");
        check (rec.state() == se::Recorder::State::idle, "record: idle after Stop");
        check (! e.captureRunning(), "record: the audio thread has stopped capturing");
    }

    // Stereo source: two new tracks after the existing ones, routed to L and R; the
    // existing track plays on, the new ones stay silent while they are written.
    {
        se::Engine e; e.prepare (kRate, kBlock);
        const uint64_t w = e.addWindow (dcDocument (20, "w"));   // 8-bit, 512 frames of DC, rate == device rate
        auto* win = e.windowById (w);
        const float level = (float) 20 / 128.0f;
        e.setDirection (w, se::PlayDirection::backward);
        se::Recorder rec (e);
        se::RecordSetup s; s.source.kind = se::AudioSourceKind::external; s.source.firstChannel = 0; s.source.stereo = true; s.incomingStereo = true;
        check (rec.start (w, s), "record stereo: starts");
        check (e.direction (w) == se::PlayDirection::forward, "record: forces Forward");
        std::vector<float> ol, orr;
        renderWithInput (e, { 0.25f, -0.5f }, ol, orr);
        rec.service();
        renderWithInput (e, { 0.25f, -0.5f }, ol, orr);
        checkClose (ol[kBlock - 1], level, 1.0e-4f, "record: the tracks already there keep playing (and only they)");
        pump (e, rec, 6, { 0.25f, -0.5f });
        rec.stop();
        settleRec (e, rec, { 0.25f, -0.5f });
        const auto* d = e.editDoc (w);
        check (d->numTracks() == 3, "record stereo: two new tracks after the existing one");
        check (d->tracks[1].outLeft && ! d->tracks[1].outRight && ! d->tracks[2].outLeft && d->tracks[2].outRight,
               "record stereo: the new tracks are routed to the left and right outputs");
        check (at (*d, 1, 100) == 32 && at (*d, 2, 100) == -64, "record stereo: left on the first, right on the second");
        check (d->tracks[0].data.size() == 512, "record: the existing track is untouched");
        (void) win;
    }

    // Pause, then Record again, appends to the same tracks.
    {
        se::Engine e; e.prepare (kRate, kBlock);
        const uint64_t w = e.addWindow (blankSound (1, kRate));
        se::Recorder rec (e);
        se::RecordSetup s; s.source.kind = se::AudioSourceKind::external; s.source.stereo = false; s.incomingStereo = false;
        rec.start (w, s);
        pump (e, rec, 5, { 0.25f });
        rec.pause();
        settleRec (e, rec, { 0.25f }, se::Recorder::State::paused);
        check (rec.state() == se::Recorder::State::paused, "pause: the take is paused");
        check (! e.captureRunning(), "pause: capture stopped");
        const size_t after5 = e.editDoc (w)->tracks[0].data.size();
        check (after5 == (size_t) (5 * kBlock) * 2, "pause: what was captured so far is in the track");
        pump (e, rec, 4, { 0.9f });   // time passes while paused: nothing is captured
        check (e.editDoc (w)->tracks[0].data.size() == after5, "pause: nothing is recorded while paused");
        rec.resume();
        check (rec.state() == se::Recorder::State::recording, "resume: recording again");
        pump (e, rec, 5, { 0.5f });
        rec.stop();
        settleRec (e, rec, { 0.5f });
        const auto* d = e.editDoc (w);
        check (d->numTracks() == 1 && d->tracks[0].data.size() == (size_t) (10 * kBlock) * 2, "resume: the new audio is appended to the same track");
        check (at (*d, 0, 5 * kBlock - 1) == quarter && at (*d, 0, 5 * kBlock) == half, "resume: no gap and no overlap at the seam");
        // A new take after Stop makes new tracks.
        rec.start (w, s);
        pump (e, rec, 2, { 0.25f });
        rec.stop();
        settleRec (e, rec, { 0.25f });
        check (e.editDoc (w)->numTracks() == 2, "a take after Stop goes into a new track");
    }

    // Rate and depth conform to the Sound's own: a Sound at half the device rate, 8 bit.
    {
        se::Engine e; e.prepare (kRate, kBlock);
        const uint64_t w = e.addWindow (blankSound (1, kRate / 2.0, 8));
        se::Recorder rec (e);
        se::RecordSetup s; s.source.stereo = false; s.incomingStereo = false;
        rec.start (w, s);
        pump (e, rec, 10, { 0.25f });
        rec.stop();
        settleRec (e, rec, { 0.25f });
        const auto* d = e.editDoc (w);
        check (d->tracks[0].data.size() == (size_t) (5 * kBlock), "conform: half the rate, half the frames (8 bit = 1 byte)");
        check (at (*d, 0, 10) == 32, "conform: 0.25 at 8 bit");
    }

    // Recording the main mix into a Window leaves that Window out of it.
    {
        se::Engine e; e.prepare (kRate, kBlock);
        const uint64_t a = e.addWindow (dcDocument (40, "a"));
        auto wdoc = dcDocument (10, "w");
        const uint64_t w = e.addWindow (std::move (wdoc));
        se::PlayOptions loopAll; loopAll.loop = true;
        e.play (a, loopAll);
        se::Recorder rec (e);
        se::RecordSetup s; s.source.kind = se::AudioSourceKind::canvas; s.incomingStereo = true;
        rec.start (w, s);
        pump (e, rec, 4, {});
        rec.stop();
        settleRec (e, rec, {});
        const auto* d = e.editDoc (w);
        // Window `a` alone: 8-bit DC 40 is 40/128 = 0.3125, which is 40 again at the Sound's 8 bits.
        check (d->numTracks() == 3, "canvas record: two tracks added to the Window");
        check (at (*d, 1, 100) == 40, "canvas record: the main mix, without the Window recorded into");
    }

    // --- 9.4: record into a selection ---------------------------------------
    // One selected track: the incoming stereo is summed to mono (averaged), and with
    // Loop off the take is a punch-in/out that ends by itself at the selection's end.
    {
        se::Engine e; e.prepare (kRate, kBlock);
        auto doc = blankSound (1, kRate);
        doc->tracks[0].data.assign (2000, 0);                    // 1000 frames of silence
        doc->selSt = 200; doc->selEnd = 600; doc->firstSelTrack = doc->lastSelTrack = 1;   // frames 100..299
        const uint64_t w = e.addWindow (std::move (doc));
        se::Recorder rec (e);
        int finished = 0;
        rec.onFinished = [&] (uint64_t) { ++finished; };
        se::RecordSetup s; s.source.kind = se::AudioSourceKind::external; s.source.stereo = true; s.incomingStereo = true;
        check (rec.start (w, s), "selection: starts over one track");
        check (rec.targetTracks().size() == 1 && rec.targetTracks()[0] == 0, "selection: the selected track is the target");
        settleRec (e, rec, { 0.5f, 0.25f });
        const auto* d = e.editDoc (w);
        check (finished == 1 && rec.state() == se::Recorder::State::idle, "punch: the take ends by itself at the selection's end");
        check (d->tracks[0].data.size() == 2000, "punch: the track keeps its length");
        check (at (*d, 0, 99) == 0 && at (*d, 0, 300) == 0, "punch: nothing outside the selection");
        check (at (*d, 0, 100) == 12288 && at (*d, 0, 299) == 12288, "punch: inside it, left + right summed to mono (0.375)");
        check (! e.captureRunning(), "punch: the audio thread stopped capturing");
        check (e.windowById (w)->transport->uiState() == se::TransportState::stopped, "punch: the transport is stopped");
    }

    // Two selected tracks: left on the upper, right on the lower, whatever their speaker toggles say.
    {
        se::Engine e; e.prepare (kRate, kBlock);
        auto doc = blankSound (3, kRate);
        for (auto& t : doc->tracks) t.data.assign (800, 0);
        doc->tracks[1].outLeft = false; doc->tracks[1].outRight = true;     // upper selected track: right speaker only
        doc->tracks[2].outLeft = true;  doc->tracks[2].outRight = false;
        doc->selSt = 100; doc->selEnd = 300; doc->firstSelTrack = 2; doc->lastSelTrack = 3;
        const uint64_t w = e.addWindow (std::move (doc));
        se::Recorder rec (e);
        se::RecordSetup s; s.source.stereo = true; s.incomingStereo = true;
        check (rec.start (w, s) && rec.targetTracks().size() == 2, "selection over 2 tracks: starts");
        settleRec (e, rec, { 0.25f, -0.5f });
        const auto* d = e.editDoc (w);
        check (at (*d, 1, 60) == 8192 && at (*d, 2, 60) == -16384, "2 tracks: incoming left on the upper track, right on the lower");
        check (at (*d, 0, 60) == 0 && at (*d, 1, 40) == 0 && at (*d, 2, 160) == 0, "2 tracks: the other tracks and times untouched");
    }

    // Loop on: continuous recording round and round inside the selection, over what was there.
    {
        se::Engine e; e.prepare (kRate, kBlock);
        auto doc = blankSound (1, kRate);
        doc->tracks[0].data.assign (1000, 0);
        doc->selSt = 100; doc->selEnd = 300; doc->firstSelTrack = doc->lastSelTrack = 1;   // 100 frames: 50..149
        const uint64_t w = e.addWindow (std::move (doc));
        se::Recorder rec (e);
        se::RecordSetup s; s.source.stereo = false; s.incomingStereo = false; s.loop = true;
        rec.start (w, s);
        pump (e, rec, 2, { 0.25f });       // frames 0..127 of the pass
        pump (e, rec, 1, { 0.5f });        // frames 128..191
        for (int i = 0; i < 6; ++i) pump (e, rec, 1, { 0.5f });
        // 9 blocks = 576 frames: the pass is far past the span, and still going.
        check (rec.state() == se::Recorder::State::recording, "loop: keeps recording past the selection's length");
        rec.stop();
        settleRec (e, rec, { 0.5f });
        const auto* d = e.editDoc (w);
        // Every position was last written with 0.5 (the final blocks are 0.5).
        check (at (*d, 0, 50) == 16384 && at (*d, 0, 100) == 16384 && at (*d, 0, 149) == 16384, "loop: round and round, the last pass wins");
        check (at (*d, 0, 49) == 0 && at (*d, 0, 150) == 0, "loop: never outside the selection");
        check (d->tracks[0].data.size() == 1000, "loop: the track keeps its length");
    }

    // Loop on, a short pass: positions the pass has not wrapped over keep the earlier take.
    {
        se::Engine e; e.prepare (kRate, kBlock);
        auto doc = blankSound (1, kRate);
        doc->tracks[0].data.assign (1000, 0);
        doc->selSt = 0; doc->selEnd = 200; doc->firstSelTrack = doc->lastSelTrack = 1;   // 100 frames
        const uint64_t w = e.addWindow (std::move (doc));
        se::Recorder rec (e);
        se::RecordSetup s; s.source.stereo = false; s.incomingStereo = false; s.loop = true;
        rec.start (w, s);
        pump (e, rec, 2, { 0.25f });        // k 0..127
        pump (e, rec, 1, { 0.5f });         // k 128..191
        rec.stop();
        settleRec (e, rec, { 0.5f });
        const auto* d = e.editDoc (w);
        // Position p was last written by k = p + 100 (if < 192) else p.
        check (at (*d, 0, 10) == 8192, "loop wrap: p < 28 was last written at k = p + 100 < 128 (0.25)");
        check (at (*d, 0, 50) == 16384, "loop wrap: 28 <= p < 92 at k = p + 100 >= 128 (0.5)");
        check (at (*d, 0, 95) == 8192, "loop wrap: p >= 92 only by the first pass (0.25)");
    }

    // A track shorter than the selection's end is padded with silence to make room.
    {
        se::Engine e; e.prepare (kRate, kBlock);
        auto doc = blankSound (1, kRate, 8);
        doc->tracks[0].data.assign (100, 0x80);                 // 8 bit: 0x80 is silence
        doc->selSt = 50; doc->selEnd = 150; doc->firstSelTrack = doc->lastSelTrack = 1;
        const uint64_t w = e.addWindow (std::move (doc));
        se::Recorder rec (e);
        se::RecordSetup s; s.source.stereo = false; s.incomingStereo = false;
        rec.start (w, s);
        settleRec (e, rec, { 0.25f });
        const auto* d = e.editDoc (w);
        check (d->tracks[0].data.size() == 150, "short track: padded up to the selection's end");
        check (at (*d, 0, 120) == 32 && at (*d, 0, 40) == 0, "short track: recorded where it was padded, untouched before");
    }

    // The others play while a selection records: only the track being written is silent.
    {
        se::Engine e; e.prepare (kRate, kBlock);
        auto doc = dcDocument (10, "x");                         // track 1 (8-bit DC 10)
        doc->addTrack(); doc->addTrack();
        for (int t = 1; t < 3; ++t)
        {
            doc->tracks[(size_t) t].data.assign (512, 0x80);
            const int lvl = t == 1 ? 20 : 40;
            for (size_t i = 0; i < 512; ++i) se::setSample (doc->tracks[(size_t) t].data.data(), i, lvl, 8);
        }
        doc->selSt = 10; doc->selEnd = 50; doc->firstSelTrack = doc->lastSelTrack = 2;
        const uint64_t w = e.addWindow (std::move (doc));
        se::Recorder rec (e);
        se::RecordSetup s; s.source.stereo = false; s.incomingStereo = false;
        rec.start (w, s);
        std::vector<float> ol, orr;
        renderWithInput (e, { 0.25f }, ol, orr);
        checkClose (ol[2], (10.0f + 40.0f) / 128.0f, 1.0e-4f, "punch: tracks 1 and 3 play, the track being written (2) does not");
        settleRec (e, rec, { 0.25f });
    }

    // With Loop on and only the written track selected, nothing else sounds in the loop.
    {
        se::Engine e; e.prepare (kRate, kBlock);
        auto doc = dcDocument (20, "y");
        doc->selSt = 10; doc->selEnd = 50; doc->firstSelTrack = doc->lastSelTrack = 1;
        const uint64_t w = e.addWindow (std::move (doc));
        se::Recorder rec (e);
        se::RecordSetup s; s.source.stereo = false; s.incomingStereo = false; s.loop = true;
        rec.start (w, s);
        std::vector<float> ol, orr;
        renderWithInput (e, { 0.25f }, ol, orr);
        checkClose (ol[2], 0.0f, 1.0e-6f, "loop: the selected track is the one written, so the loop is silent");
        rec.stop();
        settleRec (e, rec, { 0.25f });
    }

    // Selection / 3+ tracks: not allowed.
    {
        se::Engine e; e.prepare (kRate, kBlock);
        auto doc = blankSound (3, kRate);
        for (auto& t : doc->tracks) t.data.assign (200, 0);
        doc->selSt = 20; doc->selEnd = 100; doc->firstSelTrack = 1; doc->lastSelTrack = 3;
        check (! se::Recorder::canRecord (*doc), "3 selected tracks: not allowed");
        doc->lastSelTrack = 2;
        check (se::Recorder::canRecord (*doc), "2 selected tracks: allowed");
        doc->selSt = doc->selEnd = 0; doc->lastSelTrack = 3;
        check (se::Recorder::canRecord (*doc), "no selection: always allowed (new tracks)");
    }
}

// A Recorder driven from the message thread while the audio thread renders: takes
// started, paused, resumed and stopped back to back, each with its ring drained,
// conversion and document writes, under TSan.
void recordConcurrencyTests()
{
    se::Engine e;
    e.prepare (kRate, kBlock);
    const uint64_t w = e.addWindow (dcDocument (30, "w"));
    const uint64_t other = e.addWindow (dcDocument (10, "o"));
    se::PlayOptions loopAll; loopAll.loop = true;
    e.play (other, loopAll);

    std::atomic<bool> done { false };
    std::atomic<bool> sane { true };
    std::thread audio ([&]
    {
        std::vector<float> in0 ((size_t) kBlock, 0.25f), in1 ((size_t) kBlock, -0.25f), l ((size_t) kBlock), r ((size_t) kBlock);
        const float* ins[2] = { in0.data(), in1.data() };
        while (! done.load())
        {
            e.render (l.data(), r.data(), kBlock, ins, 2);
            for (int i = 0; i < kBlock; ++i)
                if (! std::isfinite (l[(size_t) i]) || std::fabs (l[(size_t) i]) > 1.0f) sane = false;
            std::this_thread::sleep_for (std::chrono::microseconds (100));   // a device does not run flat out
        }
    });

    se::Recorder rec (e);
    size_t takes = 0;
    for (int i = 0; i < 400; ++i)
    {
        se::RecordSetup s;
        s.source.kind = (i % 3 == 0) ? se::AudioSourceKind::canvas : se::AudioSourceKind::external;
        s.incomingStereo = (i % 2) == 0;
        s.mode = (i % 4 == 0) ? se::ConvertMode::clean : se::ConvertMode::raw;
        if (rec.start (w, s)) ++takes;
        for (int k = 0; k < 6; ++k) { rec.service(); std::this_thread::sleep_for (std::chrono::microseconds (150)); }
        if (i % 3 == 1)
        {
            rec.pause();
            for (int k = 0; k < 40 && rec.state() != se::Recorder::State::paused; ++k) { rec.service(); std::this_thread::sleep_for (std::chrono::microseconds (150)); }
            rec.resume();
            for (int k = 0; k < 4; ++k) { rec.service(); std::this_thread::sleep_for (std::chrono::microseconds (150)); }
        }
        rec.stop();
        for (int k = 0; k < 80 && rec.state() != se::Recorder::State::idle; ++k) { rec.service(); std::this_thread::sleep_for (std::chrono::microseconds (150)); }
        if (rec.state() != se::Recorder::State::idle) sane = false;
        e.collectRetired();
        // Keep the Sound from growing without bound across the run.
        if (auto* d = e.editDoc (w)) { while (d->numTracks() > 2) d->delTrack (d->numTracks()); e.commitEdit (w); }
    }
    done = true;
    audio.join();
    check (sane.load(), "record stress: output stays finite and every take ends");
    check (takes > 100, "record stress: takes started and ended under the audio thread");
}

// --- audio input / capture (9.1) ----------------------------------------------
// The audio thread captures a source into the ring; the message thread (here: the
// test) pops it. Sources: a device channel / pair, the Canvas main mix (minus the
// Window being recorded into), one Window's post-fader tap, the DAW input.

/** Render one block with `numIn` constant input channels; returns the output. */
void renderWithInput (se::Engine& e, std::initializer_list<float> levels, std::vector<float>& outL, std::vector<float>& outR)
{
    std::vector<std::vector<float>> in;
    for (float v : levels) in.emplace_back ((size_t) kBlock, v);
    std::vector<const float*> ptrs;
    for (auto& c : in) ptrs.push_back (c.data());
    outL.assign ((size_t) kBlock, 0.0f); outR.assign ((size_t) kBlock, 0.0f);
    e.render (outL.data(), outR.data(), kBlock, ptrs.data(), (int) ptrs.size());
}

/** Everything the ring holds, as (first left, first right, frame count). */
struct Drained { float l = 0, r = 0; size_t n = 0; };
Drained drain (se::Engine& e)
{
    std::vector<float> l (4096), r (4096);
    Drained d;
    for (;;)
    {
        const size_t got = e.captureRing().pop (l.data(), r.data(), l.size());
        if (got == 0) break;
        if (d.n == 0) { d.l = l[0]; d.r = r[0]; }
        d.n += got;
    }
    return d;
}

void captureTests()
{
    const float tol = 1.0e-5f;
    std::vector<float> ol, orr;

    {
        se::Engine e;
        e.prepare (kRate, kBlock);
        check (! e.captureRunning(), "capture: not running before it is started");
        renderWithInput (e, { 0.25f, -0.5f, 0.125f }, ol, orr);
        check (drain (e).n == 0, "capture: a block rendered before the start captures nothing");

        se::CaptureSetup pair; pair.kind = se::AudioSourceKind::external; pair.firstChannel = 0; pair.stereo = true;
        e.startCapture (pair);
        renderWithInput (e, { 0.25f, -0.5f, 0.125f }, ol, orr);
        check (e.captureRunning(), "capture: running once the audio thread has seen the start");
        Drained d = drain (e);
        check (d.n == (size_t) kBlock, "capture: one block in, one block out");
        checkClose (d.l, 0.25f, tol, "external pair: left is channel 0");
        checkClose (d.r, -0.5f, tol, "external pair: right is channel 1");
        checkClose (ol[0], 0.0f, tol, "monitor off by default: the output stays silent");

        se::CaptureSetup mono; mono.kind = se::AudioSourceKind::external; mono.firstChannel = 2; mono.stereo = false;
        e.startCapture (mono);
        renderWithInput (e, { 0.25f, -0.5f, 0.125f }, ol, orr);
        d = drain (e);
        checkClose (d.l, 0.125f, tol, "external mono: the chosen channel on the left");
        checkClose (d.r, 0.125f, tol, "external mono: ...and duplicated on the right");

        e.setInputGain (0.5f);
        renderWithInput (e, { 0.25f, -0.5f, 0.5f }, ol, orr);
        d = drain (e);
        checkClose (d.l, 0.25f, tol, "input gain 0.5 scales the capture");

        e.setInputGain (4.0f);
        renderWithInput (e, { 0.5f, 0.0f, 0.5f }, ol, orr);
        d = drain (e);
        checkClose (d.l, 1.0f, tol, "input gain past full scale clips at 1.0");
        e.setInputGain (1.0f);

        e.setMonitor (true);
        e.startCapture (pair);
        renderWithInput (e, { 0.25f, -0.5f, 0.0f }, ol, orr);
        drain (e);
        checkClose (ol[0], 0.25f, tol, "monitor on: the pair is heard, left");
        checkClose (orr[0], -0.5f, tol, "monitor on: the pair is heard, right");
        e.startCapture (mono);
        renderWithInput (e, { 0.25f, -0.5f, 0.125f }, ol, orr);
        drain (e);
        checkClose (ol[0], 0.125f, tol, "monitor on: a mono channel is heard on the left...");
        checkClose (orr[0], 0.125f, tol, "monitor on: ...and on the right");
        e.setMonitor (false);

        se::CaptureSetup daw; daw.kind = se::AudioSourceKind::daw;
        e.startCapture (daw);
        renderWithInput (e, { 0.3f, 0.6f, 0.9f }, ol, orr);
        d = drain (e);
        checkClose (d.l, 0.3f, tol, "DAW: the host's input pair, left");
        checkClose (d.r, 0.6f, tol, "DAW: the host's input pair, right");

        e.stopCapture();
        renderWithInput (e, { 0.25f, -0.5f, 0.125f }, ol, orr);
        check (! e.captureRunning(), "capture: stopped once the audio thread has seen the stop");
        check (drain (e).n == 0, "capture: nothing after the stop");

        // No input at all (a device without one): silence, not a crash.
        e.startCapture (pair);
        std::vector<float> l ((size_t) kBlock), r ((size_t) kBlock);
        e.render (l.data(), r.data(), kBlock);
        d = drain (e);
        check (d.n == (size_t) kBlock && d.l == 0.0f && d.r == 0.0f, "external with no input channels captures silence");
        e.stopCapture();
        e.render (l.data(), r.data(), kBlock);
    }

    {
        // The Canvas source.
        se::Engine e;
        e.prepare (kRate, kBlock);
        const uint64_t a = e.addWindow (dcDocument (10, "a"));
        const uint64_t b = e.addWindow (dcDocument (20, "b"));
        const uint64_t c = e.addWindow (dcDocument (40, "c"));
        se::PlayOptions loopAll; loopAll.loop = true;
        e.play (a, loopAll); e.play (b, loopAll); e.play (c, loopAll);

        auto* wa = e.windowById (a); auto* wb = e.windowById (b); auto* wc = e.windowById (c);
        wa->soloed = true; const float la = renderLevel (e);
        wa->soloed = false; wb->soloed = true; const float lb = renderLevel (e);
        wb->soloed = false; wc->soloed = true; const float lc = renderLevel (e);
        wc->soloed = false;

        se::CaptureSetup mix; mix.kind = se::AudioSourceKind::canvas;
        e.startCapture (mix);
        renderBlocks (e, 1);
        Drained d = drain (e);
        checkClose (d.l, la + lb + lc, tol, "canvas main mix: the sum of every Window");

        mix.excludeWindow = b;
        e.startCapture (mix);
        renderBlocks (e, 1);
        d = drain (e);
        checkClose (d.l, la + lc, tol, "canvas main mix: the Window being recorded into is left out");

        wb->muted = true;
        renderBlocks (e, 1);
        d = drain (e);
        checkClose (d.l, la + lc, tol, "canvas main mix: a muted excluded Window is not subtracted twice");
        wb->muted = false;

        wc->muted = true;
        renderBlocks (e, 1);
        d = drain (e);
        checkClose (d.l, la, tol, "canvas main mix: a muted Window is not in it");
        wc->muted = false;

        se::CaptureSetup tap; tap.kind = se::AudioSourceKind::canvas; tap.tapWindow = b;
        e.startCapture (tap);
        wb->gain = 0.5f;
        renderBlocks (e, 1);
        d = drain (e);
        checkClose (d.l, lb * 0.5f, tol, "canvas tap: one Window, post-fader");
        wb->balance = 1.0f;   // hard right: the left side falls to silence
        renderBlocks (e, 1);
        d = drain (e);
        checkClose (d.l, 0.0f, tol, "canvas tap: post-balance (left)");
        checkClose (d.r, lb * 0.5f, tol, "canvas tap: post-balance (right)");
        wb->balance = 0.0f;
        wb->muted = true;
        renderBlocks (e, 1);
        d = drain (e);
        checkClose (d.l, lb * 0.5f, tol, "canvas tap: a muted Window can still be sampled");
        wb->muted = false;

        // Monitoring never doubles a Canvas source: it is audible already.
        e.setMonitor (true);
        e.startCapture (mix);
        std::vector<float> l ((size_t) kBlock), r ((size_t) kBlock);
        wb->gain = 1.0f;
        e.render (l.data(), r.data(), kBlock);
        drain (e);
        checkClose (l[0], la + lb + lc, tol, "canvas source: monitor on does not double the mix");
    }

    {
        // A full ring drops what does not fit and counts it; the audio thread never waits.
        se::Engine e;
        e.prepare (kRate, kBlock);
        se::CaptureSetup pair; pair.kind = se::AudioSourceKind::external;
        e.startCapture (pair);
        const size_t blocks = e.captureRing().capacity() / (size_t) kBlock + 8;
        for (size_t i = 0; i < blocks; ++i) renderWithInput (e, { 0.1f, 0.1f }, ol, orr);
        check (e.captureRing().droppedFrames() > 0, "ring: an overflow is counted, not blocked on");
        check (e.captureRing().available() == e.captureRing().capacity() - 1, "ring: it holds capacity - 1 frames when full");
        drain (e);
        e.captureRing().resetDropped();
        renderWithInput (e, { 0.1f, 0.1f }, ol, orr);
        check (drain (e).n == (size_t) kBlock && e.captureRing().droppedFrames() == 0, "ring: works again once drained");
    }
}

// The message thread starts / stops captures, drains the ring and turns the input
// gain and the monitor while the audio thread renders a Canvas source and an
// external one: what TSan is for. Every frame that arrives must be a frame the
// audio thread wrote (a ramp, so a torn or reordered frame shows).
void captureConcurrencyTests()
{
    se::Engine e;
    e.prepare (kRate, kBlock);
    const uint64_t a = e.addWindow (dcDocument (30, "a"));
    se::PlayOptions loopAll; loopAll.loop = true;
    e.play (a, loopAll);

    std::atomic<bool> done { false };
    std::atomic<bool> sane { true };
    std::atomic<size_t> frames { 0 };

    std::thread audio ([&]
    {
        std::vector<float> in0 ((size_t) kBlock), in1 ((size_t) kBlock), l ((size_t) kBlock), r ((size_t) kBlock);
        const float* ins[2] = { in0.data(), in1.data() };
        while (! done.load())
        {
            for (int i = 0; i < kBlock; ++i) { in0[(size_t) i] = 0.25f; in1[(size_t) i] = -0.25f; }
            e.render (l.data(), r.data(), kBlock, ins, 2);
            for (int i = 0; i < kBlock; ++i)
                if (! std::isfinite (l[(size_t) i]) || std::fabs (l[(size_t) i]) > 1.0f) sane = false;
        }
    });

    std::vector<float> l (2048), r (2048);
    for (int i = 0; i < 3000; ++i)
    {
        se::CaptureSetup s;
        s.kind = (i % 3 == 0) ? se::AudioSourceKind::external : se::AudioSourceKind::canvas;
        s.tapWindow = (i % 2) ? a : 0;
        s.excludeWindow = (i % 5 == 0) ? a : 0;
        e.startCapture (s);
        e.setInputGain (0.5f + 0.001f * (float) (i % 50));
        e.setMonitor ((i % 7) == 0);
        size_t got;
        while ((got = e.captureRing().pop (l.data(), r.data(), l.size())) > 0)
        {
            frames += got;
            for (size_t k = 0; k < got; ++k)
                if (! std::isfinite (l[k]) || std::fabs (l[k]) > 1.0f || std::fabs (r[k]) > 1.0f) sane = false;
        }
        if (i % 11 == 0) e.stopCapture();
        if (i % 97 == 0) std::this_thread::sleep_for (std::chrono::microseconds (200));
        e.collectRetired();
    }
    done = true;
    audio.join();
    check (sane.load(), "capture stress: every frame finite and in range");
    check (frames.load() > 0, "capture stress: frames arrived while the message thread started and stopped");
}

int main()
{
    se::Engine engine;
    engine.prepare (kRate, kBlock);

    // Three channels at deliberately different levels, so which ones are in the sum
    // can be read straight off the output.
    const uint64_t a = engine.addWindow (dcDocument (10, "a"));
    const uint64_t b = engine.addWindow (dcDocument (20, "b"));
    const uint64_t c = engine.addWindow (dcDocument (40, "c"));
    check (a != 0 && b != 0 && c != 0, "three Windows added");

    auto* wa = engine.windowById (a);
    auto* wb = engine.windowById (b);
    auto* wc = engine.windowById (c);

    se::PlayOptions loopAll;
    loopAll.loop = true;   // no selection: every track loops over its own length
    engine.play (a, loopAll);
    engine.play (b, loopAll);
    engine.play (c, loopAll);

    // --- what each channel is worth on its own -------------------------------
    // Measured, not predicted: the integer sample path's exact scaling is the
    // effects' business, and these tests are about the mixer above it.
    wa->soloed = true;
    const float la = renderLevel (engine);
    wa->soloed = false; wb->soloed = true;
    const float lb = renderLevel (engine);
    wb->soloed = false; wc->soloed = true;
    const float lc = renderLevel (engine);
    wc->soloed = false;

    check (la > 0.0f && lb > la && lc > lb, "each channel alone sounds, at its own level");

    const float tol = 1.0e-4f;

    // --- no solo: everything sums -------------------------------------------
    check (! engine.anyWindowSoloed(), "no solo: the filter is off");
    checkClose (renderLevel (engine), la + lb + lc, tol, "no solo: all three sum");

    // --- solo is a filter ----------------------------------------------------
    wb->soloed = true;
    check (engine.anyWindowSoloed(), "one solo: the filter is on");
    checkClose (renderLevel (engine), lb, tol, "solo b: only b sounds");

    // --- and it is CUMULATIVE ------------------------------------------------
    wc->soloed = true;
    checkClose (renderLevel (engine), lb + lc, tol, "solo b + c: both sound, a does not");

    wa->soloed = true;
    checkClose (renderLevel (engine), la + lb + lc, tol, "all three soloed: same as none");
    wa->soloed = false;

    // --- mute outranks solo on the same channel ------------------------------
    wb->muted = true;
    checkClose (renderLevel (engine), lc, tol, "muted AND soloed: mute wins");
    wb->muted = false;

    wb->soloed = false; wc->soloed = false;
    check (! engine.anyWindowSoloed(), "solo cleared: the filter is off again");
    checkClose (renderLevel (engine), la + lb + lc, tol, "solo cleared: all three back");

    // --- ⌘-click on a lit box: clear it everywhere (step 12.3) -----------------
    wa->soloed = true; wc->soloed = true; wb->muted = true; wc->muted = true;
    engine.clearAllSolos();
    check (! engine.anyWindowSoloed(), "clearAllSolos: no solo left");
    check (wb->muted && wc->muted, "clearAllSolos: mutes untouched");
    wa->soloed = true;
    engine.unmuteAll();
    check (! wa->muted && ! wb->muted && ! wc->muted, "unmuteAll: nothing muted");
    check (wa->soloed, "unmuteAll: solos untouched");
    wa->soloed = false;
    checkClose (renderLevel (engine), la + lb + lc, tol, "after clearing: all three back");

    // --- gain --------------------------------------------------------------
    wa->gain = 0.5f;
    checkClose (renderLevel (engine), la * 0.5f + lb + lc, tol, "gain 0.5 halves that channel");
    wa->gain = 1.0f;

    // --- balance: linear, one-sided ------------------------------------------
    // The DC documents route to both sides, so with no balance the right channel
    // carries the same sum as the left.
    {
        const auto centre = se::Engine::balanceGains (0.0f);
        const auto half   = se::Engine::balanceGains (-0.5f);
        const auto hardR  = se::Engine::balanceGains (1.0f);
        const auto past   = se::Engine::balanceGains (-3.0f);
        check (centre.left == 1.0f && centre.right == 1.0f, "balance 0: unity on both sides");
        check (half.left == 1.0f && half.right == 0.5f, "balance -0.5: left unity, right halved");
        check (hardR.left == 0.0f && hardR.right == 1.0f, "balance +1: left silent, right unity");
        check (past.left == 1.0f && past.right == 0.0f, "balance clamps to the end stop");
    }
    {
        std::vector<float> l ((size_t) kBlock), r ((size_t) kBlock);
        auto render = [&] { engine.render (l.data(), r.data(), kBlock); };

        render();
        checkClose (r[0], la + lb + lc, tol, "centred: right carries the full sum too");

        wb->balance = -0.5f;     // b leans left: its right side halves
        render();
        checkClose (l[0], la + lb + lc, tol, "b at -0.5: the left sum is untouched");
        checkClose (r[0], la + lb * 0.5f + lc, tol, "b at -0.5: b's right is halved");

        wb->balance = 1.0f;      // b hard right: gone from the left
        render();
        checkClose (l[0], la + lc, tol, "b hard right: absent from the left");
        checkClose (r[0], la + lb + lc, tol, "b hard right: full on the right");
        wb->balance = 0.0f;
    }

    // --- the stereo VU feed ----------------------------------------------------
    // Each side's meter is that Window's OWN post-gain, post-balance peak.
    {
        // The meters hold and decay, so first let what earlier checks lit fall
        // away (~-60 dB per 300 ms; 400 blocks is well over a second of silence).
        wa->muted = wb->muted = wc->muted = true;
        renderBlocks (engine, 400);
        wa->muted = wb->muted = wc->muted = false;
        check (wa->meterLeft.load() < 1.0e-4f && wb->meterRight.load() < 1.0e-4f,
               "VU: silence drains the meters");

        wa->balance = 1.0f;      // a: right only
        wb->gain = 0.5f;         // b: both sides at half
        renderBlocks (engine, 1);
        checkClose (wa->meterLeft.load(), 0.0f, 1.0e-3f, "VU: a hard right reads nothing on the left");
        checkClose (wa->meterRight.load(), la, tol, "VU: a's right reads its own level");
        checkClose (wb->meterLeft.load(), lb * 0.5f, tol, "VU: b's left reads its post-gain level");
        checkClose (wb->meterRight.load(), lb * 0.5f, tol, "VU: b's right likewise");

        // Silence: the meters fall on their own, block by block.
        wa->muted = true;
        const float before = wa->meterRight.load();
        renderBlocks (engine, 4);
        const float after = wa->meterRight.load();
        check (after < before && after > 0.0f, "VU: a muted channel's meter decays, not drops");
        wa->muted = false; wa->balance = 0.0f; wb->gain = 1.0f;
    }

    // --- an unheard channel still ROLLS -------------------------------------
    // The whole point of running several Windows at once is loops of different
    // lengths phasing against each other. A channel silenced by mute, by somebody
    // else's solo, or by its own fader must keep advancing, or it comes back at the
    // wrong point and the phase relationship is gone.
    renderBlocks (engine, 20);
    const size_t startA = wa->transport->uiPlayhead();
    const size_t startB = wb->transport->uiPlayhead();
    const size_t startC = wc->transport->uiPlayhead();
    check (startA == startB && startB == startC && startA > 0,
           "three channels started together are in phase");

    wa->muted = true;       // silenced by mute
    wb->soloed = true;      // c is now silenced by b's solo
    wc->gain = 0.0f;        // and c's fader is shut as well
    renderBlocks (engine, 50);

    const size_t endA = wa->transport->uiPlayhead();
    const size_t endB = wb->transport->uiPlayhead();
    const size_t endC = wc->transport->uiPlayhead();

    check (endB > startB, "the audible channel advanced");
    check (endA == endB, "a muted channel keeps rolling, in phase");
    check (endC == endB, "a channel silenced by solo and a shut fader keeps rolling");

    wa->muted = false; wb->soloed = false; wc->gain = 1.0f;
    checkClose (renderLevel (engine), la + lb + lc, tol, "and all three come back");

    // --- mixer slots ---------------------------------------------------------
    // a, b, c were added in that order: slots 0, 1, 2.
    check (wa->slot == 0 && wb->slot == 1 && wc->slot == 2, "slots: new Sounds take 0, 1, 2 in order");
    check (engine.windowInSlot (1) == b && engine.windowInSlot (7) == 0, "slots: who sits where");

    engine.removeWindow (b);   // slots are 1..N with no gaps: c moves up into 1
    check (wa->slot == 0 && wc->slot == 1 && engine.highestSlot() == 1, "slots: Close compacts");
    const uint64_t d = engine.addWindow (dcDocument (5, "d"));
    check (engine.windowById (d)->slot == 2, "slots: a new Sound goes last");
    auto* wd = engine.windowById (d);

    // a c d: moving is an insert.
    check (engine.moveToSlot (d, 0) && wd->slot == 0 && wa->slot == 1 && wc->slot == 2,
           "slots: moving to the top pushes the others down");
    check (engine.moveToSlot (d, 2) && wa->slot == 0 && wc->slot == 1 && wd->slot == 2,
           "slots: moving down closes up behind it");
    check (engine.moveToSlot (a, 9) && wc->slot == 0 && wd->slot == 1 && wa->slot == 2,
           "slots: past the end lands last");

    wa->slot = 1;              // a duplicate of d's, and c unseated: what a damaged file gives
    wc->slot = se::Window::kNoSlot;
    engine.normaliseSlots();   // order a, c, d: a keeps 1, then c and d take 0 and 2
    check (wa->slot == 1 && wc->slot == 0 && wd->slot == 2,
           "slots: normalising keeps the first claimant and reseats the rest lowest-first");
    wa->slot = 4; wc->slot = 9; wd->slot = 0;   // gaps from an old file
    engine.normaliseSlots();
    check (wd->slot == 0 && wa->slot == 1 && wc->slot == 2, "slots: normalising closes the gaps, order kept");
    engine.collectRetired();

    // --- Edit ▸ Duplicate (0.4.0, no 1994 original) ---------------------------
    {
        se::DocumentInit init;
        init.numTracks = 2;
        init.sizeBytes = 8;
        se::SoundDocument dup (init);
        for (size_t i = 0; i < 8; ++i) { dup.tracks[0].data[i] = (uint8_t) i; dup.tracks[1].data[i] = (uint8_t) (10 + i); }
        dup.tracks[1].data.resize (3);   // ragged: track 2 ends inside the selection
        dup.firstSelTrack = 1; dup.lastSelTrack = 2;
        dup.selSt = 2; dup.selEnd = 5;
        check (se::duplicate (dup), "duplicate: a selection is duplicated");
        const std::vector<uint8_t> t1 { 0, 1, 2, 3, 4, 2, 3, 4, 5, 6, 7 };
        const std::vector<uint8_t> t2 { 10, 11, 12, 0x80, 0x80, 12 };   // padded to the end, then its own part
        check (dup.tracks[0].data == t1, "duplicate: the copy lands right after the selection");
        check (dup.tracks[1].data == t2, "duplicate: a short track is padded to the selection's end first");
        check (dup.selSt == 5 && dup.selEnd == 8, "duplicate: the copy is selected");
        dup.selSt = dup.selEnd = 4;
        check (! se::duplicate (dup), "duplicate: a caret does nothing");
    }

    // --- Selection ▸ Extend / Set Loop / Select Loop (CODE 4) ----------------
    {
        se::DocumentInit init;
        init.numTracks = 3;
        init.sizeBytes = 6;
        se::SoundDocument d (init);
        d.tracks[2].data.resize (4);
        d.firstSelTrack = d.lastSelTrack = 3;
        d.selSt = 1; d.selEnd = 2;
        check (se::extendToEnd (d) && d.selEnd == 4, "extend to end: the selected tracks' longest end");
        check (se::extendToBeginning (d) && d.selSt == 0, "extend to beginning: start at 0");
        check (! se::extendBelow (d) && se::extendAbove (d) && se::extendAbove (d) && ! se::extendAbove (d)
               && d.firstSelTrack == 1 && d.lastSelTrack == 3, "extend above/below: one track at a time, up to the edges");
        d.selSt = 2; d.selEnd = 5;
        se::setLoop (d);
        d.firstSelTrack = d.lastSelTrack = 2; d.selSt = d.selEnd = 0;
        check (se::selectLoop (d) && d.selSt == 2 && d.selEnd == 5 && d.firstSelTrack == 1 && d.lastSelTrack == 3,
               "select loop: the loop region, every track");
    }

    directionTests();
    directionEngineTests();
    switchingTests();
    concurrencyTests();
    deselectSyncTests();
    deselectConcurrencyTests();
    captureTests();
    convertTests();
    recordTests();
    captureConcurrencyTests();
    recordConcurrencyTests();

    std::printf ("\n%s\n", failures == 0 ? "engine: all checks passed"
                                         : (std::to_string (failures) + " FAILED").c_str());
    return failures == 0 ? 0 : 1;
}
