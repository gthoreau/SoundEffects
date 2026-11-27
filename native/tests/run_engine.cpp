// Engine tests — the desktop-wide mixer: per-Window gain, balance, mute, the
// CUMULATIVE solo filter and the stereo VU feed, plus the rule that none of
// them may stop a transport.
//
// These are the parts of the Engine that have no golden vector to check them
// against (there is no 1994 original for a mixer that never existed), so they are
// asserted directly. JUCE-free, like everything else under Source/engine/.

#include "engine/Edit.h"
#include "engine/KeyboardVoice.h"
#include "engine/MidiNotes.h"
#include "engine/Engine.h"
#include "engine/RecordConvert.h"
#include "engine/Recorder.h"
#include "engine/Resample.h"
#include "engine/Tape.h"
#include "engine/SampleFormat.h"
#include "engine/TransportButtons.h"
#include "ui/ScrollTarget.h"
#include "run_declick.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
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
        se::Transport tr (kRate, 0.0);
        tr.play (*doc, loop);
        check (isSequence (renderTransport (tr, *doc, 250).left, walk (0, kRamp, 0, 1, false, 250)),
               "direction: forward loops 0..99 as always");
    }
    // Backward and ping-pong, looping the whole sound.
    for (int pp = 0; pp < 2; ++pp)
    {
        auto doc = rampDocument ({ kRamp });
        se::Transport tr (kRate, 0.0);
        tr.setDirection (pp ? PlayDirection::pingPong : PlayDirection::backward, *doc);   // stopped: kept
        tr.play (*doc, loop);
        const auto want = pp ? walk (0, kRamp, 0, 1, true, 450) : walk (0, kRamp, kRamp - 1, -1, false, 250);
        check (isSequence (renderTransport (tr, *doc, (int) want.size()).left, want),
               pp ? "direction: ping-pong runs up, bounces, runs down, bounces" : "direction: backward loops 99..0");
    }
    // Tracks of different lengths each keep their own length and phase.
    {
        auto doc = rampDocument ({ 100, 60 });
        se::Transport tr (kRate, 0.0);
        tr.setDirection (PlayDirection::backward, *doc);
        tr.play (*doc, loop);
        const auto o = renderTransport (tr, *doc, 300);
        check (isSequence (o.left, walk (0, 100, 99, -1, false, 300)) && isSequence (o.right, walk (0, 60, 59, -1, false, 300)),
               "direction: backward, two tracks of 100 and 60 loop over their own lengths");
    }
    {
        auto doc = rampDocument ({ 100, 60 });
        se::Transport tr (kRate, 0.0);
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
        se::Transport tr (kRate, 0.0);
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
        se::Transport tr (kRate, 0.0);
        tr.setDirection (PlayDirection::backward, *doc);
        tr.play (*doc, {});
        const auto o = renderTransport (tr, *doc, 300);
        check (o.frames == kRamp && isSequence (o.left, walk (0, kRamp, kRamp - 1, -1, false, kRamp)) && tr.state() == se::TransportState::stopped,
               "direction: once backward plays 99..0 then stops");
    }
    {
        auto doc = rampDocument ({ kRamp });
        se::Transport tr (kRate, 0.0);
        tr.setDirection (PlayDirection::pingPong, *doc);
        tr.play (*doc, {});
        const auto o = renderTransport (tr, *doc, 500);
        check (o.frames == 2 * kRamp && isSequence (o.left, walk (0, kRamp, 0, 1, true, 2 * kRamp)) && tr.state() == se::TransportState::stopped,
               "direction: once ping-pong is one round trip, then stops");
    }
    {
        auto doc = rampDocument ({ kRamp });
        doc->selSt = 30; doc->selEnd = 50;
        se::Transport tr (kRate, 0.0);
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
        se::Transport tr (kRate, 0.0);
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
        se::Transport tr (kRate, 0.0);
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
        se::Transport tr (kRate, 0.0);
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
        se::Transport tr (kRate, 0.0);
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
        se::Transport tr (kRate, 0.0);
        tr.setDirection (PlayDirection::backward, *doc);
        tr.play (*doc, loop);
        tr.setRateScale (2.0);
        const auto o = renderTransport (tr, *doc, 10);
        check (o.left[0] == 99 && o.left[1] == 97 && o.left[2] == 95, "direction: a re-pitched backward loop steps by two");
        tr.advance (*doc);
        check (true, "direction: advance accepted");
    }
}

// --- 0.5.0 step 3: playback range ------------------------------------------
// Ramps are 8-bit mono at the device rate, so bytes == frames. Track 0 is heard on
// the left, track 1 on the right; where a test needs to know whether a track SOUNDS
// rather than where it reads, `silent` checks the raw samples (a ramp is 0.0 only at
// index 50, so a run of zeros is silence).

bool silent (const std::vector<float>& v, size_t from, size_t to)
{
    for (size_t i = from; i < to && i < v.size(); ++i) if (v[i] != 0.0f) return false;
    return true;
}

std::vector<float> rawRight (se::Transport& tr, const se::SoundDocument& doc, int frames, std::vector<float>& left)
{
    std::vector<float> l ((size_t) frames, 0.0f), r ((size_t) frames, 0.0f);
    tr.render (doc, l.data(), r.data(), frames);
    left = l;
    return r;
}

// 3.1: today's loop-on behaviour, pinned before the play-once and caret changes.
// Every PlayOptions here is one the Keyboard, the Recorder or 0.4's Play button
// passes; none of steps 3.2 / 3.3 may change what these hear.
void loopOnPinTests()
{
    se::PlayOptions loop; loop.loop = true;
    se::PlayOptions region; region.loop = true; region.useLoopRegion = true;   // the Play button's
    const auto at = [] (se::Transport& tr, int t) { size_t p = 0; return tr.uiTrackPosition (t, p) ? (long) p : -1L; };

    // No selection: every track loops over ITS OWN length, from 0, drifting apart.
    {
        auto doc = rampDocument ({ 100, 60 });
        se::Transport tr (kRate, 0.0);
        tr.play (*doc, region);
        const auto o = renderTransport (tr, *doc, 350);
        check (tr.uiLoopMode() == se::LoopMode::all && o.frames == 350
               && isSequence (o.left, walk (0, 100, 0, 1, false, 350)) && isSequence (o.right, walk (0, 60, 0, 1, false, 350)),
               "loop pin: no selection, tracks of 100 and 60 each loop over their own length from 0");
        check (tr.uiPlaySt() == 0 && tr.uiPlayEnd() == 100 && tr.uiFirstPlayTrack() == 1 && tr.uiLastPlayTrack() == 2,
               "loop pin: no selection, the published region is the whole sound, every track");
        check (at (tr, 0) == 50 && at (tr, 1) == 50, "loop pin: no selection, each playhead on its own frame (350 mod 100, 350 mod 60)");
    }
    // A caret is no selection: the whole sound loops from 0, wherever the caret is.
    {
        auto doc = rampDocument ({ 100, 60 });
        doc->selSt = doc->selEnd = 70;
        se::Transport tr (kRate, 0.0);
        tr.play (*doc, region);
        const auto o = renderTransport (tr, *doc, 120);
        check (o.left.front() == 0 && o.right.front() == 0 && isSequence (o.left, walk (0, 100, 0, 1, false, 120))
               && isSequence (o.right, walk (0, 60, 0, 1, false, 120)),
               "loop pin: a caret loops the whole sound from 0 (0.4 Play, the Keyboard)");
    }
    // A selection loops its span in its tracks only, in phase; the rest is silent.
    {
        auto doc = rampDocument ({ 100, 100 });
        doc->firstSelTrack = doc->lastSelTrack = 2;
        doc->selSt = 20; doc->selEnd = 60;
        se::Transport tr (kRate, 0.0);
        tr.play (*doc, region);
        std::vector<float> l;
        const auto r = rawRight (tr, *doc, 200, l);
        std::vector<int> ri; for (float v : r) ri.push_back (indexOf (v));
        check (silent (l, 0, l.size()) && ri == walk (20, 40, 0, 1, false, 200),
               "loop pin: a selection on track 2 loops 20..59 on track 2 alone");
        check (at (tr, 0) == -1 && at (tr, 1) == 20, "loop pin: the unselected track has no playhead");
        check (tr.uiLoopMode() == se::LoopMode::selection && tr.uiPlaySt() == 20 && tr.uiPlayEnd() == 60
               && tr.uiFirstPlayTrack() == 2 && tr.uiLastPlayTrack() == 2,
               "loop pin: the published region is the selection, in its tracks");
    }
    // A selected track shorter than the selection is absent past its end, then comes back.
    {
        auto doc = rampDocument ({ 100, 40 });
        doc->firstSelTrack = 1; doc->lastSelTrack = 2;
        doc->selSt = 20; doc->selEnd = 60;
        se::Transport tr (kRate, 0.0);
        tr.play (*doc, loop);
        std::vector<float> l;
        const auto r = rawRight (tr, *doc, 80, l);
        std::vector<int> li; for (float v : l) li.push_back (indexOf (v));
        std::vector<int> ri; for (size_t i = 0; i < 20; ++i) ri.push_back (indexOf (r[i]));
        check (li == walk (20, 40, 0, 1, false, 80) && ri == walk (20, 20, 0, 1, false, 20)
               && silent (r, 20, 40) && indexOf (r[40]) == 20 && silent (r, 60, 80),
               "loop pin: a 40-frame track in a 20..60 loop plays 20..39, is silent to 60, then again");
    }
    // Set Loop: the Play button loops the region on every track; the selection is ignored.
    {
        auto doc = rampDocument ({ 100, 100 });
        doc->loopSt = 30; doc->loopEnd = 50;
        doc->firstSelTrack = doc->lastSelTrack = 2;
        doc->selSt = 70; doc->selEnd = 90;
        se::Transport tr (kRate, 0.0);
        tr.play (*doc, region);
        const auto o = renderTransport (tr, *doc, 70);
        check (isSequence (o.left, walk (30, 20, 0, 1, false, 70)) && isSequence (o.right, walk (30, 20, 0, 1, false, 70)),
               "loop pin: Set Loop loops 30..49 on every track, over a selection elsewhere");
        check (tr.uiPlaySt() == 30 && tr.uiPlayEnd() == 50 && tr.uiFirstPlayTrack() == 1 && tr.uiLastPlayTrack() == 2,
               "loop pin: the published region is the Set Loop region, every track");
    }
    // Set Loop with a caret: the region all the same.
    {
        auto doc = rampDocument ({ 100, 60 });
        doc->loopSt = 30; doc->loopEnd = 50;
        doc->selSt = doc->selEnd = 80;
        se::Transport tr (kRate, 0.0);
        tr.play (*doc, region);
        const auto o = renderTransport (tr, *doc, 50);
        check (isSequence (o.left, walk (30, 20, 0, 1, false, 50)) && isSequence (o.right, walk (30, 20, 0, 1, false, 50)),
               "loop pin: Set Loop with a caret loops the region");
    }
    // Without useLoopRegion (the Keyboard), a set region is ignored: the selection loops.
    {
        auto doc = rampDocument ({ 100, 100 });
        doc->loopSt = 30; doc->loopEnd = 50;
        doc->firstSelTrack = 1; doc->lastSelTrack = 2;
        doc->selSt = 60; doc->selEnd = 70;
        se::Transport tr (kRate, 0.0);
        tr.play (*doc, loop);
        const auto o = renderTransport (tr, *doc, 30);
        check (isSequence (o.left, walk (60, 10, 0, 1, false, 30)) && isSequence (o.right, walk (60, 10, 0, 1, false, 30)),
               "loop pin: the Keyboard's loop ignores Set Loop and loops the selection");
    }
    // A Set Loop region that the shorter track does not reach: it is silent throughout.
    {
        auto doc = rampDocument ({ 100, 20 });
        doc->loopSt = 30; doc->loopEnd = 50;
        se::Transport tr (kRate, 0.0);
        tr.play (*doc, region);
        std::vector<float> l;
        const auto r = rawRight (tr, *doc, 60, l);
        check (silent (r, 0, r.size()) && at (tr, 1) == -1 && at (tr, 0) == 30,
               "loop pin: a track that ends before the region is silent, with no playhead");
    }
    // Block after block, the loop keeps running (it never stops).
    {
        auto doc = rampDocument ({ 100, 60 });
        se::Transport tr (kRate, 0.0);
        tr.play (*doc, region);
        for (int k = 0; k < 40; ++k) renderTransport (tr, *doc, kBlock);
        check (tr.state() == se::TransportState::playing && at (tr, 0) == (40 * kBlock) % 100 && at (tr, 1) == (40 * kBlock) % 60,
               "loop pin: a loop runs on for ever, each track on its own lap");
    }
}

