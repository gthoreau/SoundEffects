// Every ported module's factory, in one place.
//
// A module is a VALUE, not a type — `EffectModule` is a struct of metadata and
// std::functions, so each .cpp exposes one factory that builds it. This header is
// what lets a module's own translation unit see its declaration (no
// -Wmissing-prototypes) and what Registry.cpp includes to assemble the list.
//
// Adding a module means three lines: the factory here, the push_back in
// Registry.cpp, and the source file in the two CMakeLists.

#pragma once

#include "../Module.h"

namespace se::modules {

// --- Wave A: pointwise and simple -------------------------------------------
EffectModule makeAmplify();          // Amplify.cpp  [SOURCE: Gain.c]

// Simple.cpp — the published one-liners, all [DOC]
EffectModule makeInvert();
EffectModule makeReverse();
EffectModule makeSilence();
EffectModule makeDrawLine();
EffectModule makeFadeIn();
EffectModule makeFadeOut();
EffectModule makeNormalize();

// Recovered by disassembly — the ones where the unwritten detail is the sound
EffectModule makeSmooth();           // Smooth.cpp   2-point, centre EXCLUDED
EffectModule makeDither();           // Dither.cpp   Toolbox RNG, adds
EffectModule makeNoise();            // Noise.cpp    Toolbox RNG, replaces, 16-bit stores
EffectModule makeRobotize();         // Robotize.cpp recursive comb at ~117 Hz

// Fir.cpp — [DOC]; "Bandpass"/"Bandstop" are misnomers, both are recursive
EffectModule makeLowPassFIR();
EffectModule makeHighPassFIR();
EffectModule makeBandpassFIR();
EffectModule makeBandstopFIR();

// Iir.cpp — [BINARY]; 0.1 coefficient quantisation, per-term truncation
EffectModule makeHighPassIIR();
EffectModule makeLowPassIIR();
EffectModule makeShelving();

// --- Wave B: the length-changing ones ----------------------------------------
// These grow their own track's buffer, which is why runSkeleton recomputes each
// pass immediately before its body instead of measuring them all up front.

// EchoDelay.cpp — one kernel, two sweep directions
EffectModule makeEcho();
EffectModule makeDelay();

EffectModule makeReplicate();        // Replicate.cpp   copies + gaps, no skeleton
EffectModule makeWeirdTempo();       // WeirdTempo.cpp  period doubler, ignores doExtend
EffectModule makeZeroCrosser();      // ZeroCrosser.cpp moves the SELECTION, writes nothing

} // namespace se::modules
