// FixtureFile — reader for the golden vectors in tests/fixtures/effects/*.fx,
// written by `npm run fixtures` (tools/gen-effect-fixtures.ts).
//
// The TypeScript effects in the repo root are the byte-accurate reference (§8).
// Each fixture carries, per case: the input document, the settings, and the exact
// document the reference produced. A native port is correct when it reproduces
// that byte for byte — not when it "sounds right". The quirks that make these
// effects what they are (truncating integer divides, coefficients quantised to
// one decimal, Smooth's centre-excluding average) do not survive a port checked
// by ear.
//
// No JUCE here, and no dependency on the engine's threading: this reads plain
// SoundDocuments so the DSP can be tested without a build of the plugin.

#pragma once

#include "effects/Settings.h"
#include "engine/SoundDocument.h"

#include <cstdint>
#include <string>
#include <vector>

namespace se::fx {

/** Container version this reader understands. Must match FIXTURE_VERSION in
    tools/gen-effect-fixtures.ts — a silently-skewed layout is worse than a
    refusal, so the loader throws rather than guessing. */
inline constexpr uint32_t kFixtureVersion = 1;

// A fixture's settings ARE the runtime's settings — `se::Settings`, the flat
// string-keyed map with dotted paths for nested groups ("rate.initialValue").
// Deliberately one type rather than two that happen to agree: a fixture whose
// settings could not be handed straight to a module would be testing a translation
// layer instead of the module.

/** The `doExtend` preference, mirroring effects/Host.h. Re-declared as the
    on-disk encoding, which is fixed by the container and must not drift with the
    runtime enum. */
enum class ExtendPref : uint8_t { Never = 0, Always = 1, Ask = 2 };

struct Case
{
    std::string name;
    uint32_t    randSeed = 1;
    ExtendPref  doExtend = ExtendPref::Ask;
    /** What AskDoExtend() answered on the reference run. The port must be asked
        the same question and given the same answer, or the buffer-grow paths
        diverge for reasons that have nothing to do with the DSP. */
    bool        extendAnswer = false;

    Settings     params;
    SoundDocument input;

    /** The module's OSErr. kModNoError / kModCancel are both normal outcomes:
        Gain.c starts at "didn't do anything" and promotes. */
    int32_t       result = 0;
    SoundDocument expected;

    /** Documents the module spawned via ModNewDocument — Quick Mix and Difference
        render to a "New Document", which the host is expected to open as a new
        Window. `effect()` returns an OSErr, so this list is the only hand-back. */
    std::vector<SoundDocument> spawned;
};

struct FixtureFile
{
    std::string moduleName;   // the menu name, e.g. "Fade In"
    std::string signature;    // the Pascal-string settings key, e.g. "SampleEffect"
    std::vector<Case> cases;
};

/** Load one .fx file. Throws std::runtime_error on a bad magic, an unknown
    version, or a truncated file. */
FixtureFile load (const std::string& path);

/** Every .fx in a directory, sorted by filename for a stable report order. */
std::vector<std::string> listFixtures (const std::string& dir);

// --- comparison -------------------------------------------------------------

/** A human-readable account of the first way two documents differ, or empty if
    they are identical. Byte-level: reports the track and offset, because "the
    audio differs" is not a debuggable statement. */
std::string diff (const SoundDocument& expected, const SoundDocument& actual);

} // namespace se::fx
