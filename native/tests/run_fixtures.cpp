// The golden-vector runner for the native effect ports.
//
// Every case in tests/fixtures/effects/*.fx pairs an input document and settings
// with the exact output the TypeScript reference produced. A port is correct when
// it reproduces those bytes; anything else is a regression report with a track
// number and a byte offset.
//
// Modules land in the registry as they are ported (the waves in the effects plan).
// A fixture with no registered module is reported as PENDING, never as a pass —
// a green run that silently skipped 30 modules is the failure mode this whole
// harness exists to prevent.
//
//   cmake -S native/tests -B native/tests/build -G Ninja
//   cmake --build native/tests/build
//   ./native/tests/build/effect-fixtures

#include "FixtureFile.h"

#include "effects/Host.h"
#include "effects/Module.h"
#include "effects/Registry.h"
#include "engine/Edit.h"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

struct Tally
{
    int passed = 0;
    int failed = 0;
    int pending = 0;
    int modulesPorted = 0;
    int modulesPending = 0;

    // Reader self-check counters — see the note in the case loop.
    int casesLoaded = 0;
    int casesWithAudio = 0;
    int casesThatChange = 0;
};

/**
 * Run one case against the ported module of that name, if there is one.
 *
 * The host is rebuilt per case and given exactly what the reference run was given:
 * the same RNG seed, the same doExtend pref, and the same answer to AskDoExtend().
 * Without that the buffer-grow paths diverge for reasons that have nothing to do
 * with the DSP, and the mismatch report blames the wrong thing.
 */
bool runPortedModule (const std::string& moduleName,
                      const se::fx::Case& testCase,
                      se::SoundDocument& doc,
                      std::vector<se::SoundDocument>& spawned,
                      se::ModResult& result)
{
    const se::EffectModule* module = se::findEffect (moduleName);
    if (module == nullptr) return false;

    se::HostOptions opts;
    opts.randSeed = testCase.randSeed;
    opts.askDoExtend = [answer = testCase.extendAnswer] { return answer; };
    // Every sample, so a module that polls timeToCallProgress takes its
    // cancel-check path exactly as often as it did on the reference run.
    opts.progressInterval = 1;

    se::Host host (std::move (opts));
    host.doExtend = static_cast<se::DoExtend> (testCase.doExtend);

    result = se::runEffect (*module, doc, host, testCase.params);

    for (const auto& d : host.spawnedDocuments)
        spawned.push_back (*d);

    return true;
}

/**
 * The Edit and Selection menus' commands (signature "Edit"): not modules, but the
 * same contract, replayed against engine/Edit. False when the name is none of them.
 */
bool runEditCommand (const std::string& name, const se::fx::Case& testCase,
                     se::SoundDocument& doc, se::ModResult& result)
{
    const auto bytes = static_cast<size_t> (se::getInt (testCase.params, "bytes"));
    if (name == "Leave Silence")
    {
        se::leaveSilence (doc, bytes, se::getBool (testCase.params, "sound"));
        result = 0;
        return true;
    }
    if (name == "Select Exactly")
    {
        se::selectExactly (doc, bytes, static_cast<se::ExactlyAnchor> (se::getInt (testCase.params, "anchor")));
        result = 0;
        return true;
    }
    if (name == "Select Peak")
    {
        se::selectPeak (doc, static_cast<size_t> (se::getInt (testCase.params, "margin")), se::getBool (testCase.params, "sound"));
        result = 0;
        return true;
    }
    if (name == "Select Silence")
    {
        result = se::selectSilence (doc) ? 1 : 0;
        return true;
    }
    if (name == "Shift Sound")
    {
        result = se::shiftSound (doc, bytes, se::getBool (testCase.params, "right")) ? 1 : 0;
        return true;
    }
    return false;
}

std::string pad (std::string s, size_t width)
{
    if (s.size() < width) s.resize (width, ' ');
    return s;
}

} // namespace

