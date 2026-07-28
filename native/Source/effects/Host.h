// The host services a module sees — the `GlueRec` surface. SPEC §7.6,
// [SOURCE: Glue.h]. Ported from the TS reference `src/runtime/host.ts`.
//
// A module reaches everything outside its own buffer through this object: progress,
// settings persistence, selection queries, document restructuring, the two RNGs,
// unit conversion. Keeping it as one explicit surface is what lets the same effect
// code run under the JUCE app, under a test harness, and (§12b) on firmware.
//
// No JUCE here. Nothing on this path may allocate on the audio thread — effects
// run on the message thread (or a worker), never in render().

#pragma once

#include "Results.h"
#include "Rng.h"
#include "Settings.h"
#include "Units.h"
#include "engine/SoundDocument.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace se {

/**
 * `MachineRec` — pre-resolved Gestalt flags, "so the modules don't have to call
 * Gestalt all the time". Kept because modules branch on it (FPU builds
 * especially), and because it is how a firmware port says "no FPU, take the
 * integer path".
 */
struct MachineInfo
{
    int  machineRecVers = 1;
    bool hasFPU = true;
    bool hasColorQD = true;
    bool has32BitQD = true;
    bool hasQuickTime = true;
    bool hasSoundManager3 = true;
    bool hasSoundInputDevice = true;
    bool hasStereoOutput = true;
    bool hasStereoInput = true;
    bool mixesStereoOutputToMono = false;
    bool hasPlayAndRecord = true;
    bool has16BitSoundIO = true;
    bool hasMultipleChannels = true;
};

/** The `doExtend` policy. [BINARY: Echo-Delay.md] confirms 2 = "Ask Each Time"
    triggers a live AskDoExtend() call; anything else is used directly. */
enum class DoExtend : int { Never = 0, Always = 1, Ask = 2 };

struct HostOptions
{
    uint32_t randSeed = 1;
    /** Answers AskDoExtend() when the pref is Ask. Absent means "don't extend":
        silently growing someone's buffer is the rude option. */
    std::function<bool()> askDoExtend;
    /** Called on every showProgress; return true to cancel (heeded only if the
        module passed canCancel). */
    std::function<bool (size_t cur, size_t total, const std::string& text)> onProgress;
    /** How often the host raises timeToCallProgress. 1 = every sample. */
    int progressInterval = 1;
};

class Host
{
public:
    explicit Host (HostOptions opts = {});

    MachineInfo machine;
    ToolboxRandom random;   // the Ricci modules' RNG; Norris modules make their own

    /**
     * `timeToCallProgress` — the host tells the module when to ACTUALLY call
     * showProgress. The module polls this every sample; the host raises it on its
     * own schedule. It exists so the progress bar's responsiveness is a host
     * preference (§7.6) rather than each module's guess.
     */
    bool timeToCallProgress = false;

    /** The pref value. Ask makes askDoExtend() fire per run. */
    DoExtend doExtend = DoExtend::Ask;

    // --- progress -----------------------------------------------------------

    void setupProgress (const std::string& text);

    /**
     * A nonzero return means the user cancelled — AND THE HALF-PROCESSED AUDIO
     * STAYS (§1.2). The module breaks out of its loop and returns; nothing
     * rewinds. Most modules hard-code canCancel = false (Amplify, Echo, Delay,
     * Noise, Smooth, Robotize, Downsample Bits), which is why a long Echo is
     * genuinely uninterruptible. That is not an oversight to fix.
     */
    ModResult showProgress (size_t cur, size_t total, bool canCancel);

    void closeProgress();
    bool isProgressOpen() const noexcept { return progressOpen; }

    // --- settings persistence ------------------------------------------------

    /** Keyed by the module's signature (Gain's is "SampleEffect"), which is why
        two modules can deliberately share a settings blob. */
    const Settings* loadEffectSettings (const std::string& signature) const;
    void saveEffectSettings (const std::string& signature, Settings s);