// 3.2, Bug 2: loop off, a selection plays only its tracks, over its span, once.
void playOnceSelectionTests()
{
    using se::PlayDirection;
    const se::PlayOptions once;
    const auto at = [] (se::Transport& tr, int t) { size_t p = 0; return tr.uiTrackPosition (t, p) ? (long) p : -1L; };

    // The bug: a selection on track 2 used to play track 1 over the span as well.
    {
        auto doc = rampDocument ({ 100, 100 });
        doc->firstSelTrack = doc->lastSelTrack = 2;
        doc->selSt = 20; doc->selEnd = 60;
        se::Transport tr (kRate, 0.0);
        tr.play (*doc, once);
        check (at (tr, 0) == -1 && at (tr, 1) == 20, "bug 2: the unselected track has no playhead");
        check (tr.uiPlaySt() == 20 && tr.uiPlayEnd() == 60 && tr.uiFirstPlayTrack() == 2 && tr.uiLastPlayTrack() == 2,
               "bug 2: the published region is the selection, in its tracks");
        std::vector<float> l;
        const auto r = rawRight (tr, *doc, 100, l);
        std::vector<int> ri; for (size_t i = 0; i < 40; ++i) ri.push_back (indexOf (r[i]));
        check (silent (l, 0, l.size()), "bug 2: the unselected track is silent");
        check (ri == walk (20, 40, 0, 1, false, 40) && silent (r, 40, 100) && tr.state() == se::TransportState::stopped,
               "bug 2: the selected track plays 20..59, once, then the transport stops");
    }
    // Two of three tracks: the third stays out.
    {
        auto doc = rampDocument ({ 100, 100, 100 });
        doc->firstSelTrack = 1; doc->lastSelTrack = 2;
        doc->selSt = 10; doc->selEnd = 30;
        se::Transport tr (kRate, 0.0);
        tr.play (*doc, once);
        renderTransport (tr, *doc, 5);
        check (at (tr, 0) == 15 && at (tr, 1) == 15 && at (tr, 2) == -1, "bug 2: tracks 1-2 selected, track 3 out");
    }
    // Backward and ping-pong over a selection: still only its tracks, then a stop.
    for (int d = 1; d < 3; ++d)
    {
        auto doc = rampDocument ({ 100, 100 });
        doc->firstSelTrack = doc->lastSelTrack = 2;
        doc->selSt = 30; doc->selEnd = 50;
        se::Transport tr (kRate, 0.0);
        tr.setDirection ((PlayDirection) d, *doc);
        tr.play (*doc, once);
        std::vector<float> l;
        const auto r = rawRight (tr, *doc, 100, l);
        const int n = d == 1 ? 20 : 40;
        std::vector<int> ri; for (int i = 0; i < n; ++i) ri.push_back (indexOf (r[(size_t) i]));
        const auto want = d == 1 ? walk (30, 20, 19, -1, false, 20) : walk (30, 20, 0, 1, true, 40);
        check (silent (l, 0, l.size()) && ri == want && tr.state() == se::TransportState::stopped,
               d == 1 ? "bug 2: backward, the selected track alone, 49..30" : "bug 2: ping-pong, the selected track alone, one round trip");
    }
    // A direction change mid-pass when track 1 is not selected: the stop point is the
    // selected track's (the re-anchor used to take it from track 1).
    {
        auto doc = rampDocument ({ 100, 100 });
        doc->firstSelTrack = doc->lastSelTrack = 2;
        doc->selSt = 10; doc->selEnd = 100;
        se::Transport tr (kRate, 0.0);
        tr.play (*doc, once);
        renderTransport (tr, *doc, 60);                    // reads 70 next
        tr.setDirection (PlayDirection::backward, *doc);
        const bool kept = at (tr, 1) == 70 && at (tr, 0) == -1;
        const auto o = renderTransport (tr, *doc, 200);    // past the 2 ms crossfade: 70 down to 10
        check (kept && o.frames == 61 && o.right.back() == 10 && silent (o.rawL, 0, o.rawL.size()) && tr.state() == se::TransportState::stopped,
               "bug 2: turned round mid-pass, the selected track runs back to 10 and stops");
    }
    // A selected track shorter than the span: silent past its end, the pass still ends at the span's end.
    {
        auto doc = rampDocument ({ 100, 30 });
        doc->firstSelTrack = 1; doc->lastSelTrack = 2;
        doc->selSt = 20; doc->selEnd = 60;
        se::Transport tr (kRate, 0.0);
        tr.play (*doc, once);
        const auto o = renderTransport (tr, *doc, 100);
        check (o.frames == 40 && isSequence (o.left, walk (20, 40, 0, 1, false, 40)) && o.right[9] == 29,
               "bug 2: a short selected track plays to its end, the pass runs the span");
    }
    // The Recorder's play (allTracks): every track over the span, the backing.
    {
        auto doc = rampDocument ({ 100, 100 });
        doc->firstSelTrack = doc->lastSelTrack = 2;
        doc->selSt = 20; doc->selEnd = 60;
        se::PlayOptions rec; rec.allTracks = true;
        se::Transport tr (kRate, 0.0);
        tr.play (*doc, rec);
        const auto o = renderTransport (tr, *doc, 100);
        check (o.frames == 40 && isSequence (o.left, walk (20, 40, 0, 1, false, 40)) && isSequence (o.right, walk (20, 40, 0, 1, false, 40)),
               "bug 2: allTracks (a punch-in's backing) still plays every track over the span");
    }
    // No selection: every track, the whole sound, once (unchanged).
    {
        auto doc = rampDocument ({ 100, 60 });
        doc->firstSelTrack = doc->lastSelTrack = 2;        // a caret on track 2 is not a selection
        se::Transport tr (kRate, 0.0);
        tr.play (*doc, once);
        const auto o = renderTransport (tr, *doc, 200);
        check (o.frames == 100 && isSequence (o.left, walk (0, 100, 0, 1, false, 100))
               && std::vector<int> (o.right.begin(), o.right.begin() + 60) == walk (0, 60, 0, 1, false, 60)
               && tr.uiFirstPlayTrack() == 1 && tr.uiLastPlayTrack() == 2,
               "bug 2: no selection, every track plays the whole sound once");
    }
    // Through the Engine, as the Play button sends it (loop off, useLoopRegion).
    {
        se::Engine engine;
        engine.prepare (kRate, kBlock); engine.setDeclickMs (0.0);
        auto doc = rampDocument ({ 100, 100 });
        doc->firstSelTrack = doc->lastSelTrack = 1;
        doc->selSt = 40; doc->selEnd = 50;
        const uint64_t id = engine.addWindow (std::move (doc));
        se::PlayOptions o; o.useLoopRegion = true;
        engine.play (id, o);
        std::vector<float> l ((size_t) kBlock, 0.0f), r ((size_t) kBlock, 0.0f);
        engine.render (l.data(), r.data(), kBlock);
        std::vector<int> li; for (int i = 0; i < 10; ++i) li.push_back (indexOf (l[(size_t) i]));
        check (li == walk (40, 10, 0, 1, false, 10) && silent (r, 0, r.size()) && silent (l, 10, l.size()),
               "bug 2: through the engine, the Play button plays the selected track's span alone");
    }
}