int main (int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : "fixtures/effects";

    std::vector<std::string> files;
    try
    {
        files = se::fx::listFixtures (dir);
    }
    catch (const std::exception& e)
    {
        std::cerr << "error: " << e.what() << "\n";
        return 2;
    }

    if (files.empty())
    {
        std::cerr << "error: no .fx fixtures in " << dir << " — run `npm run fixtures`\n";
        return 2;
    }

    Tally tally;
    std::vector<std::string> failures;
    std::vector<std::string> pendingModules;

    for (const auto& path : files)
    {
        se::fx::FixtureFile file;
        try
        {
            file = se::fx::load (path);
        }
        catch (const std::exception& e)
        {
            std::cerr << "FAIL  " << e.what() << "\n";
            tally.failed++;
            failures.push_back (e.what());
            continue;
        }

        int passed = 0, failed = 0, pending = 0;

        for (const auto& testCase : file.cases)
        {
            // Reader self-check, run on every case whether or not a port exists.
            // A reader that quietly produced empty documents would make the whole
            // suite pass the day the first module lands, so prove here that the
            // fixture holds real, differing audio before trusting any comparison
            // made through it.
            tally.casesLoaded++;
            if (! testCase.input.tracks.empty() && ! testCase.input.tracks[0].data.empty())
                tally.casesWithAudio++;
            if (! se::fx::diff (testCase.input, testCase.expected).empty()
                || ! testCase.spawned.empty())
                tally.casesThatChange++;

            // The port mutates its own copy: the fixture's input is the shared
            // starting point for every run and must survive intact.
            se::SoundDocument doc = testCase.input;
            std::vector<se::SoundDocument> spawned;
            se::ModResult result = se::kModNoError;

            if (! runPortedModule (file.moduleName, testCase, doc, spawned, result)
                && ! runEditCommand (file.moduleName, testCase, doc, result))
            {
                ++pending;
                continue;
            }

            std::string why = se::fx::diff (testCase.expected, doc);

            // The OSErr is part of the contract, not commentary: its SIGN tells the
            // host whether the buffer was touched (§7.7), and kModCancel is how a
            // module says "I declined" — Replicate with extension refused, Zero
            // Crosser finding no crossing. A port that mangles nothing but reports
            // the wrong outcome produces identical bytes and a lying UI, so the
            // document diff alone cannot catch it.
            if (why.empty() && result != testCase.result)
            {
                why = "returned " + std::to_string (result) + ", expected "
                    + std::to_string (testCase.result);
            }

            if (why.empty() && spawned.size() != testCase.spawned.size())
            {
                why = "spawned " + std::to_string (spawned.size()) + " documents, expected "
                    + std::to_string (testCase.spawned.size());
            }
            for (size_t i = 0; why.empty() && i < spawned.size(); ++i)
            {
                const std::string d = se::fx::diff (testCase.spawned[i], spawned[i]);
                if (! d.empty()) why = "spawned document " + std::to_string (i + 1) + ": " + d;
            }

            if (why.empty())
            {
                ++passed;
            }
            else
            {
                ++failed;
                failures.push_back (file.moduleName + " [" + testCase.name + "] " + why);
            }
        }

        tally.passed  += passed;
        tally.failed  += failed;
        tally.pending += pending;

        if (pending == static_cast<int> (file.cases.size()))
        {
            tally.modulesPending++;
            pendingModules.push_back (file.moduleName);
        }
        else
        {
            tally.modulesPorted++;
            std::cout << (failed == 0 ? "ok    " : "FAIL  ")
                      << pad (file.moduleName, 22)
                      << passed << "/" << (passed + failed) << " cases\n";
        }
    }

    if (! failures.empty())
    {
        std::cout << "\nFailures:\n";
        for (const auto& f : failures) std::cout << "  " << f << "\n";
    }

    if (! pendingModules.empty())
    {
        std::cout << "\nNot ported yet (" << pendingModules.size() << "):\n  ";
        for (size_t i = 0; i < pendingModules.size(); ++i)
            std::cout << pendingModules[i] << (i + 1 < pendingModules.size() ? ", " : "\n");
    }

    std::cout << "\n" << tally.passed << " passed, " << tally.failed << " failed, "
              << tally.pending << " pending"
              << "   (" << tally.modulesPorted << " modules ported, "
              << tally.modulesPending << " to go)\n";

    std::cout << "fixtures: " << tally.casesLoaded << " cases, "
              << tally.casesWithAudio << " carry audio, "
              << tally.casesThatChange << " change the document\n";

    // A fixture set where nothing carries audio, or nothing changes, means the
    // reader is broken — and it would report a clean sweep the moment a module is
    // registered. Fail now instead.
    if (tally.casesWithAudio == 0 || tally.casesThatChange == 0)
    {
        std::cerr << "error: fixtures loaded but carry no usable data — the reader "
                     "and the generator's layout disagree\n";
        return 2;
    }

    // Pending is not failure — it is the plan. Only a mismatch fails the build.
    return tally.failed == 0 ? 0 : 1;
}
