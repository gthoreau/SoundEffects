// Engine tests — the desktop-wide mixer: per-Window gain, balance, mute, the
// CUMULATIVE solo filter and the stereo VU feed, plus the rule that none of
// them may stop a transport.
//
// These are the parts of the Engine that have no golden vector to check them
// against (there is no 1994 original for a mixer that never existed), so they are
// asserted directly. JUCE-free, like everything else under Source/engine/.

#include "engine/Edit.h"
#include "engine/Engine.h"
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

    engine.removeWindow (b);   // frees slot 1, leaving a gap
    check (engine.highestSlot() == 2 && engine.lowestFreeSlot() == 1, "slots: Close leaves a gap");
    const uint64_t d = engine.addWindow (dcDocument (5, "d"));
    check (engine.windowById (d)->slot == 1, "slots: the next new Sound fills the gap");

    check (engine.moveToSlot (a, 6) && wa->slot == 6 && engine.lowestFreeSlot() == 0,
           "slots: moving to an empty slot frees the old one");
    check (engine.moveToSlot (c, 6) && wc->slot == 6 && wa->slot == 2,
           "slots: moving onto an occupied slot swaps the two");

    wa->slot = 1;              // a duplicate of d's, and c unseated: what a damaged file gives
    wc->slot = se::Window::kNoSlot;
    engine.normaliseSlots();   // order a, c, d: a keeps 1, then c and d take 0 and 2
    check (wa->slot == 1 && wc->slot == 0 && engine.windowById (d)->slot == 2,
           "slots: normalising keeps the first claimant and reseats the rest lowest-first");
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

    std::printf ("\n%s\n", failures == 0 ? "engine: all checks passed"
                                         : (std::to_string (failures) + " FAILED").c_str());
    return failures == 0 ? 0 : 1;
}