// 3.3: play from the caret (decisions table): no selection, a caret — every track
// starts at the caret. Loop off: to the end of each track, once. Loop on: from the
// caret, then each track loops over its whole length; a caret past a track's end
// leaves it silent until its next lap. The playhead is drawn only where it sounds.
void playFromCaretTests()
{
    using se::PlayDirection;
    se::PlayOptions once; once.useLoopRegion = true; once.fromCaret = true;          // the Play button, loop off
    se::PlayOptions loop = once; loop.loop = true;                                   // ...loop on
    const auto at = [] (se::Transport& tr, int t) { size_t p = 0; return tr.uiTrackPosition (t, p) ? (long) p : -1L; };
    const auto caretAt = [] (se::SoundDocument& d, size_t c, int track) { d.selSt = d.selEnd = c; d.firstSelTrack = d.lastSelTrack = track; };

    // --- loop off ---
    {
        auto doc = rampDocument ({ 100, 60 });
        caretAt (*doc, 30, 1);
        se::Transport tr (kRate, 0.0);
        tr.play (*doc, once);
        check (at (tr, 0) == 30 && at (tr, 1) == 30 && tr.uiPlaySt() == 30 && tr.uiPlayEnd() == 100,
               "caret, loop off: every track starts at the caret; the region is caret..end");
        std::vector<float> l;
        const auto r = rawRight (tr, *doc, 200, l);
        std::vector<int> li, ri;
        for (int i = 0; i < 70; ++i) li.push_back (indexOf (l[(size_t) i]));
        for (int i = 0; i < 30; ++i) ri.push_back (indexOf (r[(size_t) i]));
        check (li == walk (30, 70, 0, 1, false, 70) && ri == walk (30, 30, 0, 1, false, 30)
               && silent (r, 30, r.size()) && silent (l, 70, l.size()) && tr.state() == se::TransportState::stopped,
               "caret, loop off: each track to its own end, once, then stop");
    }
    {
        auto doc = rampDocument ({ 100, 100 });
        caretAt (*doc, 40, 2);                              // a caret in track 2 only
        se::Transport tr (kRate, 0.0);
        tr.play (*doc, once);
        const auto o = renderTransport (tr, *doc, 10);
        check (o.left.front() == 40 && o.right.front() == 40 && tr.uiFirstPlayTrack() == 1 && tr.uiLastPlayTrack() == 2,
               "caret, loop off: a caret in one track plays ALL tracks");
    }
    {
        auto doc = rampDocument ({ 100, 60 });
        caretAt (*doc, 70, 1);                              // past track 2's end
        se::Transport tr (kRate, 0.0);
        tr.play (*doc, once);
        const bool noHead = at (tr, 1) == -1;
        std::vector<float> l;
        const auto r = rawRight (tr, *doc, 100, l);
        std::vector<int> li; for (int i = 0; i < 30; ++i) li.push_back (indexOf (l[(size_t) i]));
        check (noHead && silent (r, 0, r.size()) && li == walk (70, 30, 0, 1, false, 30) && tr.state() == se::TransportState::stopped,
               "caret, loop off: a caret past a track's end leaves it silent, with no playhead");
    }
    for (size_t c : { (size_t) 0, (size_t) 100 })           // at the start, or at the end
    {
        auto doc = rampDocument ({ 100 });
        caretAt (*doc, c, 1);
        se::Transport tr (kRate, 0.0);
        tr.play (*doc, once);
        const auto o = renderTransport (tr, *doc, 200);
        check (o.frames == 100 && isSequence (o.left, walk (0, 100, 0, 1, false, 100)),
               c == 0 ? "caret, loop off: a caret at 0 plays the whole sound (unchanged)"
                      : "caret, loop off: a caret at the end plays the whole sound from the start");
    }
    {
        auto doc = rampDocument ({ 100, 100 });
        caretAt (*doc, 30, 1);
        se::Transport tr (kRate, 0.0);
        tr.setDirection (PlayDirection::backward, *doc);
        tr.play (*doc, once);
        const auto o = renderTransport (tr, *doc, 100);
        check (o.frames == 30 && isSequence (o.left, walk (0, 30, 29, -1, false, 30)) && isSequence (o.right, walk (0, 30, 29, -1, false, 30)),
               "caret, loop off, backward: from the caret down to the start");
    }
    {
        auto doc = rampDocument ({ 100 });
        caretAt (*doc, 30, 1);
        se::Transport tr (kRate, 0.0);
        tr.play (*doc, once);
        tr.advance (*doc);                                  // FF while rolling
        const auto o = renderTransport (tr, *doc, 5);
        check (o.left[0] == 30 && o.left[1] == 32 && o.left[2] == 34, "caret, loop off: Advance carries on from the caret at 2x");
        tr.stop();
        tr.advance (*doc);                                  // FF from Stop: the last Play's options
        check (at (tr, 0) == 30, "caret: Advance from Stop starts at the caret too");
    }
    {
        auto doc = rampDocument ({ 100, 100 });
        caretAt (*doc, 30, 1);
        se::PlayOptions plain;                              // the Recorder's / an old caller's
        se::Transport tr (kRate, 0.0);
        tr.play (*doc, plain);
        check (at (tr, 0) == 0 && at (tr, 1) == 0, "caret: without fromCaret, play-once starts at 0 (the Recorder)");
    }
    {
        auto doc = rampDocument ({ 100, 100 });
        doc->firstSelTrack = doc->lastSelTrack = 2;
        doc->selSt = 20; doc->selEnd = 40;
        se::Transport tr (kRate, 0.0);
        tr.play (*doc, once);
        check (at (tr, 0) == -1 && at (tr, 1) == 20, "caret: a selection is not a caret (Bug 2 rule)");
    }

    // --- loop on ---
    {
        auto doc = rampDocument ({ 100, 60 });
        caretAt (*doc, 30, 1);
        se::Transport tr (kRate, 0.0);
        tr.play (*doc, loop);
        check (tr.uiLoopMode() == se::LoopMode::all && at (tr, 0) == 30 && at (tr, 1) == 30, "caret, loop on: every track starts at the caret");
        const auto o = renderTransport (tr, *doc, 300);
        check (isSequence (o.left, walk (0, 100, 30, 1, false, 300)) && isSequence (o.right, walk (0, 60, 30, 1, false, 300)),
               "caret, loop on: a partial first lap, then each track loops its whole length");
    }
    {
        auto doc = rampDocument ({ 100, 60 });
        caretAt (*doc, 70, 1);                              // past track 2's end: its next lap is at 120
        se::Transport tr (kRate, 0.0);
        tr.play (*doc, loop);
        const bool noHead = at (tr, 1) == -1;
        std::vector<float> l;
        const auto r = rawRight (tr, *doc, 200, l);
        std::vector<int> li, ri;
        for (float v : l) li.push_back (indexOf (v));
        for (size_t i = 50; i < r.size(); ++i) ri.push_back (indexOf (r[i]));
        check (noHead && silent (r, 0, 50) && ri == walk (0, 60, 0, 1, false, 150) && li == walk (0, 100, 70, 1, false, 200),
               "caret, loop on: a caret past a track's end leaves it silent until its next lap, then it loops");
        check (at (tr, 1) == (270 % 60), "caret, loop on: its playhead appears with its sound");
    }
    {
        auto doc = rampDocument ({ 100, 40 });
        caretAt (*doc, 80, 1);                              // exactly on track 2's lap boundary
        se::Transport tr (kRate, 0.0);
        tr.play (*doc, loop);
        const auto o = renderTransport (tr, *doc, 50);
        check (isSequence (o.right, walk (0, 40, 0, 1, false, 50)), "caret, loop on: a caret on a lap boundary starts that track at once, from 0");
    }
    {
        auto doc = rampDocument ({ 100, 60 });
        caretAt (*doc, 100, 1);                             // at the end: the start, as before
        se::Transport tr (kRate, 0.0);
        tr.play (*doc, loop);
        const auto o = renderTransport (tr, *doc, 150);
        check (isSequence (o.left, walk (0, 100, 0, 1, false, 150)) && isSequence (o.right, walk (0, 60, 0, 1, false, 150)),
               "caret, loop on: a caret at the end loops from the start");
    }
    {
        auto doc = rampDocument ({ 100, 20 });
        caretAt (*doc, 30, 1);
        se::Transport tr (kRate, 0.0);
        tr.setDirection (PlayDirection::backward, *doc);
        tr.play (*doc, loop);
        std::vector<float> l;
        const auto r = rawRight (tr, *doc, 150, l);
        std::vector<int> li, ri;
        for (float v : l) li.push_back (indexOf (v));
        for (size_t i = 10; i < r.size(); ++i) ri.push_back (indexOf (r[i]));
        check (li == walk (0, 100, 29, -1, false, 150) && silent (r, 0, 10) && ri == walk (0, 20, 19, -1, false, 140),
               "caret, loop on, backward: down from the caret; a shorter track comes in when the read point reaches it");
    }
    {
        auto doc = rampDocument ({ 100 });
        caretAt (*doc, 30, 1);
        se::Transport tr (kRate, 0.0);
        tr.setDirection (PlayDirection::pingPong, *doc);
        tr.play (*doc, loop);
        const auto o = renderTransport (tr, *doc, 250);
        check (isSequence (o.left, walk (0, 100, 30, 1, true, 250)), "caret, loop on, ping-pong: up from the caret, then bounces");
    }
    {
        auto doc = rampDocument ({ 100, 100 });
        doc->loopSt = 50; doc->loopEnd = 60;
        caretAt (*doc, 30, 1);
        se::Transport tr (kRate, 0.0);
        tr.play (*doc, loop);
        const auto o = renderTransport (tr, *doc, 30);
        check (isSequence (o.left, walk (50, 10, 0, 1, false, 30)), "caret, loop on: a Set Loop region wins over the caret");
    }
    {
        auto doc = rampDocument ({ 100, 100 });
        doc->firstSelTrack = doc->lastSelTrack = 2;
        doc->selSt = 20; doc->selEnd = 40;
        se::Transport tr (kRate, 0.0);
        tr.play (*doc, loop);
        check (tr.uiLoopMode() == se::LoopMode::selection && at (tr, 0) == -1 && at (tr, 1) == 20,
               "caret, loop on: a selection loops as before (3.1)");
    }
    {
        // Moving the caret while it loops re-aims nothing (a running loop is never re-aimed by a caret).
        auto doc = rampDocument ({ 100 });
        caretAt (*doc, 30, 1);
        se::Transport tr (kRate, 0.0);
        tr.play (*doc, loop);
        renderTransport (tr, *doc, 10);
        caretAt (*doc, 80, 1);
        renderTransport (tr, *doc, 1);
        check (at (tr, 0) == 41, "caret, loop on: moving the caret mid-loop does not move the playhead");
    }
    {
        // A direction change during a track's wait ends the wait: it joins on the others' frame.
        auto doc = rampDocument ({ 100, 60 });
        caretAt (*doc, 70, 1);
        se::Transport tr (kRate, 0.0);
        tr.play (*doc, loop);
        renderTransport (tr, *doc, 10);                     // track 1 reads 80 next; track 2 waiting
        tr.setDirection (PlayDirection::backward, *doc);
        check (at (tr, 0) == 80 && at (tr, 1) == 20, "caret, loop on: turned round mid-wait, the waiting track joins (80 mod 60)");
    }
    // Through the Engine, as the Play button sends it.
    {
        se::Engine engine;
        engine.prepare (kRate, kBlock); engine.setDeclickMs (0.0);
        auto doc = rampDocument ({ 100, 60 });
        caretAt (*doc, 30, 1);
        const uint64_t id = engine.addWindow (std::move (doc));
        engine.play (id, once);
        std::vector<float> l ((size_t) kBlock, 0.0f), r ((size_t) kBlock, 0.0f);
        engine.render (l.data(), r.data(), kBlock);
        check (indexOf (l[0]) == 30 && indexOf (r[0]) == 30 && indexOf (l[63]) == 93 && silent (r, 30, (size_t) kBlock),
               "caret: through the engine, the Play button starts every track at the caret");
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
        se::Transport tr (kRate, 0.0);
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
        se::Transport tr (kRate, 0.0);
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
        se::Transport tr (kRate, 0.0);
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
        se::Transport tr (kRate, 0.0);
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
        se::Transport tr (kRate, 0.0);
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
        se::Transport tr (kRate, 0.0);
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
        se::Transport tr (kRate, 0.0);
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
        se::Transport tr (kRate, 0.0);
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
        se::Transport tr (kRate, 0.0);
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
        se::Transport tr (kRate, 0.0);
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
    // Case 10: while a take runs (takeOpen) the aim is frozen; it lands after.
    {
        auto doc = rampDocument ({ 100, 100 });
        selectTracks (*doc, 2, 2, 20, 60);
        se::Transport tr (kRate, 0.0);
        tr.play (*doc, loop);
        tr.excludeMask = 2;                                // track 2 is being written
        tr.takeOpen = true;
        renderTransport (tr, *doc, 25);
        doc->selSt = doc->selEnd = 0;
        renderTransport (tr, *doc, 1);
        check (tr.uiLoopMode() == se::LoopMode::selection && at (tr, 1) == 46, "recording: no re-aim mid-take");
        tr.takeOpen = false;                               // the take ends (a disarm keeps the track silent)
        renderTransport (tr, *doc, 1);
        check (tr.uiLoopMode() == se::LoopMode::all && at (tr, 0) == 47 && at (tr, 1) == 47,
               "recording: the deselection lands when the take ends, even with the track still silent (a disarm)");
    }
    // Case 11: no click — a sine through the deselection.
    {
        auto doc = sineDocument (400);
        doc->tracks.push_back (doc->tracks[0]);
        doc->tracks[1].outLeft = true;
        selectTracks (*doc, 2, 2, 100, 380);   // the old loop does not wrap under the crossfade
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
        se::Transport tr (kRate, 0.0);
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
    engine.prepare (kRate, kBlock); engine.setDeclickMs (0.0);
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
        se::Engine e; e.prepare (kRate, kBlock); e.setDeclickMs (0.0);
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
        size_t from = 99, to = 0;
        check (rec.takeWrittenFrames (from, to) && from == 0 && to == (size_t) (10 * kBlock),
               "record: the written frames are reported, [0, end)");
        check (! rec.takeWrittenFrames (from, to), "record: and only once");
    }

    // Stereo source: two new tracks after the existing ones, routed to L and R; the
    // existing track plays on, the new ones stay silent while they are written.
    {
        se::Engine e; e.prepare (kRate, kBlock); e.setDeclickMs (0.0);
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
        se::Engine e; e.prepare (kRate, kBlock); e.setDeclickMs (0.0);
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
        se::Engine e; e.prepare (kRate, kBlock); e.setDeclickMs (0.0);
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
        se::Engine e; e.prepare (kRate, kBlock); e.setDeclickMs (0.0);
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
        se::Engine e; e.prepare (kRate, kBlock); e.setDeclickMs (0.0);
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
        size_t from = 0, to = 0;
        check (rec.takeWrittenFrames (from, to) && from == 100 && to == 300,
               "punch: the written frames are the selection's");
        check (e.windowById (w)->transport->uiState() == se::TransportState::stopped, "punch: the transport is stopped");
    }

    // Two selected tracks: left on the upper, right on the lower, whatever their speaker toggles say.
    {
        se::Engine e; e.prepare (kRate, kBlock); e.setDeclickMs (0.0);
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
        se::Engine e; e.prepare (kRate, kBlock); e.setDeclickMs (0.0);
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
        se::Engine e; e.prepare (kRate, kBlock); e.setDeclickMs (0.0);
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
        se::Engine e; e.prepare (kRate, kBlock); e.setDeclickMs (0.0);
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
        se::Engine e; e.prepare (kRate, kBlock); e.setDeclickMs (0.0);
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
        se::Engine e; e.prepare (kRate, kBlock); e.setDeclickMs (0.0);
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
        se::Engine e; e.prepare (kRate, kBlock); e.setDeclickMs (0.0);
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
// --- 0.5.0 step 5.2: a take that joins a rolling transport, a disarm that ends the
// take and leaves playback alone, and a take that follows the Set Loop region. ------
void takeJoinTests()
{
    const float level = 20.0f / 128.0f;   // dcDocument (20): what the existing track sounds at

    // Join a transport that is playing: no restart, the new track starts with
    // silence up to the frame it joined at.
    {
        se::Engine e; e.prepare (kRate, kBlock); e.setDeclickMs (0.0);
        const uint64_t w = e.addWindow (dcDocument (20, "w"));   // 8-bit, 512 frames
        auto* win = e.windowById (w);
        e.setDirection (w, se::PlayDirection::backward);
        e.play (w, {});
        se::Recorder rec (e);
        std::vector<float> ol, orr;
        for (int i = 0; i < 3; ++i) renderWithInput (e, { 0.25f }, ol, orr);   // backward: 512 -> 320
        const size_t before = win->transport->uiPlayhead();
        se::RecordSetup s; s.source.kind = se::AudioSourceKind::external; s.source.stereo = false; s.incomingStereo = false;
        check (rec.start (w, s, true), "join: a take starts on a playing transport");
        renderWithInput (e, { 0.25f }, ol, orr); rec.service();
        check (win->transport->uiPlayhead() == before + (size_t) kBlock, "join: the transport is not restarted");
        check (e.direction (w) == se::PlayDirection::forward && win->transport->direction() == se::PlayDirection::forward,
               "join: the transport turns Forward where it is");
        checkClose (ol[kBlock - 1], level, 1.0e-4f, "join: the existing track keeps sounding, the new one is silent");
        const size_t joinedAt = e.captureStartFrame();
        check (joinedAt == 512 - 1 - (size_t) (3 * kBlock), "join: the frame it joined at is the one the track was reading");
        pump (e, rec, 2, { 0.25f });
        rec.stop();
        settleRec (e, rec, { 0.25f });
        const auto* d = e.editDoc (w);
        check (d->numTracks() == 2, "join: one new track");
        check (at (*d, 1, joinedAt - 1) == 0 && at (*d, 1, joinedAt) == 32 && at (*d, 1, joinedAt + 3 * kBlock - 1) == 32,
               "join: silence up to the join, the take from there");
        check (d->tracks[1].data.size() == joinedAt + (size_t) (3 * kBlock), "join: the take is as long as what was captured");
    }

    // Join a paused transport: it resumes from where it was held.
    {
        se::Engine e; e.prepare (kRate, kBlock); e.setDeclickMs (0.0);
        const uint64_t w = e.addWindow (dcDocument (20, "w"));
        auto* win = e.windowById (w);
        e.play (w, {});
        renderBlocks (e, 2);
        e.pause (w);
        renderBlocks (e, 3);
        const size_t held = win->transport->uiPlayhead();
        se::Recorder rec (e);
        se::RecordSetup s; s.source.stereo = false; s.incomingStereo = false;
        rec.start (w, s, true);
        std::vector<float> ol, orr;
        renderWithInput (e, { 0.25f }, ol, orr); rec.service();
        check (win->transport->uiState() == se::TransportState::playing && win->transport->uiPlayhead() == held + (size_t) kBlock,
               "join paused: resumes from where it was held");
        rec.stop(); settleRec (e, rec, { 0.25f });
        check (at (*e.editDoc (w), 1, (size_t) (2 * kBlock)) == 32 && at (*e.editDoc (w), 1, (size_t) (2 * kBlock) - 1) == 0,
               "join paused: the take begins at the held frame");
    }

    // Disarm mid-take (Recorder::end): the capture ends, the transport plays on
    // untouched, the new track stays silent until the next Play or Stop.
    {
        se::Engine e; e.prepare (kRate, kBlock); e.setDeclickMs (0.0);
        const uint64_t w = e.addWindow (dcDocument (20, "w"));
        auto* win = e.windowById (w);
        se::PlayOptions loopAll; loopAll.loop = true;
        e.play (w, loopAll);
        renderBlocks (e, 2);
        se::Recorder rec (e);
        int finished = 0;
        rec.onFinished = [&] (uint64_t) { ++finished; };
        se::RecordSetup s; s.source.stereo = false; s.incomingStereo = false; s.loop = true;
        rec.start (w, s, true);
        pump (e, rec, 3, { 0.5f });
        const size_t before = win->transport->uiPlayhead();
        rec.end();
        settleRec (e, rec, { 0.5f });
        check (finished == 1 && rec.state() == se::Recorder::State::idle && ! e.captureRunning(), "end: the take is over, capture stopped");
        check (win->transport->uiState() == se::TransportState::playing, "end: the transport still plays");
        std::vector<float> ol, orr;
        const size_t now = win->transport->uiPlayhead();
        renderWithInput (e, { 0.5f }, ol, orr);
        check (now > before && win->transport->uiPlayhead() == now + (size_t) kBlock, "end: the playhead carries on, no reset");
        checkClose (ol[0], level, 0.01f, "end: no step in the output (the take fades in from silence)");
        check (e.editDoc (w)->numTracks() == 2 && at (*e.editDoc (w), 1, 3 * kBlock) == 64, "end: the take is kept");
        check (win->transport->excludeMask == 0, "end: once published, the take is released (no Stop needed)");
        check (! win->transport->takeOpen, "end: a loop may re-aim again (the take no longer freezes it)");
        {
            float peak = 0.0f;
            for (int b = 0; b < 10; ++b)
            {
                renderWithInput (e, { 0.5f }, ol, orr);
                for (int i = 0; i < kBlock; ++i) peak = std::max (peak, ol[(size_t) i]);
            }
            checkClose (peak, level + 0.5f, 1.0e-3f, "end: the take is heard on top of the old track");
        }
        {
            // The release itself: a released track that is not silent where it is
            // read ramps in over ~5 ms instead of stepping in.
            auto doc = dcDocument (20, "r");
            doc->addTrack();
            doc->tracks[1].data.assign (512, 0);
            for (size_t i = 0; i < 512; ++i) se::setSample (doc->tracks[1].data.data(), i, 64, 8);
            se::Transport tr (kRate);
            se::PlayOptions lp; lp.loop = true;
            tr.play (*doc, lp);
            tr.excludeMask = 2;
            std::vector<float> l ((size_t) kBlock), r ((size_t) kBlock);
            for (int b = 0; b < 3; ++b) tr.render (*doc, l.data(), r.data(), kBlock);   // past the start's fade-in
            tr.releaseExcluded();
            std::vector<float> ramp;
            for (int b = 0; b < 3; ++b)
            {
                tr.render (*doc, l.data(), r.data(), kBlock);
                ramp.insert (ramp.end(), l.begin(), l.end());
            }
            float maxStep = 0.0f;
            for (size_t i = 1; i < ramp.size(); ++i) maxStep = std::max (maxStep, std::fabs (ramp[i] - ramp[i - 1]));
            check (tr.excludeMask == 0 && ramp.front() < level + 0.01f && maxStep < 0.01f,
                   "release: the track fades in, no step");
            checkClose (ramp.back(), level + 0.5f, 1.0e-4f, "release: after ~5 ms it is at full level");
        }

        // Ending a paused take: the transport stays paused.
        rec.start (w, s, true);
        pump (e, rec, 2, { 0.5f });
        rec.pause(); settleRec (e, rec, { 0.5f }, se::Recorder::State::paused);
        rec.end();
        renderBlocks (e, 1);
        check (rec.state() == se::Recorder::State::idle && win->transport->uiState() == se::TransportState::paused,
               "end a paused take: idle, the transport still paused");
    }

    // Join a play-once pass over a selection: a punch-in part-way, out at the end.
    {
        se::Engine e; e.prepare (kRate, kBlock); e.setDeclickMs (0.0);
        auto doc = blankSound (1, kRate);
        doc->tracks[0].data.assign (2000, 0);
        doc->selSt = 200; doc->selEnd = 600; doc->firstSelTrack = doc->lastSelTrack = 1;   // frames 100..299
        const uint64_t w = e.addWindow (std::move (doc));
        e.play (w, {});
        renderBlocks (e, 1);   // reading frame 164
        se::Recorder rec (e);
        int finished = 0;
        rec.onFinished = [&] (uint64_t) { ++finished; };
        se::RecordSetup s; s.source.stereo = false; s.incomingStereo = false;
        rec.start (w, s, true);
        settleRec (e, rec, { 0.25f });
        const auto* d = e.editDoc (w);
        check (finished == 1, "join selection: the punch-out still ends the take at the selection's end");
        check (at (*d, 0, 163) == 0 && at (*d, 0, 164) == 8192 && at (*d, 0, 299) == 8192 && at (*d, 0, 300) == 0,
               "join selection: punched in where it joined, out at the end, nothing after");
    }

    // Loop on and a Set Loop region: the take stays inside it, round and round.
    {
        se::Engine e; e.prepare (kRate, kBlock); e.setDeclickMs (0.0);
        auto doc = blankSound (1, kRate);
        doc->tracks[0].data.assign (2000, 0);
        doc->loopSt = 200; doc->loopEnd = 600;   // frames 100..299; no selection
        const uint64_t w = e.addWindow (std::move (doc));
        auto* win = e.windowById (w);
        se::Recorder rec (e);
        se::RecordSetup s; s.source.stereo = false; s.incomingStereo = false; s.loop = true;
        check (rec.start (w, s), "region: a take starts");
        pump (e, rec, 3, { 0.25f });    // 192 frames: 100..291
        pump (e, rec, 1, { 0.5f });     // 64 more: 292..299 then 100..155
        check (win->transport->uiPlaySt() == 200 && win->transport->uiPlayEnd() == 600, "region: the transport loops the region");
        rec.stop(); settleRec (e, rec, { 0.5f });
        const auto* d = e.editDoc (w);
        check (d->numTracks() == 2 && d->tracks[1].data.size() == 600, "region: a new track, padded to the region's end and no further");
        check (at (*d, 1, 99) == 0 && at (*d, 1, 100) == 16384 && at (*d, 1, 156) == 8192 && at (*d, 1, 291) == 8192
               && at (*d, 1, 292) == 16384 && at (*d, 1, 299) == 16384,
               "region: silence before it, and the take wraps inside it");

        // Loop off: the region is ignored, as play-once ignores it.
        s.loop = false;
        rec.start (w, s);
        pump (e, rec, 2, { 0.25f });
        rec.stop(); settleRec (e, rec, { 0.25f });
        check (at (*e.editDoc (w), 2, 0) == 8192 && e.editDoc (w)->tracks[2].data.size() == (size_t) (2 * kBlock) * 2,
               "region, Loop off: a new track from 0, as before");
    }

    // A region and a selection over one track: the selected track, inside the region.
    {
        se::Engine e; e.prepare (kRate, kBlock); e.setDeclickMs (0.0);
        auto doc = blankSound (2, kRate);
        for (auto& t : doc->tracks) t.data.assign (400, 0);   // 200 frames: shorter than the region's end
        doc->loopSt = 200; doc->loopEnd = 600;
        doc->selSt = 0; doc->selEnd = 100; doc->firstSelTrack = doc->lastSelTrack = 2;
        const uint64_t w = e.addWindow (std::move (doc));
        se::Recorder rec (e);
        se::RecordSetup s; s.source.stereo = false; s.incomingStereo = false; s.loop = true;
        rec.start (w, s);
        pump (e, rec, 2, { 0.25f });
        rec.stop(); settleRec (e, rec, { 0.25f });
        const auto* d = e.editDoc (w);
        check (d->tracks[1].data.size() == 600 && at (*d, 1, 99) == 0 && at (*d, 1, 100) == 8192 && at (*d, 1, 227) == 8192
               && at (*d, 1, 228) == 0, "region + selection: written into the selected track, inside the region");
        check (d->tracks[0].data.size() == 400, "region + selection: the other track untouched");
    }
}

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
        e.prepare (kRate, kBlock); e.setDeclickMs (0.0);
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
        e.prepare (kRate, kBlock); e.setDeclickMs (0.0);
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
        e.prepare (kRate, kBlock); e.setDeclickMs (0.0);
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

// --- commitMeta (perf audit P3) -------------------------------------------------
// The selection, the loop region and the speakers are published without cloning
// the samples. It must be indistinguishable, to the ear, from publishing the whole
// document: the same script rendered both ways gives the same bits.
void metaTests()
{
    const auto makeDoc = []
    {
        se::DocumentInit init;
        init.name = "meta"; init.numTracks = 2; init.bps = 8; init.rateHz = kRate; init.sizeBytes = 2000;
        auto doc = std::make_unique<se::SoundDocument> (init);
        for (int t = 0; t < 2; ++t)
            for (size_t i = 0; i < 2000; ++i)
                se::setSample (doc->tracks[(size_t) t].data.data(), i, (int) ((i * 3 + (size_t) t * 50) % 200) - 100, 8);
        return doc;
    };

    std::vector<float> out[2];
    for (int mode = 0; mode < 2; ++mode)
    {
        se::Engine e; e.prepare (kRate, kBlock); e.setDeclickMs (0.0);
        const uint64_t id = e.addWindow (makeDoc());
        const auto publish = [&] { if (mode == 0) e.commitEdit (id); else e.commitMeta (id); };
        const auto run = [&] (int blocks)
        {
            std::vector<float> l ((size_t) kBlock), r ((size_t) kBlock);
            for (int b = 0; b < blocks; ++b)
            {
                e.render (l.data(), r.data(), kBlock);
                out[mode].insert (out[mode].end(), l.begin(), l.end());
                out[mode].insert (out[mode].end(), r.begin(), r.end());
            }
            e.collectRetired();
        };
        se::PlayOptions o; o.loop = true;
        e.play (id, o);
        run (5);
        auto* d = e.editDoc (id);
        d->firstSelTrack = 1; d->lastSelTrack = 2;
        se::setSelection (*d, 300, 900);              publish(); run (10);   // a loop re-aims at it
        d->tracks[1].outLeft = false;                 publish(); run (5);    // a speaker off
        d->firstSelTrack = d->lastSelTrack = 2;       publish(); run (5);    // one track
        d->loopSt = 100; d->loopEnd = 500;            publish(); run (5);    // Set Loop
        se::setSelection (*d, 0, 0);                  publish(); run (10);   // deselected
        e.play (id, [] { se::PlayOptions p; p.loop = true; p.useLoopRegion = true; return p; }());
        run (10);                                                            // the region plays
    }
    bool same = out[0].size() == out[1].size() && ! out[0].empty();
    for (size_t i = 0; same && i < out[0].size(); ++i)
        same = std::memcmp (&out[0][i], &out[1][i], sizeof (float)) == 0;
    check (same, "commitMeta: the same script renders bit-identical to commitEdit");

    // A document whose tracks changed without a commitEdit is left alone (it cannot
    // happen in the app; the guard keeps the audio thread off a mismatched layout).
    {
        se::Engine e; e.prepare (kRate, kBlock); e.setDeclickMs (0.0);
        const uint64_t id = e.addWindow (makeDoc());
        se::PlayOptions o; o.loop = true;
        e.play (id, o);
        renderBlocks (e, 2);
        auto* d = e.editDoc (id);
        d->firstSelTrack = 1; d->lastSelTrack = 1;
        se::setSelection (*d, 200, 400);
        check (e.commitMeta (id), "commitMeta: posted");
        renderBlocks (e, 2);
        check (e.windowById (id)->transport->uiPlaySt() == 200, "commitMeta: the loop follows the new selection");
        d->addTrack();
        se::setSelection (*d, 600, 800);
        e.commitMeta (id);
        renderBlocks (e, 2);
        check (e.windowById (id)->transport->uiPlaySt() == 200, "commitMeta: ignored when the track count differs");
    }
}

// --- 0.5.0 step 4.1: Selection ▸ Unset Loop -----------------------------------
// The region is cleared and nothing else moves; a loop that is playing the region
// follows through Transport::render's live re-aim, as it does for Set Loop.
void unsetLoopTests()
{
    {
        se::DocumentInit init; init.numTracks = 2; init.sizeBytes = 8;
        se::SoundDocument d (init);
        check (! se::hasLoop (d) && ! se::unsetLoop (d), "unset loop: nothing set, nothing done (the menu row greys)");
        d.selSt = d.selEnd = 3;
        se::setLoop (d);
        check (! se::hasLoop (d), "unset loop: a caret's Set Loop sets no region");
        d.selSt = 2; d.selEnd = 6; d.firstSelTrack = d.lastSelTrack = 2;
        se::setLoop (d);
        check (se::hasLoop (d), "unset loop: Set Loop on a selection sets one");
        d.selSt = 1; d.selEnd = 4;
        check (se::unsetLoop (d) && ! se::hasLoop (d) && d.loopSt == 0 && d.loopEnd == 0,
               "unset loop: the region is cleared");
        check (d.selSt == 1 && d.selEnd == 4 && d.firstSelTrack == 2 && d.lastSelTrack == 2,
               "unset loop: the selection is left alone");
        check (! se::unsetLoop (d), "unset loop: twice does nothing the second time");
        se::selectLoop (d);
        check (d.selSt == 0 && d.selEnd == 0, "unset loop: Select Set Loop then gives a caret at 0 (it greys)");
    }

    se::PlayOptions region; region.loop = true; region.useLoopRegion = true;   // the Play button, loop on
    const auto at = [] (se::Transport& tr, int t) { size_t p = 0; return tr.uiTrackPosition (t, p) ? (long) p : -1L; };

    // Playing the region, a selection elsewhere: Unset Loop hands the loop to the selection.
    {
        auto doc = rampDocument ({ 100, 100 });
        doc->selSt = 30; doc->selEnd = 50; doc->firstSelTrack = 1; doc->lastSelTrack = 2;
        se::setLoop (*doc);
        doc->selSt = 60; doc->selEnd = 80; doc->firstSelTrack = doc->lastSelTrack = 2;
        se::Transport tr (kRate, 0.0);
        tr.play (*doc, region);
        renderTransport (tr, *doc, 25);
        check (tr.uiPlaySt() == 30 && tr.uiPlayEnd() == 50 && at (tr, 0) >= 30 && at (tr, 0) < 50,
               "unset loop, playing: the region loops before");
        se::unsetLoop (*doc);
        std::vector<float> l;
        const auto r = rawRight (tr, *doc, 200, l);
        check (tr.state() == se::TransportState::playing && tr.uiPlaySt() == 60 && tr.uiPlayEnd() == 80
               && tr.uiFirstPlayTrack() == 2 && tr.uiLastPlayTrack() == 2,
               "unset loop, playing: the loop re-aims at the selection, still playing");
        std::vector<int> ri;
        for (size_t i = 100; i < 200; ++i) ri.push_back (indexOf (r[i]));
        bool inSel = true;
        for (int v : ri) inSel = inSel && v >= 60 && v < 80;
        check (inSel && silent (l, 100, 200) && at (tr, 0) == -1, "unset loop, playing: only the selected track sounds, over its span");
    }
    // Playing the region, a caret: Unset Loop hands it to the whole sound, each track its own length.
    {
        auto doc = rampDocument ({ 100, 60 });
        doc->selSt = 30; doc->selEnd = 50;
        se::setLoop (*doc);
        doc->selSt = doc->selEnd = 10;
        se::Transport tr (kRate, 0.0);
        tr.play (*doc, region);
        renderTransport (tr, *doc, 25);
        se::unsetLoop (*doc);
        const auto o = renderTransport (tr, *doc, 300);
        check (tr.uiLoopMode() == se::LoopMode::all && tr.uiPlaySt() == 0 && tr.uiPlayEnd() == 100,
               "unset loop, caret: the whole sound loops");
        // After the crossfade each track walks its own length: the right one wraps at 60.
        bool wrapsAt60 = false, leftPast50 = false;
        for (size_t i = 1; i < o.right.size(); ++i) wrapsAt60 = wrapsAt60 || (o.right[i - 1] == 59 && o.right[i] == 0);
        for (int v : o.left) leftPast50 = leftPast50 || v >= 60;
        check (wrapsAt60 && leftPast50, "unset loop, caret: each track loops its own length again");
    }
    // Through the Engine, as the menu commits it (commitMeta).
    {
        se::Engine e; e.prepare (kRate, kBlock); e.setDeclickMs (0.0);
        auto d0 = rampDocument ({ 100 });
        d0->selSt = 20; d0->selEnd = 40;
        se::setLoop (*d0);
        d0->selSt = 60; d0->selEnd = 90;
        const uint64_t id = e.addWindow (std::move (d0));
        e.play (id, region);
        renderBlocks (e, 2);
        auto* tr = e.windowById (id)->transport.get();
        check (tr->uiPlaySt() == 20 && tr->uiPlayEnd() == 40, "unset loop, engine: the region plays");
        auto* d = e.editDoc (id);
        check (se::unsetLoop (*d) && e.commitMeta (id), "unset loop, engine: committed");
        renderBlocks (e, 2);
        check (tr->uiPlaySt() == 60 && tr->uiPlayEnd() == 90 && tr->state() == se::TransportState::playing,
               "unset loop, engine: the playing loop follows to the selection");
        e.collectRetired();
    }
    // Loop off: play-once never reads the region (3.2), so Unset Loop mid-pass changes nothing.
    {
        auto doc = rampDocument ({ 100 });
        doc->selSt = 30; doc->selEnd = 50;
        se::setLoop (*doc);
        doc->selSt = doc->selEnd = 0;
        se::PlayOptions once; once.useLoopRegion = true; once.fromCaret = true;
        se::Transport tr (kRate, 0.0);
        tr.play (*doc, once);
        const auto a = renderTransport (tr, *doc, 5);
        se::unsetLoop (*doc);
        const auto b = renderTransport (tr, *doc, 200);
        std::vector<int> all = a.left; all.insert (all.end(), b.left.begin(), b.left.end());
        check (all == walk (0, 100, 0, 1, false, 100) && tr.state() == se::TransportState::stopped,
               "unset loop, loop off: the pass in flight plays on to the end, untouched");
    }
}

// --- 0.5.0 step 5.1: the transport buttons' model, one block per row of the spec
// table ("Transport buttons"), plus the initial state from its note. ---------------
bool cuesAre (const se::TransportCues& c, std::initializer_list<se::TransportCue> want)
{
    if (c.count != static_cast<int> (want.size())) return false;
    int i = 0;
    for (auto w : want) if (c.list[static_cast<size_t> (i++)] != w) return false;
    return true;
}

bool lights (const se::TransportButtons& b, se::RecordLight rec, bool stop, bool play, bool pause, bool ff)
{
    return b.record() == rec && b.stopOn() == stop && b.playOn() == play && b.pauseOn() == pause && b.ffOn() == ff;
}

// 0.5.0 step 6: the selection-info icons (start, length, end), pure.
void selectionInfoTests()
{
    const auto sel = [] (se::SoundDocument& d, size_t st, size_t en, int a, int b)
    { d.selSt = st; d.selEnd = en; d.firstSelTrack = a; d.lastSelTrack = b; };
    const auto is = [] (const se::SoundDocument& d, size_t st, size_t en, int a, int b)
    { return d.selSt == st && d.selEnd == en && d.firstSelTrack == a && d.lastSelTrack == b; };

    // Uneven tracks: 100, 60, 0 (empty), 80.
    auto doc = rampDocument ({ 100, 60, 0, 80 });
    auto& d = *doc;

    // --- start
    sel (d, 20, 50, 2, 4);
    check (se::selectionToStart (d) && is (d, 0, 50, 2, 4), "selection info, start: the start goes to 0, end and tracks kept");
    check (! se::selectionToStart (d), "selection info, start: already at 0, nothing changes");
    sel (d, 40, 40, 2, 3);
    check (se::selectionToStart (d) && is (d, 0, 0, 2, 3), "selection info, start: a caret goes to 0 in its tracks");
    check (! se::selectionToStart (d), "selection info, start: a caret at 0 stays");
    sel (d, 70, 90, 2, 2);   // past track 2's end (the longest track allows it)
    check (se::selectionToStart (d) && is (d, 0, 90, 2, 2), "selection info, start: a selection past its track's end keeps its end");
    sel (d, 10, 30, 3, 3);   // the empty track
    check (se::selectionToStart (d) && is (d, 0, 30, 3, 3), "selection info, start: on an empty track too (it is a time range)");

    // --- whole tracks
    sel (d, 20, 50, 2, 4);
    check (se::selectionToWholeTracks (d) && is (d, 0, 80, 2, 4),
           "selection info, length: 0 to the longest selected track (80 of 60/0/80), not the sound's 100");
    check (! se::selectionToWholeTracks (d), "selection info, length: already whole, nothing changes");
    sel (d, 30, 30, 1, 2);
    check (se::selectionToWholeTracks (d) && is (d, 0, 100, 1, 2), "selection info, length: a caret selects its tracks whole");
    sel (d, 70, 90, 2, 2);
    check (se::selectionToWholeTracks (d) && is (d, 0, 60, 2, 2), "selection info, length: a selection past its track's end comes back to it");
    sel (d, 10, 30, 3, 3);
    check (! se::selectionToWholeTracks (d) && is (d, 10, 30, 3, 3), "selection info, length: an empty track has nothing to select");
    sel (d, 0, 0, 3, 3);
    check (! se::selectionToWholeTracks (d) && is (d, 0, 0, 3, 3), "selection info, length: a caret in an empty track stays");
    sel (d, 5, 10, 2, 3);
    check (se::selectionToWholeTracks (d) && is (d, 0, 60, 2, 3), "selection info, length: an empty track among others is ignored");

    // --- end
    sel (d, 20, 50, 1, 2);
    check (se::selectionToEnd (d) && is (d, 20, 100, 1, 2), "selection info, end: the end goes to the longest selected track");
    check (! se::selectionToEnd (d), "selection info, end: already there, nothing changes");
    sel (d, 20, 90, 2, 3);
    check (se::selectionToEnd (d) && is (d, 20, 60, 2, 3), "selection info, end: a selection running past its audio is shortened to it");
    sel (d, 70, 90, 2, 2);
    check (! se::selectionToEnd (d) && is (d, 70, 90, 2, 2), "selection info, end: a selection wholly past its audio is left alone");
    sel (d, 60, 90, 2, 2);
    check (! se::selectionToEnd (d) && is (d, 60, 90, 2, 2), "selection info, end: one starting at its audio's end is left alone");
    sel (d, 10, 10, 2, 4);
    check (se::selectionToEnd (d) && is (d, 80, 80, 2, 4), "selection info, end: a caret goes to the end of its tracks' audio");
    check (! se::selectionToEnd (d), "selection info, end: a caret already there stays");
    sel (d, 90, 90, 2, 2);
    check (se::selectionToEnd (d) && is (d, 60, 60, 2, 2), "selection info, end: a caret past its track's end comes back to it");
    sel (d, 30, 30, 3, 3);
    check (se::selectionToEnd (d) && is (d, 0, 0, 3, 3), "selection info, end: a caret in an empty track goes to 0, its end");

    // --- the Set Loop region is never touched
    sel (d, 30, 50, 1, 4);
    se::setLoop (d);
    sel (d, 40, 45, 2, 2);
    se::selectionToStart (d); se::selectionToWholeTracks (d); se::selectionToEnd (d);
    check (d.loopSt == 30 && d.loopEnd == 50, "selection info: the Set Loop region is left alone");

    // --- an empty sound: nothing to do
    {
        auto e = rampDocument ({ 0, 0 });
        check (! se::selectionToStart (*e) && ! se::selectionToWholeTracks (*e) && ! se::selectionToEnd (*e)
               && e->selSt == 0 && e->selEnd == 0, "selection info: an empty sound does nothing");
    }

    // --- frames: 16-bit tracks end on a frame; a ragged tail is snapped down
    {
        se::DocumentInit init; init.numTracks = 2; init.bps = 16; init.sizeBytes = 0;
        se::SoundDocument s (init);
        s.tracks[0].data.assign (200, 0);
        s.tracks[1].data.assign (301, 0);   // malformed: half a frame at the end
        sel (s, 40, 40, 1, 1);
        check (se::selectionToEnd (s) && is (s, 200, 200, 1, 1), "selection info, 16 bit: the caret goes to the track's last frame boundary");
        sel (s, 40, 40, 1, 2);
        check (se::selectionToWholeTracks (s) && s.selEnd == 300 && s.selEnd % (size_t) s.frameSize() == 0,
               "selection info, 16 bit: a ragged tail is snapped to a frame");
    }

    // --- playing: a region loop is not re-aimed by the icons; a selection loop follows them
    {
        se::PlayOptions region; region.loop = true; region.useLoopRegion = true;
        auto r = rampDocument ({ 100, 60 });
        sel (*r, 30, 50, 1, 2);
        se::setLoop (*r);
        sel (*r, 10, 20, 2, 2);
        se::Transport tr (kRate, 0.0);
        tr.play (*r, region);
        renderTransport (tr, *r, 25);
        se::selectionToWholeTracks (*r);
        renderTransport (tr, *r, 100);
        check (tr.uiPlaySt() == 30 && tr.uiPlayEnd() == 50, "selection info, playing: the Set Loop region keeps looping");

        auto q = rampDocument ({ 100, 60 });
        sel (*q, 10, 20, 2, 2);
        se::Transport t2 (kRate, 0.0);
        t2.play (*q, region);
        renderTransport (t2, *q, 25);
        se::selectionToWholeTracks (*q);
        renderTransport (t2, *q, 100);
        check (t2.uiPlaySt() == 0 && t2.uiPlayEnd() == 60 && t2.uiFirstPlayTrack() == 2 && t2.uiLastPlayTrack() == 2,
               "selection info, playing: a selection loop re-aims at the whole track");
    }
}

void transportButtonsTests()
{
    using B = se::TransportButtons;
    using Q = se::TransportCue;
    using R = se::RecordLight;
    using S = se::RollState;

    // --- the note: initial state ---------------------------------------------------
    {
        const B b = B::initial();
        check (lights (b, R::off, false, false, false, false) && b.loopOn(),
               "buttons initial: everything off but Loop");
        check (b.directionEnabled() && b.ffEnabled() && b.loopEnabled() && ! b.takeOpen(),
               "buttons initial: Direction, FF and Loop enabled, no take");
    }

    // --- Record: R / click arms and disarms ------------------------------------------
    {
        B b = B::initial();
        auto c = b.pressRecord();
        check (c.empty() && b.record() == R::armed && ! b.playOn(), "record: R arms (blinks), nothing starts");
        check (! b.directionEnabled() && ! b.ffEnabled() && ! b.loopEnabled(), "record armed: Direction, FF, Loop disabled");
        check (b.ffDown().empty() && ! b.ffOn(), "record armed: FF does nothing");
        b.toggleLoop();
        check (b.loopOn(), "record armed: Loop cannot be toggled");
        c = b.pressRecord();
        check (c.empty() && b.record() == R::off && b.directionEnabled() && b.ffEnabled() && b.loopEnabled() && b.loopOn(),
               "record: R again disarms; Direction, FF, Loop back as they were");

        b.toggleLoop();   // Loop off before arming: it stays off through arm/disarm
        b.pressRecord(); b.pressRecord();
        check (! b.loopOn(), "record: disarming gives Loop back its own state (off)");
    }
    {
        // Armed, then Play: recording starts.
        B b = B::initial();
        b.pressRecord();
        auto c = b.pressPlay();
        check (cuesAre (c, { Q::takeStart }) && lights (b, R::on, false, true, false, false) && b.takeOpen(),
               "record armed + Play: Record on, the take starts");
        // Space does the same.
        B s = B::initial();
        s.pressRecord();
        check (cuesAre (s.space(), { Q::takeStart }) && s.record() == R::on, "record armed + Space: the take starts");
    }
    {
        // Play already on, then R: recording starts at once.
        B b = B::initial();
        b.pressPlay();
        auto c = b.pressRecord();
        check (cuesAre (c, { Q::takeStart }) && lights (b, R::on, false, true, false, false),
               "record while playing: Record on at once, the take joins the rolling transport");
        // Disarm mid-take: the take ends, playback carries on.
        c = b.pressRecord();
        check (cuesAre (c, { Q::takeEnd }) && lights (b, R::off, false, true, false, false) && ! b.takeOpen(),
               "record: disarm mid-take ends the take, Play stays on");
        check (b.directionEnabled() && b.ffEnabled() && b.loopEnabled(), "record: disarm mid-take re-enables Direction, FF, Loop");
    }
    {
        // ⇧R: direct recording, from every state.
        B b = B::initial();
        auto c = b.directRecord();
        check (cuesAre (c, { Q::takeStart }) && lights (b, R::on, false, true, false, false) && ! b.ffEnabled(),
               "direct recording from the initial state: Record and Play on, the take starts");
        check (b.directRecord().empty(), "direct recording while recording: nothing");

        B p = B::initial(); p.pressPlay(); p.pressPause();
        c = p.directRecord();
        check (cuesAre (c, { Q::takeStart }) && lights (p, R::on, false, true, false, false),
               "direct recording from Pause: Pause off, Record and Play on");

        B s = B::initial(); s.pressStop();
        s.directRecord();
        check (lights (s, R::on, false, true, false, false), "direct recording from Stop: Stop off");

        B a = B::initial(); a.pressRecord();
        check (cuesAre (a.directRecord(), { Q::takeStart }) && a.record() == R::on, "direct recording from armed: starts");
    }
    {
        // A take that cannot start puts Record back off.
        B b = B::initial(); b.pressRecord(); b.pressPlay();
        b.takeRefused (false);
        check (lights (b, R::off, false, false, false, false) && ! b.takeOpen(), "record refused from stopped: all off again");
        B p = B::initial(); p.pressPlay(); p.pressRecord();
        p.takeRefused (true);
        check (lights (p, R::off, false, true, false, false), "record refused while playing: Play stays on");
    }

    // --- Stop: latches; Record, Play, Pause off ---------------------------------------
    {
        B b = B::initial();
        b.pressPlay();
        auto c = b.pressStop();
        check (cuesAre (c, { Q::stop }) && lights (b, R::off, true, false, false, false), "stop from Play: latches, Play off");
        c = b.pressStop();
        check (cuesAre (c, { Q::stop }) && b.stopOn(), "stop again: stays latched");

        B r = B::initial(); r.directRecord();
        c = r.pressStop();
        check (cuesAre (c, { Q::stop }) && lights (r, R::off, true, false, false, false) && ! r.takeOpen(),
               "stop while recording: the take and playback stop, Record off");
        check (r.directionEnabled() && r.ffEnabled() && r.loopEnabled(), "stop after a take: Direction, FF, Loop enabled");

        B p = B::initial(); p.pressPlay(); p.pressPause();
        p.pressStop();
        check (lights (p, R::off, true, false, false, false), "stop from Pause: Pause off");

        B a = B::initial(); a.pressRecord();
        a.pressStop();
        check (a.record() == R::off, "stop with Record armed: disarms");
    }

    // --- Direction: disabled while Record is armed or on ------------------------------
    {
        B b = B::initial();
        check (b.directionEnabled(), "direction: enabled initially");
        b.directRecord();
        check (! b.directionEnabled(), "direction: disabled while recording");
        b.pressPause();
        check (! b.directionEnabled(), "direction: disabled while the take is held (armed)");
        b.pressStop();
        check (b.directionEnabled(), "direction: enabled after Stop");
    }

    // --- Play: latches; Pause off; armed Record on ------------------------------------
    {
        B b = B::initial();
        auto c = b.pressPlay();
        check (cuesAre (c, { Q::play }) && lights (b, R::off, false, true, false, false), "play from initial: latches on");
        check (b.pressPlay().empty() && b.playOn(), "play again: push-on, nothing more");
        b.pressPause();
        c = b.pressPlay();
        check (cuesAre (c, { Q::resume }) && lights (b, R::off, false, true, false, false), "play from Pause: Pause off, resumes");
        b.pressStop();
        c = b.pressPlay();
        check (cuesAre (c, { Q::play }) && lights (b, R::off, false, true, false, false), "play from Stop: Stop off");
    }

    // --- Pause: latches; Play off; Record on -> armed ----------------------------------
    {
        B b = B::initial();
        check (b.pressPause().empty() && ! b.pauseOn(), "pause with nothing rolling: nothing to hold");
        b.pressPlay();
        auto c = b.pressPause();
        check (cuesAre (c, { Q::pause }) && lights (b, R::off, false, false, true, false), "pause from Play: latches, Play off");
        check (b.pressPause().empty() && b.pauseOn(), "pause again: push-on, stays paused");
        check (cuesAre (b.space(), { Q::resume }) && b.playOn(), "space from Pause: plays");
        check (cuesAre (b.space(), { Q::pause }) && b.pauseOn(), "space from Play: pauses");

        B r = B::initial(); r.directRecord();
        c = r.pressPause();
        check (cuesAre (c, { Q::takePause }) && lights (r, R::armed, false, false, true, false) && r.takeOpen(),
               "pause while recording: Record back to armed (blinks), the take held");
        c = r.pressPlay();
        check (cuesAre (c, { Q::takeResume }) && lights (r, R::on, false, true, false, false),
               "play on a held take: Record on, the same take carries on");
        r.pressPause();
        c = r.pressRecord();
        check (cuesAre (c, { Q::takeEnd }) && lights (r, R::off, false, false, true, false) && ! r.takeOpen(),
               "disarm a held take: it ends, Pause stays on");
        c = r.pressPlay();
        check (cuesAre (c, { Q::resume }), "play after that: plain playback, no take");

        B a = B::initial(); a.pressPlay(); a.pressPause(); a.pressRecord();
        c = a.pressPlay();
        check (cuesAre (c, { Q::takeStart }) && a.record() == R::on, "armed while paused, then Play: a new take starts");
    }

    // --- FF: momentary, in its three cases --------------------------------------------
    {
        // IF Play is on.
        B b = B::initial(); b.pressPlay();
        const B before = b;
        auto c = b.ffDown();
        check (cuesAre (c, { Q::fastOn }) && lights (b, R::off, false, true, false, true), "FF held from Play: FF on");
        check (b.ffDown().empty(), "FF held: a repeat press does nothing");
        c = b.ffUp();
        check (cuesAre (c, { Q::fastOff }) && b == before, "FF let go from Play: FF off, Play still on");
    }
    {
        // IF Pause is on.
        B b = B::initial(); b.pressPlay(); b.pressPause();
        const B before = b;
        auto c = b.ffDown();
        check (cuesAre (c, { Q::resume, Q::fastOn }) && lights (b, R::off, false, true, false, true),
               "FF held from Pause: FF and Play on, Pause off");
        c = b.ffUp();
        check (cuesAre (c, { Q::fastOff, Q::pause }) && b == before, "FF let go from Pause: back to Pause");
    }
    {
        // IF Stop is on.
        B b = B::initial(); b.pressStop();
        const B before = b;
        auto c = b.ffDown();
        check (cuesAre (c, { Q::play, Q::fastOn }) && lights (b, R::off, false, true, false, true),
               "FF held from Stop: FF and Play on, Stop off");
        c = b.ffUp();
        check (cuesAre (c, { Q::stop }) && b == before, "FF let go from Stop: back to Stop");
    }
    {
        // The initial state (not in the table): as Stop, and back to the initial state.
        B b = B::initial();
        b.ffDown();
        check (b.playOn() && b.ffOn() && ! b.stopOn(), "FF held from the initial state: plays");
        auto c = b.ffUp();
        check (cuesAre (c, { Q::stop }) && b == B::initial(), "FF let go from the initial state: back to it, Stop unlit");
    }
    {
        // Another press while FF is held: FF lets go where it is, nothing is restored.
        B b = B::initial(); b.pressStop();
        b.ffDown();
        auto c = b.space();
        check (cuesAre (c, { Q::fastOff }) && lights (b, R::off, false, true, false, false),
               "space while FF held from Stop: FF off, playback stays on");
        check (b.ffUp().empty() && b.playOn(), "FF let go after that: nothing to restore");

        B p = B::initial(); p.pressPlay(); p.pressPause(); p.ffDown();
        c = p.pressPause();
        check (cuesAre (c, { Q::fastOff, Q::pause }) && p.pauseOn() && ! p.ffOn(), "pause while FF held: pauses, FF off");

        B s = B::initial(); s.pressPlay(); s.ffDown();
        c = s.pressStop();
        check (cuesAre (c, { Q::stop }) && lights (s, R::off, true, false, false, false) && s.ffUp().empty(),
               "stop while FF held: stops, the release does nothing");

        B r = B::initial(); r.pressPlay(); r.ffDown();
        c = r.pressRecord();
        check (cuesAre (c, { Q::fastOff, Q::takeStart }) && r.record() == R::on && ! r.ffOn(),
               "R while FF held: FF off, then recording starts");
    }

    // --- Loop: toggles (on initially) -------------------------------------------------
    {
        B b = B::initial();
        b.toggleLoop();
        check (! b.loopOn(), "loop: L turns it off");
        b.toggleLoop();
        check (b.loopOn(), "loop: L again turns it on");
        b.pressPlay(); b.toggleLoop();
        check (! b.loopOn() && b.playOn(), "loop: toggles while playing, playback untouched");
    }

    // --- following the engine -------------------------------------------------------
    {
        B b = B::initial(); b.pressPlay();
        b.engineChanged (S::stopped, S::stopped);
        check (b.playOn(), "engine: no edge (a cue not applied yet) changes nothing");
        b.engineChanged (S::playing, S::stopped);
        check (lights (b, R::off, true, false, false, false), "engine: a pass that runs out latches Stop");

        B k = B::initial();
        k.engineChanged (S::stopped, S::playing);
        check (lights (k, R::off, false, true, false, false), "engine: the Keyboard starts it, Play lights");

        B r = B::initial(); r.directRecord();
        r.engineChanged (S::playing, S::stopped);
        check (r.record() == R::on, "engine: a take's transport stopping is the Recorder's to report");
        r.ended();
        check (lights (r, R::off, true, false, false, false) && ! r.takeOpen(), "ended: a punch-out ends as a Stop");
    }
}

// --- Main Level (0.5.0): the master gain over the whole mix ------------------
void masterLevelTests()
{
    const float tol = 1.0e-4f;
    se::Engine e;
    e.prepare (kRate, kBlock); e.setDeclickMs (0.0);
    const uint64_t a = e.addWindow (dcDocument (10, "a"));
    const uint64_t b = e.addWindow (dcDocument (40, "b"));
    se::PlayOptions loopAll; loopAll.loop = true;
    e.play (a, loopAll); e.play (b, loopAll);
    auto* wa = e.windowById (a); auto* wb = e.windowById (b);

    wa->soloed = true; const float la = renderLevel (e);
    wa->soloed = false; wb->soloed = true; const float lb = renderLevel (e);
    wb->soloed = false;
    check (la > 0.0f && lb > la, "master: each channel sounds on its own");

    check (e.masterGain() == 1.0f, "master: unity by default");
    checkClose (renderLevel (e), la + lb, tol, "master: unity leaves the mix alone");

    // A change lands as a ramp over the block, then holds: settle it first.
    e.setMasterGain (0.5f);
    renderBlocks (e, 1);
    checkClose (renderLevel (e), 0.5f * (la + lb), tol, "master: 0.5 halves the mix");

    // The fader's +6 dB is past unity and below the clamp here.
    e.setMasterGain (1.5f);
    renderBlocks (e, 1);
    checkClose (renderLevel (e), std::min (1.0f, 1.5f * (la + lb)), tol, "master: above unity boosts");

    // Applied BEFORE the final clamp: a boost that overshoots clips at 1, and the
    // per-Window VU is still the Window's own pre-master level.
    e.setMasterGain (se::Engine::kMaxMasterGain);
    wa->gain = 1.0f; wb->gain = 1.0f;
    renderBlocks (e, 1);
    {
        std::vector<float> l ((size_t) kBlock), r ((size_t) kBlock);
        e.render (l.data(), r.data(), kBlock);
        float mx = 0.0f;
        for (float v : l) mx = std::max (mx, std::abs (v));
        check (mx <= 1.0f, "master: the clamp comes after Main Level");
        check ((la + lb) * se::Engine::kMaxMasterGain > 1.0f ? mx == 1.0f : true, "master: an overshoot clips at full scale");
        checkClose (wa->meterLeft.load(), la, tol, "master: a channel's VU is its own, before Main Level");
        checkClose (e.masterMeterLeft(), mx, tol, "master: the Main VU reads the output");
    }

    // Silence the mix; the VU falls on its own and the channels keep their own meters.
    e.setMasterGain (0.0f);
    renderBlocks (e, 1);
    check (renderLevel (e) == 0.0f, "master: 0 silences the mix");

    // ...and never stops a transport: both channels rolled through the silence.
    const size_t pa = wa->transport->uiPlayhead();
    renderBlocks (e, 10);
    check (wa->transport->uiPlayhead() != pa || wb->transport->uiPlayhead() != 0,
           "master: a shut Main Level does not stop a transport");
    check (wa->transport->uiState() == se::TransportState::playing && wb->transport->uiState() == se::TransportState::playing, "master: both transports still play");

    // Bad values: clamped, NaN reads as unity.
    e.setMasterGain (-3.0f);
    check (e.masterGain() == 0.0f, "master: negative clamps to 0");
    e.setMasterGain (1000.0f);
    check (e.masterGain() == se::Engine::kMaxMasterGain, "master: huge clamps to the maximum");
    e.setMasterGain (std::nanf (""));
    check (e.masterGain() == 1.0f, "master: NaN reads as unity");

    // The Main source hears what the speakers get; "the mix without Window b" stays exact.
    e.setMasterGain (0.5f);
    renderBlocks (e, 1);
    se::CaptureSetup mix; mix.kind = se::AudioSourceKind::canvas;
    e.startCapture (mix);
    renderBlocks (e, 1);
    Drained d = drain (e);
    checkClose (d.l, 0.5f * (la + lb), tol, "master: the Main source is after Main Level");
    e.stopCapture();
    renderBlocks (e, 1);
    mix.excludeWindow = b;
    e.startCapture (mix);
    renderBlocks (e, 1);
    d = drain (e);
    checkClose (d.l, 0.5f * la, tol, "master: the Main source without a Window is scaled alike");
    e.stopCapture();
    renderBlocks (e, 1);
}

// --- a canvas with no Sound (0.6.0 step 1.5) ---------------------------------
// A new canvas is empty: the mix, the keys, solo and mute all meet no Window.
void emptyCanvasTests()
{
    se::Engine e;
    e.prepare (48000.0, 512); e.setDeclickMs (0.0);
    check (renderLevel (e) == 0.0f && e.numWindows() == 0, "empty canvas: renders silence");

    se::KeyboardVoice keys (e);
    check (keys.target() == 0 && ! keys.hasTarget() && keys.baseNote() == 60, "empty canvas: the keys have no target");
    keys.strike (60);
    keys.release();
    e.clickSolo (0, true);
    e.unmuteAll();
    e.clearAllSolos();
    check (! e.anyWindowSoloed() && e.highestSlot() < 0 && renderLevel (e) == 0.0f,
           "empty canvas: keys, solo and mute do nothing");
}

// --- Main Mute (0.6.0 step 1.4) ----------------------------------------------
void mainMuteTests()
{
    se::Engine e;
    e.prepare (48000.0, 512);
    const uint64_t a = e.addWindow (dcDocument (20, "a"));
    se::PlayOptions loop;
    loop.loop = true;
    e.play (a, loop);
    (void) renderLevel (e);   // the start fades in
    renderBlocks (e, 4);
    const float level = renderLevel (e);
    check (level > 0.0f && ! e.masterMuted(), "main mute: off by default");

    e.setMasterGain (0.5f);
    (void) renderLevel (e);

    // The block that takes the mute ramps down to silence, it does not cut.
    std::vector<float> l (512), r (512);
    e.setMasterMuted (true);
    e.render (l.data(), r.data(), 512);
    float step = 0.0f;
    for (size_t i = 1; i < l.size(); ++i) step = std::max (step, std::abs (l[i] - l[i - 1]));
    check (l[0] > 0.45f * level && l[511] == 0.0f, "main mute: ramps from the level to zero");
    check (step < level / 400.0f, "main mute: no jump inside the ramp");
    check (renderLevel (e) == 0.0f, "main mute: silent");
    check (e.masterGain() == 0.5f, "main mute: the fader keeps its value");
    check (e.windowById (a)->transport->uiState() == se::TransportState::playing, "main mute: the transport still plays");

    e.setMasterGain (1.0f);
    check (renderLevel (e) == 0.0f, "main mute: the fader does not unmute");
    e.setMasterMuted (false);
    e.render (l.data(), r.data(), 512);
    check (l[0] < level / 100.0f && std::abs (l[511] - level) < 1.0e-4f, "main mute: ramps back up");
    checkClose (renderLevel (e), level, 1.0e-4f, "main mute: off again");
}


// 10.2: where the Effects column scrolls to when a tray opens.
static void scrollTargetTests()
{
    std::printf ("scroll target\n");
    const auto go = [] (int viewTop, int viewH, int contentH, int groupTop, int trayTop, int trayBottom)
    {
        return se::scrollTargetForTray ({ viewTop, viewH, contentH, groupTop, trayTop, trayBottom });
    };
    // 400 px view over a 1000 px column. Group 100 tall, tray 150.
    check (go (0, 400, 1000, 300, 400, 550) == 275,   "tray fits: centred (150 tall in 400 -> 125 above)");
    check (go (700, 400, 1000, 300, 400, 550) == 275, "centred from a position below too");
    check (go (275, 400, 1000, 300, 400, 550) == 275, "already centred: unchanged");
    check (go (0, 400, 1000, 100, 200, 350) == 75,    "a tray near the top of the column: centred");
    check (go (0, 400, 1000, 20, 60, 160) == 0,       "near the top: clamped to 0");
    check (go (0, 400, 700, 300, 400, 700) == 300,    "tray 300: centred 350 clamped to the end of the column");
    check (go (0, 400, 1000, 100, 200, 600) == 200,   "tray exactly the view: top under the header");
    check (go (300, 400, 1000, 100, 200, 700) == 200, "tray taller than view: top under the header");
    check (go (0, 400, 300, 100, 200, 250) == 0,      "column shorter than the view: never scrolls");
    check (go (50, 400, 1000, 100, 200, 200) == 50,   "empty tray: unchanged");
    check (go (900, 400, 1000, 100, 200, 350) == 75,  "a stale position past the end is brought back");
}

// --- MIDI (0.5.0 step 12.1) -------------------------------------------------------
static void midiNoteTests()
{
    std::printf ("midi notes\n");
    se::NoteStack s;
    check (s.empty() && s.top() == -1, "empty stack: nothing sounds");
    check (s.press (60) == 60, "first key sounds");
    check (s.press (64) == 64, "last note priority: the newer key wins");
    check (s.press (67) == 67, "third key wins");
    check (s.lift (64) == 67, "lifting a held-under key changes nothing");
    check (s.lift (67) == 60, "lifting the top brings back the one before it");
    check (s.lift (60) == -1 && s.empty(), "last key up: silence");
    check (s.lift (60) == -1, "a stray note-off is harmless");

    s.press (60); s.press (62); s.press (60);
    check (s.size() == 2 && s.top() == 60, "a key struck twice counts once, as the newest");
    check (s.lift (60) == 62, "...and one note-off lets it go");
    s.clear();
    check (s.empty(), "clear");

    check (s.press (-1) == -1 && s.press (128) == -1 && s.empty(), "out-of-range numbers are ignored");

    for (int n = 40; n < 40 + 20; ++n) s.press (n);   // more than sixteen fingers
    check (s.size() == se::NoteStack::kMax && s.top() == 59, "sixteen keys max, newest on top");
    for (int n = 59; n >= 44; --n) s.lift (n);
    check (s.empty(), "the oldest four gave way, the rest come off in turn");

    // Fingers with their own identity and bend (MPE): the newest sounds with its own bend.
    s.clear();
    s.press (1001, 60, 0.5);
    s.press (1002, 64, -2.0);
    check (s.top() == 64 && s.topId() == 1002 && s.topBend() == -2.0, "MPE key: note, id and bend of the top");
    s.setBend (1001, 7.0);
    check (s.topBend() == -2.0, "bending a key under the sounding one does not touch it");
    s.setBend (1002, 3.25);
    check (s.topBend() == 3.25, "bending the sounding key moves its bend");
    check (s.lift (1002) == 60 && s.topBend() == 7.0, "lifting it brings the other back with its own bend");
    s.press (60);
    check (s.size() == 2, "a plain key and an MPE key on the same note are two fingers");
    s.clear();

    // The bend wheel: centre, ends, clamping.
    check (se::midibend::normalised (8192) == 0.0, "bend centre");
    check (se::midibend::normalised (0) == -1.0, "bend bottom");
    check (se::midibend::normalised (16383) == 1.0, "bend top reaches +1");
    check (se::midibend::normalised (-5) == -1.0 && se::midibend::normalised (99999) == 1.0, "bend clamps");
    checkClose ((float) se::midibend::semitones (16383, 2.0), 2.0f, 1e-6f, "plain bend: +2 st at the top");
    checkClose ((float) se::midibend::semitones (0, 48.0), -48.0f, 1e-6f, "MPE range 48: -48 st at the bottom");

    // The queue carries events in order and drops the newest when full.
    se::MidiQueue q;
    for (int i = 0; i < 255; ++i)
    {
        se::MidiEvent e; e.note = (uint8_t) (i & 127); e.channel = (uint8_t) (i % 16);
        if (! q.push (e)) { check (false, "queue took 255"); break; }
    }
    se::MidiEvent extra;
    check (! q.push (extra), "a full queue refuses the newest");
    se::MidiEvent out; bool inOrder = true;
    for (int i = 0; i < 255; ++i)
        if (! q.pop (out) || out.note != (uint8_t) (i & 127)) inOrder = false;
    check (inOrder && ! q.pop (out), "events come out in order, then empty");
}


// The keyboard voice without a palette (0.5.0 step 12.3): MIDI plays with no editor.
static void keyboardVoiceTests()
{
    std::printf ("keyboard voice\n");
    se::Engine e;
    e.prepare (kRate, kBlock); e.setDeclickMs (0.0);
    const uint64_t w = e.addWindow (dcDocument (20, "w"));
    se::KeyboardVoice v (e);
    const auto playing = [&] { renderLevel (e); return e.windowById (w)->transport->uiState() == se::TransportState::playing; };

    // No editor ever aimed the keys: the first Window stands in.
    check (v.target() == w && v.hasTarget(), "no editor yet: the first Window is the target");
    v.midiNoteOn (64, 64, 0.0);
    check (playing(), "a note starts the Window");
    v.midiNoteOn (67, 67, 0.0);
    v.midiNoteOff (67);
    check (playing() && v.heldNote() == 64, "last-note priority: the earlier key sounds again");
    v.midiBend (64, 5.0);
    check (playing() && std::abs (v.slide() - 5.0) < 1e-9, "a bend on the sounding key slides it");
    v.midiNoteOff (64);
    check (! playing() && v.heldNote() == -1, "last key up stops what the keys started");

    v.midiNoteOn (10, 10, 0.0);   // below the drawn keyboard
    check (! playing(), "a note outside the keyboard is ignored");

    v.setTarget (0);
    v.midiNoteOn (64, 64, 0.0);
    check (! playing(), "an editor that aimed at nothing: silence");
    v.setTarget (w);
    v.midiNoteOn (64, 64, 0.0);
    check (playing(), "aimed at the Window again: it sounds");
    v.midiAllOff();
    check (! playing(), "all notes off");

    // A Window that was already rolling is borrowed, and handed back still rolling.
    se::PlayOptions o; o.loop = true;
    e.play (w, o);
    check (playing(), "(set up) the Window rolls by itself");
    v.midiNoteOn (72, 72, 0.0);
    v.midiNoteOff (72);
    check (playing(), "a rolling Window is only re-pitched, and keeps rolling");
}

// --- Magnetophon (0.5.0 step 11.1) ------------------------------------------------
// The pure tape model first: takes as start markers, tape-like record-over,
// play to the end of the tape, next / previous, unsent audio.
static std::vector<size_t> starts (const se::Tape& t)
{
    std::vector<size_t> v;
    for (const auto& k : t.takes()) v.push_back (k.start);
    return v;
}

static void tapeModelTests()
{
    std::printf ("tape model\n");
    constexpr double R = 48000.0;
    {
        se::Tape t;
        check (t.empty() && ! t.play() && ! t.nextTake() && ! t.previousTake (0) && ! t.hasUnsent(),
               "blank tape: nothing to play, no takes, nothing unsent");
        check (! t.stop(), "blank tape: Stop does nothing");
    }
    {
        se::Tape t;
        check (t.record (R) && t.mode() == se::Tape::Mode::recording && t.recordingTake() == 0, "Record on a blank tape starts take 1 at 0");
        check (! t.record (R), "Record while recording, nothing recorded yet: refused");
        check (! t.play() && ! t.nextTake() && ! t.previousTake (0) && ! t.seek (0), "while recording: Play, Next, Previous and seek refused");
        t.recorded (100);
        check (t.stop() && t.length() == 100 && t.position() == 100 && starts (t) == std::vector<size_t> { 0 },
               "Stop: one take of 100 frames, the tape where it stopped");
        check (t.hasUnsent() && t.rate() > 47999.0, "the take is unsent; the tape took the device rate");
        check (t.record (R), "Record again at the end");
        t.recorded (50); t.stop();
        check (starts (t) == std::vector<size_t> { 0, 100 } && t.length() == 150, "a second take appended at 100");
        check (t.takeEnd (0) == 100 && t.takeEnd (1) == 150 && t.takeAt (120) == 1 && t.takeAt (99) == 0, "take spans");

        // Previous: inside a take past the grace -> its start; at its start -> the one before.
        check (t.previousTake (10) && t.position() == 100, "Previous from the end: the start of the last take");
        check (t.previousTake (10) && t.position() == 0, "Previous at a take's start: the take before");
        check (! t.previousTake (10), "Previous at 0: nothing");
        t.seek (105);
        check (t.previousTake (10) && t.position() == 0, "Previous within the grace of a take's start: the take before");
        check (t.nextTake() && t.position() == 100, "Next: the next take's start");
        check (t.nextTake() && t.position() == 150, "Next from the last take: the end of the tape");
        check (! t.nextTake(), "Next at the end: nothing");

        check (t.play() && t.position() == 100 && t.mode() == se::Tape::Mode::playing, "Play at the end replays the last take from its start");
        t.played (130);
        check (t.position() == 130 && t.mode() == se::Tape::Mode::playing, "playback follows");
        check (t.previousTake (10) && t.position() == 100 && t.mode() == se::Tape::Mode::playing, "Previous while playing: plays on from the take's start");
        check (t.nextTake() && t.position() == 150 && t.mode() == se::Tape::Mode::stopped, "Next off the end while playing: stops at the end");
        t.seek (0); t.play(); t.played (150);
        check (t.mode() == se::Tape::Mode::stopped && t.position() == 150, "playback across takes stops at the end of the tape");
        t.seek (20); t.play(); t.played (60);
        check (t.stop() && t.position() == 60 && t.mode() == se::Tape::Mode::stopped, "Stop while playing: the tape stays where it is");
    }
    {
        // Record-over, tape-like: four takes of 100; stopped at 150, record 100 frames.
        se::Tape t;
        for (int i = 0; i < 4; ++i) { t.record (R); t.recorded (100); t.stop(); }
        t.markSent (2);
        check (starts (t) == std::vector<size_t> { 0, 100, 200, 300 } && t.length() == 400, "four takes");
        t.seek (150);
        t.record (R);
        check (starts (t) == std::vector<size_t> { 0, 100, 150, 200, 300 }, "Record mid-take: a new marker at the position");
        t.recorded (100);
        check (starts (t) == std::vector<size_t> { 0, 100, 150, 300 } && t.length() == 400 && t.position() == 250,
               "the marker run over (200) is gone; the tape keeps its length");
        t.stop();
        check (starts (t) == std::vector<size_t> { 0, 100, 150, 250, 300 }, "stopped inside older audio: its tail is a take of its own");
        check (t.takes()[3].sent && ! t.takes()[2].sent && ! t.takes()[1].sent, "the tail keeps the run-over take's flag; the new take is unsent");
        check (t.takeEnd (1) == 150, "the take recorded over is cut where the new one starts");

        // Recording that ends exactly on a marker leaves it alone, adds nothing.
        t.seek (250); t.record (R); t.recorded (50); t.stop();
        check (starts (t) == std::vector<size_t> { 0, 100, 150, 250, 300 }, "a take ending on the next marker: markers unchanged");
        check (! t.takes()[3].sent, "the take at 250 was re-recorded: unsent");

        // Past the end: the tape grows, no tail.
        t.seek (350); t.record (R); t.recorded (100); t.stop();
        check (starts (t) == std::vector<size_t> { 0, 100, 150, 250, 300, 350 } && t.length() == 450, "recording over the end grows the tape");

        // Record and Stop with nothing recorded: as if nothing happened.
        t.markAllSent();
        t.seek (100); t.record (R); t.stop();
        check (starts (t) == std::vector<size_t> { 0, 100, 150, 250, 300, 350 } && t.takes()[1].sent && ! t.hasUnsent(),
               "an empty take on a marker puts the marker (and its flag) back");
        t.seek (120); t.record (R); t.stop();
        check (starts (t) == std::vector<size_t> { 0, 100, 150, 250, 300, 350 } && ! t.hasUnsent(), "an empty take mid-take leaves nothing");
        t.seek (0); t.record (R); t.recorded (450); t.stop();
        check (starts (t) == std::vector<size_t> { 0 } && t.length() == 450, "recording over the whole tape leaves one take");
        check (t.hasUnsent(), "and it is unsent");
        t.markAllSent();
        check (! t.hasUnsent(), "all sent: nothing unsent");
    }
    {
        se::Tape t;
        t.record (R); t.recorded (50);
        check (t.record (R) && t.recordingTake() == 1 && starts (t) == std::vector<size_t> { 0, 50 }, "Record while recording: the next take starts at the head");
        t.recorded (30); t.stop();
        check (t.length() == 80 && t.numTakes() == 2 && t.takeEnd (1) == 80, "...seamlessly: 50 + 30");
    }
    {
        se::Tape t;
        t.record (48000.0); t.recorded (10); t.stop();
        check (! t.record (44100.0) && t.mode() == se::Tape::Mode::stopped, "a tape recorded at another device rate refuses to record");
        t.clear();
        check (t.empty() && t.numTakes() == 0 && t.position() == 0 && t.record (44100.0), "cleared: blank, and takes the new rate");
    }
}

/** Render one block with constant input levels, then service the deck. */
static void tapeBlock (se::Engine& e, se::TapeDeck& d, std::initializer_list<float> in, std::vector<float>& l, std::vector<float>& r)
{
    renderWithInput (e, in, l, r);
    d.service();
}

static void tapeSettle (se::Engine& e, se::TapeDeck& d, std::initializer_list<float> in)
{
    std::vector<float> l, r;
    for (int i = 0; i < 100 && (d.finishing() || d.tape().mode() == se::Tape::Mode::recording); ++i) tapeBlock (e, d, in, l, r);
}

static float tapeAt (se::Engine& e, size_t frame, bool right = false)
{
    float l = 0, r = 0;
    e.tapeStore().read (frame, &l, &r, 1);
    return right ? r : l;
}

static void tapeEngineTests()
{
    std::printf ("tape engine\n");
    std::vector<float> l, r;
    {
        // Main: what the speakers get after Main Level.
        se::Engine e; e.prepare (kRate, kBlock); e.setDeclickMs (0.0);
        const uint64_t a = e.addWindow (dcDocument (40, "a"));
        se::PlayOptions loopAll; loopAll.loop = true;
        e.play (a, loopAll);
        e.setMasterGain (0.5f);
        renderWithInput (e, {}, l, r);
        renderWithInput (e, {}, l, r);
        const float heard = l[0];
        se::TapeDeck d (e);
        check (d.record(), "Record with the Main source");
        for (int i = 0; i < 10; ++i) tapeBlock (e, d, {}, l, r);
        check (d.tape().length() == (size_t) (10 * kBlock), "the take grows as the blocks come in");
        d.stop();
        check (d.finishing(), "Stop: finishing until the audio thread lets go");
        tapeSettle (e, d, {});
        check (d.tape().mode() == se::Tape::Mode::stopped && d.tape().length() == (size_t) (10 * kBlock),
               "Stop: exactly the blocks rendered while recording, nothing after");
        checkClose (tapeAt (e, 5), heard, 1e-6f, "the tape holds what the speakers got (Main Level included)");
        size_t from = 0, to = 0;
        check (d.takeWrittenFrames (from, to) && from == 0 && to == (size_t) (10 * kBlock), "the written range is reported once");
        check (! d.takeWrittenFrames (from, to), "...and only once");

        // Playback: heard through Main Level, and NOT re-captured by the Main source.
        e.stop (a);
        renderWithInput (e, {}, l, r);
        se::CaptureSetup main; main.kind = se::AudioSourceKind::canvas;
        e.startCapture (main);
        check (d.play() && d.tape().position() == 0, "Play at the end of a one-take tape: from its start");
        renderWithInput (e, {}, l, r);
        checkClose (l[0], heard * 0.5f, 1e-6f, "tape playback is heard, scaled by Main Level");
        float got[4]; float gr[4];
        e.captureRing().discard();
        renderWithInput (e, {}, l, r);
        const size_t n = e.captureRing().pop (got, gr, 4);
        check (n == 4 && got[0] == 0.0f && l[0] > 0.0f, "the Recorder's Main Mix does not hear the tape");
        e.stopCapture();
        // Same for the tape's own Main capture, driven straight through the Engine.
        e.tapeCaptureStart ({});
        e.tapeRing().discard();
        renderWithInput (e, {}, l, r);
        const size_t m = e.tapeRing().pop (got, gr, 4);
        check (m == 4 && got[0] == 0.0f && l[0] > 0.0f, "the tape's Main source does not hear the tape's playback");
        e.tapeCaptureStop();
        renderWithInput (e, {}, l, r);
        e.tapeRing().discard();

        // Play runs to the end of the tape and stops by itself.
        for (int i = 0; i < 20; ++i) tapeBlock (e, d, {}, l, r);
        check (d.tape().mode() == se::Tape::Mode::stopped && d.tape().position() == d.tape().length() && ! e.tapePlaying(),
               "playback stops at the end of the tape");
        renderWithInput (e, {}, l, r);
        check (l[0] == 0.0f, "...and is silent after");

        d.clear();
        renderWithInput (e, {}, l, r);
        d.service();
        check (d.tape().empty() && e.tapeStore().reservedFrames() == 0 && e.tapeStore().length() == 0, "clear: blank tape, memory released");
    }
    {
        // External source (one channel on both sides, then a pair), gain, monitor.
        se::Engine e; e.prepare (kRate, kBlock); e.setDeclickMs (0.0);
        se::TapeDeck d (e);
        se::TapeSource ext; ext.kind = se::AudioSourceKind::external; ext.firstChannel = 1; ext.stereo = false;
        check (d.setSource (ext), "source: Input 2 alone");
        e.setTapeInputGain (2.0f);
        d.record();
        check (! d.setSource ({}), "the source cannot change while recording");
        tapeBlock (e, d, { 0.1f, 0.2f, 0.3f }, l, r);
        check (l[0] == 0.0f, "monitor off: the input is not heard");
        e.setTapeMonitor (true);
        tapeBlock (e, d, { 0.1f, 0.2f, 0.3f }, l, r);
        checkClose (l[0], 0.4f, 1e-6f, "monitor on: the input is heard, with its gain");
        e.setTapeMonitor (false);
        e.setTapeInputGain (10.0f);
        tapeBlock (e, d, { 0.1f, 0.2f, 0.3f }, l, r);
        d.stop(); tapeSettle (e, d, {});
        checkClose (tapeAt (e, 0), 0.4f, 1e-6f, "Input 2 x gain 2, left");
        checkClose (tapeAt (e, 0, true), 0.4f, 1e-6f, "...the same on the right");
        checkClose (tapeAt (e, (size_t) (2 * kBlock)), 1.0f, 1e-6f, "clamped to full scale");
        check (e.tapeMeter() > 0.0f, "the tape's meter moved");

        e.setTapeInputGain (1.0f);
        ext.stereo = true; ext.firstChannel = 1;
        d.setSource (ext);
        d.record();
        tapeBlock (e, d, { 0.1f, 0.2f, 0.3f }, l, r);
        d.stop(); tapeSettle (e, d, {});
        checkClose (tapeAt (e, (size_t) (3 * kBlock)), 0.2f, 1e-6f, "Inputs 2 + 3: left");
        checkClose (tapeAt (e, (size_t) (3 * kBlock), true), 0.3f, 1e-6f, "Inputs 2 + 3: right");
        check (d.tape().numTakes() == 2, "two takes");

        se::TapeSource daw; daw.kind = se::AudioSourceKind::daw;
        d.setSource (daw); d.record();
        tapeBlock (e, d, { 0.5f, -0.5f }, l, r);
        d.stop(); tapeSettle (e, d, {});
        checkClose (tapeAt (e, (size_t) (4 * kBlock), true), -0.5f, 1e-6f, "DAW: the host's input pair");
    }
    {
        // One Sound: post-fader, deaf to mute, the other Sounds left out.
        se::Engine e; e.prepare (kRate, kBlock); e.setDeclickMs (0.0);
        const uint64_t a = e.addWindow (dcDocument (20, "a"));
        const uint64_t b = e.addWindow (dcDocument (40, "b"));
        se::PlayOptions loopAll; loopAll.loop = true;
        e.play (a, loopAll); e.play (b, loopAll);
        e.windowById (a)->muted = true;
        renderWithInput (e, {}, l, r);
        e.windowById (a)->muted = false;
        e.windowById (b)->muted = true;
        renderWithInput (e, {}, l, r);
        const float aAlone = l[0];
        e.windowById (b)->muted = false;
        e.windowById (a)->gain = 0.5f;
        e.windowById (a)->muted = true;
        se::TapeDeck d (e);
        se::TapeSource s; s.tapWindow = a;
        d.setSource (s); d.record();
        tapeBlock (e, d, {}, l, r);
        d.stop(); tapeSettle (e, d, {});
        checkClose (tapeAt (e, 0), aAlone * 0.5f, 1e-6f, "Sound a: post-fader, heard though muted, b left out");
    }
    {
        // Record-over through the deck: the replaced span holds the new audio, the rest the old.
        se::Engine e; e.prepare (kRate, kBlock); e.setDeclickMs (0.0);
        se::TapeDeck d (e);
        se::TapeSource ext; ext.kind = se::AudioSourceKind::external; ext.stereo = false;
        d.setSource (ext);
        d.record();
        for (int i = 0; i < 10; ++i) tapeBlock (e, d, { 0.25f }, l, r);
        d.stop(); tapeSettle (e, d, { 0.25f });
        d.seek ((size_t) (2 * kBlock));
        d.record();
        for (int i = 0; i < 3; ++i) tapeBlock (e, d, { -0.5f }, l, r);
        d.stop(); tapeSettle (e, d, { -0.5f });
        check (d.tape().length() == (size_t) (10 * kBlock), "record-over keeps the tape's length");
        check (tapeAt (e, (size_t) (2 * kBlock) - 1) == 0.25f && tapeAt (e, (size_t) (2 * kBlock)) == -0.5f
               && tapeAt (e, (size_t) (5 * kBlock) - 1) == -0.5f && tapeAt (e, (size_t) (5 * kBlock)) == 0.25f,
               "record-over replaces exactly the span recorded");
        check (starts (d.tape()) == std::vector<size_t> { 0, (size_t) (2 * kBlock), (size_t) (5 * kBlock) }, "takes: 0, the new one, the old tail");

        // Next / Previous / Play from the deck, and Record while playing punches in.
        d.seek (0);
        check (d.nextTake() && d.tape().position() == (size_t) (2 * kBlock), "Next");
        check (d.play(), "Play from take 2");
        tapeBlock (e, d, {}, l, r);
        checkClose (l[0], -0.5f, 1e-6f, "plays take 2");
        check (d.nextTake() && e.tapeAckGen() != 0, "Next while playing");
        tapeBlock (e, d, {}, l, r);
        checkClose (l[0], 0.25f, 1e-6f, "plays on from take 3");
        check (d.previousTake(), "Previous while playing");
        tapeBlock (e, d, {}, l, r);
        checkClose (l[0], -0.5f, 1e-6f, "back at the start of take 2 (inside the grace of take 3)");
        tapeBlock (e, d, {}, l, r);
        const size_t here = d.tape().position();
        check (d.record() && d.tape().mode() == se::Tape::Mode::recording, "Record while playing records there");
        check (d.tape().takes()[(size_t) d.tape().recordingTake()].start == here, "...from where the tape was");
        tapeBlock (e, d, { 0.75f }, l, r);
        check (l[0] == 0.0f && ! e.tapePlaying(), "playback stopped: nothing plays while recording");
        d.stop(); tapeSettle (e, d, {});
        check (tapeAt (e, here) == 0.75f, "the punch-in landed where playback was");

        // Clear while recording throws the take away.
        d.record();
        tapeBlock (e, d, { 0.1f }, l, r);
        d.clear();
        check (! d.record(), "Record refused while a cleared take is still settling");
        tapeSettle (e, d, { 0.1f });
        check (d.tape().empty() && e.tapeStore().length() == 0 && e.tapeStore().reservedFrames() == 0, "clear mid-take: blank tape");
        check (d.record(), "Record again after the clear");
        tapeBlock (e, d, { 0.1f }, l, r);
        d.stop(); tapeSettle (e, d, {});
        check (d.tape().length() == (size_t) kBlock && tapeAt (e, 0) == 0.1f, "...a fresh take, none of the old one");
    }
    {
        // The store across chunk boundaries.
        se::TapeStore s;
        const size_t N = se::TapeStore::kChunkFrames + 100;
        std::vector<float> a (N), b (N);
        for (size_t i = 0; i < N; ++i) { a[i] = (float) i; b[i] = -(float) i; }
        check (s.reserve (N) && s.write (0, a.data(), b.data(), N) == N, "store: a write across two chunks");
        check (s.length() == 0, "store: nothing readable before the length is published");
        s.setLength (N);
        std::vector<float> x (200), y (200);
        const size_t at = se::TapeStore::kChunkFrames - 100;
        check (s.read (at, x.data(), y.data(), 200) == 200 && x[0] == (float) at && x[199] == (float) (at + 199) && y[150] == -(float) (at + 150),
               "store: a read across the chunk boundary");
        check (s.read (N - 10, x.data(), y.data(), 200) == 10, "store: reads stop at the length");
        check (! s.reserve (se::TapeStore::kMaxFrames + 1), "store: refuses past its maximum");
    }
}

static void tapeSendTests()
{
    std::printf ("tape send\n");
    se::TapeStore s;
    const size_t N = 1000;
    std::vector<float> a (N), b (N);
    for (size_t i = 0; i < N; ++i) { a[i] = 0.5f; b[i] = -0.25f; }
    s.reserve (N); s.write (0, a.data(), b.data(), N); s.setLength (N);
    a.assign (N, 0.0f); s.write (600, a.data(), b.data(), 400);   // frames 600.. silent on the left

    auto d = se::soundFromTape (s, 100, 700, 48000.0, "Take 2");
    check (d != nullptr && d->name == "Take 2" && d->numTracks() == 2 && d->bps == 16 && d->rateHz > 47999.0 && d->rateHz < 48001.0,
           "send: a 16-bit two-track Sound at the tape's rate, named");
    check (d->tracks[0].outLeft && ! d->tracks[0].outRight && d->tracks[1].outRight && ! d->tracks[1].outLeft,
           "send: track 1 routed left, track 2 right");
    check (d->tracks[0].data.size() == 600 * 2 && d->tracks[1].data.size() == 600 * 2, "send: exactly the span's frames");
    const auto at = [&] (int t, size_t f) { return se::getSample (d->tracks[(size_t) t].data.data(), f * 2, 16); };
    check (at (0, 0) == 16384 && at (1, 0) == -8192 && at (0, 499) == 16384 && at (0, 500) == 0,
           "send: the samples, from the span's start (Raw: exact)");
    check (se::soundFromTape (s, 500, 500, 48000.0, "x") == nullptr && se::soundFromTape (s, 2000, 3000, 48000.0, "x") == nullptr,
           "send: an empty span gives nothing");
    auto whole = se::soundFromTape (s, 0, 5000, 48000.0, "Tape", se::ConvertMode::clean);
    check (whole != nullptr && whole->tracks[0].data.size() == N * 2, "send: clamped to the tape; Clean keeps the length");
    const int v = se::getSample (whole->tracks[0].data.data(), 300 * 2, 16);
    check (std::abs (v - 16384) <= 2, "send: Clean only dithers at the same rate");
}

// Scrub: dragging the strip is heard, varispeed, and silent at rest, with no click.
static void tapeScrubTests()
{
    std::printf ("tape scrub\n");
    se::Engine e; e.prepare (kRate, kBlock); e.setDeclickMs (0.0);
    se::TapeDeck d (e);
    se::TapeSource ext; ext.kind = se::AudioSourceKind::external; ext.stereo = false;
    d.setSource (ext);
    std::vector<float> l, r;
    d.record();
    for (int i = 0; i < 200; ++i) tapeBlock (e, d, { 0.5f }, l, r);
    d.stop(); tapeSettle (e, d, {});
    const size_t len = d.tape().length();
    d.tape().seek (0);   // not playing: a plain move

    check (d.scrubBegin() && d.scrubbing(), "scrub begins on a stopped tape");
    renderWithInput (e, {}, l, r);
    check (e.tapePlaying(), "the audio thread reads the tape while scrubbing");
    float peak = 0.0f, maxStep = 0.0f, prev = 0.0f;
    // Drag forward 2000 frames over ~20 blocks, as a mouse would.
    for (int i = 1; i <= 20; ++i)
    {
        d.scrubTo ((size_t) (i * 100));
        tapeBlock (e, d, {}, l, r);
        for (float v : l) { peak = std::max (peak, std::abs (v)); maxStep = std::max (maxStep, std::abs (v - prev)); prev = v; }
    }
    check (peak > 0.25f, "dragging is heard");
    check (d.tape().position() == 2000, "the tape's position is where the drag is");
    // Held still: the tape comes to rest and goes silent, smoothly.
    float tail = 1.0f;
    for (int i = 0; i < 100; ++i)
    {
        tapeBlock (e, d, {}, l, r);
        tail = 0.0f;
        for (float v : l) { tail = std::max (tail, std::abs (v)); maxStep = std::max (maxStep, std::abs (v - prev)); prev = v; }
    }
    check (tail < 1.0e-3f, "held still: silent");
    check (maxStep < 0.02f, "no click: every sample step is small");
    check (e.tapePlaying(), "still held: the tape is still under the hand");

    d.scrubEnd();
    for (int i = 0; i < 20; ++i) tapeBlock (e, d, {}, l, r);
    check (! d.scrubbing() && ! e.tapePlaying() && d.tape().mode() == se::Tape::Mode::stopped, "let go: the tape stops reading, stopped");

    // A playing tape stops while held and plays on from where it is let go.
    d.tape().seek (0);
    d.play();
    tapeBlock (e, d, {}, l, r);
    check (d.scrubBegin() && d.tape().mode() == se::Tape::Mode::stopped, "scrub while playing: playback stops");
    d.scrubTo (len / 2);
    for (int i = 0; i < 40; ++i) tapeBlock (e, d, {}, l, r);
    d.scrubEnd();
    check (d.tape().mode() == se::Tape::Mode::playing && d.tape().position() == len / 2, "let go: plays on from there");
    tapeBlock (e, d, {}, l, r);
    checkClose (l[(size_t) kBlock - 1], 0.5f, 1.0e-3f, "playback heard again");

    // Clear while scrubbing: the memory goes once the scrub has faded out.
    d.stop();
    tapeBlock (e, d, {}, l, r);
    d.scrubBegin();
    d.scrubTo (100);
    tapeBlock (e, d, {}, l, r);
    d.clear();
    for (int i = 0; i < 20; ++i) tapeBlock (e, d, {}, l, r);
    check (e.tapeStore().reservedFrames() == 0 && ! e.tapePlaying(), "clear mid-scrub: released after the fade");
    check (! d.scrubBegin(), "a blank tape cannot be scrubbed");
}

// The tape under a real audio thread: the deck records, plays, jumps and clears
// while render() runs. For TSan.
static void tapeConcurrencyTests()
{
    se::Engine e; e.prepare (kRate, kBlock); e.setDeclickMs (0.0);
    const uint64_t a = e.addWindow (dcDocument (30, "a"));
    se::PlayOptions loopAll; loopAll.loop = true;
    e.play (a, loopAll);
    se::TapeDeck d (e);
    std::atomic<bool> done { false }, sane { true };
    std::thread audio ([&]
    {
        std::vector<float> in0 ((size_t) kBlock, 0.25f), l ((size_t) kBlock), r ((size_t) kBlock);
        const float* ins[1] = { in0.data() };
        while (! done.load())
        {
            e.render (l.data(), r.data(), kBlock, ins, 1);
            for (int i = 0; i < kBlock; ++i)
                if (! std::isfinite (l[(size_t) i]) || std::fabs (l[(size_t) i]) > 1.0f) sane = false;
        }
    });
    for (int i = 0; i < 4000; ++i)
    {
        switch (i % 9)
        {
            case 0: d.record(); break;
            case 2: d.stop(); break;
            case 3: d.play(); break;
            case 4: d.previousTake(); break;
            case 5: d.nextTake(); break;
            case 6: d.seek ((size_t) (i * 7)); break;
            case 7: if (i % 90 == 7) d.clear(); else if (i % 3 == 0) { d.scrubBegin(); d.scrubTo ((size_t) i); } else d.scrubEnd(); break;
            case 8: { se::TapeSource s; s.tapWindow = (i % 2) ? a : 0; if (i % 3 == 0) s.kind = se::AudioSourceKind::external; d.setSource (s); } break;
            default: break;
        }
        d.service();
        if (i % 50 == 0) std::this_thread::sleep_for (std::chrono::microseconds (300));
    }
    d.stop();
    for (int i = 0; i < 200 && d.finishing(); ++i) { std::this_thread::sleep_for (std::chrono::microseconds (200)); d.service(); }
    done = true;
    audio.join();
    d.service();
    check (sane.load(), "tape stress: output finite and in range");
    check (! d.finishing() && d.tape().mode() != se::Tape::Mode::recording, "tape stress: the deck settles");
    check (d.tape().length() == e.tapeStore().length(), "tape stress: the model and the store agree on the length");
}

int main()
{
    se::Engine engine;
    engine.prepare (kRate, kBlock); engine.setDeclickMs (0.0);

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

    // --- the S box: plain click, and the ⌘ solo swap (0.6.0 step 1.3) --------
    engine.clickSolo (a, false);
    engine.clickSolo (b, false);
    checkClose (renderLevel (engine), la + lb, tol, "plain clicks add solos");
    engine.clickSolo (c, true);
    check (! wa->soloed && ! wb->soloed && wc->soloed, "cmd on an unlit S: only that one soloed");
    checkClose (renderLevel (engine), lc, tol, "solo swap: only c sounds");
    check (wb->muted == false, "solo swap: mutes untouched");
    engine.clickSolo (a, false);
    engine.clickSolo (a, true);
    check (! engine.anyWindowSoloed(), "cmd on a lit S: every solo cleared");
    engine.clickSolo (b, true);
    check (! wa->soloed && wb->soloed && ! wc->soloed, "cmd with nothing soloed: solos that one");
    engine.clickSolo (b, false);
    check (! engine.anyWindowSoloed(), "plain click on a lit S: off");
    engine.clickSolo (987654, true);
    check (! engine.anyWindowSoloed(), "unknown id: nothing happens");
    checkClose (renderLevel (engine), la + lb + lc, tol, "after the clicks: all three back");

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

    scrollTargetTests();
    midiNoteTests();
    keyboardVoiceTests();
    transportButtonsTests();
    masterLevelTests();
    mainMuteTests();
    emptyCanvasTests();
    directionTests();
    loopOnPinTests();
    playOnceSelectionTests();
    playFromCaretTests();
    unsetLoopTests();
    selectionInfoTests();
    directionEngineTests();
    switchingTests();
    concurrencyTests();
    deselectSyncTests();
    deselectConcurrencyTests();
    captureTests();
    convertTests();
    recordTests();
    takeJoinTests();
    captureConcurrencyTests();
    recordConcurrencyTests();
    metaTests();
    tapeModelTests();
    tapeEngineTests();
    tapeConcurrencyTests();
    tapeSendTests();
    tapeScrubTests();
    failures += declickTests();

    std::printf ("\n%s\n", failures == 0 ? "engine: all checks passed"
                                         : (std::to_string (failures) + " FAILED").c_str());
    return failures == 0 ? 0 : 1;
}