    // --- environment ---------------------------------------------------------

    const MachineInfo& getMachineInfo() const noexcept { return machine; }

    /** AskDoExtend() — only consulted when the pref is Ask; the module checks
        first. "Ask each time" is one of the app's non-negotiables (§12). */
    bool askDoExtend();

    void initRandSeed (uint32_t seed) { random.initSeed (seed); }

    // --- unit conversion ------------------------------------------------------

    double convertToUnits (const SoundDocument& doc, size_t bytes, Unit unit) const
    {
        return bytesToUnit (bytes, unit, doc.rateHz, doc.bps);
    }

    size_t convertToBytes (const SoundDocument& doc, double value, Unit unit) const
    {
        return unitToBytes (value, unit, doc.rateHz, doc.bps);
    }

    // --- selection queries ----------------------------------------------------

    /** GetBytesToProcess(modInfo) — total bytes across the selected tracks. */
    size_t getBytesToProcess (const SoundDocument& doc) const;

    /** ModMaxChSize / ModMaxRelChSize. */
    size_t maxTrackSize (const SoundDocument& doc) const { return doc.maxTrackSize(); }
    size_t maxSelectedTrackSize (const SoundDocument& doc) const { return doc.maxSelectedTrackSize(); }

    struct Peak { int64_t peak = 0; size_t pos = 0; };

    /**
     * ModGetPeak(modData, &peak, &pos).
     *
     * NOT Select Peak's metric: Select Peak sums ACROSS tracks, whereas this
     * reports the largest single |sample| and where it is. Flanger's envelope
     * follower uses it over 1024-sample windows.
     */
    Peak getPeak (const SoundDocument& doc) const;

    struct Limits { int64_t min = 0; int64_t max = 0; };

    /** ModGetSampleValueLimits(bps, &min, &max) — the range every effect clamps to. */
    static Limits getSampleValueLimits (int bps) noexcept;

    // --- document restructuring -----------------------------------------------

    ModResult addNewTrack (SoundDocument& doc, int where = -1);
    ModResult delTrack (SoundDocument& doc, int track);

    bool getTrackPan (const SoundDocument& doc, int track, bool leftOne) const;
    void setTrackPan (SoundDocument& doc, int track, bool leftPan, bool rightPan);

    /** ModSilenceOut(modData, chSt, chEnd, st, end) — 1-based tracks. */
    ModResult silenceOut (SoundDocument& doc, int trackSt, int trackEnd, size_t st, size_t end);

    ModResult resampleSoundBits (SoundDocument& doc, int newBps);
    ModResult resampleSoundRate (SoundDocument& doc, double newRateHz);

    /**
     * Documents spawned during this run, in creation order.
     *
     * `effect()` returns an OSErr, not a document, so a module that renders to a
     * "New Document" has no way to hand it back — in the original it calls
     * ModNewDocument(&modData) and the HOST owns the resulting window. This list
     * is that ownership: the host opens whatever ends up here, as a new Window on
     * the canvas. Quick Mix and Difference are the modules that use it.
     */
    std::vector<std::unique_ptr<SoundDocument>> spawnedDocuments;

    /** ModNewDocument(&modData) — an empty document matching this one's format,
        recorded for the host to open. */
    SoundDocument& newDocument (const SoundDocument& like);

    /** ModShowError(modInfo, errNum, withCancel). Collected, not drawn — the UI
        layer decides how to voice it (§7.7). */
    struct ReportedError { ModResult errNum; bool withCancel; };
    std::vector<ReportedError> errors;

    void showError (ModResult errNum, bool withCancel = false)
    {
        errors.push_back ({ errNum, withCancel });
    }

private:
    std::string progressText;
    bool   progressOpen = false;
    int    sinceLastProgress = 0;
    int    progressInterval = 1;

    std::function<bool()> askDoExtendFn;
    std::function<bool (size_t, size_t, const std::string&)> onProgress;

    std::map<std::string, Settings> prefsStore;
};

} // namespace se
