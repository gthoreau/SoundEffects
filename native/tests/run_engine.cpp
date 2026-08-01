// Engine tests — the desktop-wide mixer: per-Window gain, mute and the CUMULATIVE
// solo filter, plus the rule that none of them may stop a transport.
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
