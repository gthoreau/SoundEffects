// Engine tests — the desktop-wide mixer: per-Window gain, balance, mute, the
// CUMULATIVE solo filter and the stereo VU feed, plus the rule that none of
// them may stop a transport.
//
// These are the parts of the Engine that have no golden vector to check them
// against (there is no 1994 original for a mixer that never existed), so they are
// asserted directly. JUCE-free, like everything else under Source/engine/.

#include "engine/Engine.h"
#include "engine/SampleFormat.h"

#include <cmath>
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

    std::printf ("\n%s\n", failures == 0 ? "engine: all checks passed"
                                         : (std::to_string (failures) + " FAILED").c_str());
    return failures == 0 ? 0 : 1;
}
