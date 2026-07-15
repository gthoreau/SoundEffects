# Resample Rate — full algorithm recovery (68k disassembly)

**Source:** `EFCT` 128 (3382 bytes) from `Effects Folder/Resample Rate`, disassembled with capstone (M68K). 68k only. Module v1.1, ©1994 Alberto Ricci.

**Verdict: the module implements none of the interpolation itself.** `effect()` validates that the whole document is selected (offering to expand the selection if it isn't), reads a single `Fixed` target-rate value out of its settings, and hands the entire job to the **host** via the `GlueRec` service `ModResampleSoundRate(ModParamsHandle modData, Fixed newRate)` — offset `0x9A` in the glue table (`Glue.h`: *"resampling"* section, alongside `ModResampleSoundBits`). The actual interpolation algorithm (drop/duplicate vs. linear, the SPEC's open question) is therefore **application code, not module code** — the same category of problem as the Meters/Feet constant (§14 item 6): it requires disassembling the ~200 KB, unsymbolized app binary rather than a small self-contained `EFCT` resource. This doc nails down everything that *is* resolvable at the module level and documents the dead end precisely so it isn't re-walked.

## Behavior, precisely

1. `UseResFile(mi->refNum)`, `Get1Resource('STR ',128)` → progress string *"Resampling sound…"*, fails with `kModCouldntLoadMyRes` if missing.
2. `ModSetupProgress`/`ReleaseResource`, then the standard empty-selection-means-everything expansion (`ModMaxRelChSize`) — present but almost moot given step 4 below.
3. Settings are a **single 4-byte field**: `{ Fixed targetRate; }`. Loaded via the handle in `*prefs`, copied into a local, then `DisposHandle`+nil'd immediately — same transient-settings pattern as every other module in this codebase. The `GetModSettings` fallback loader (used only if `LoadEffectSettings` has never successfully saved anything to disk) does `NewHandle(4)` and defaults the field to **Fixed 44100.0** (`0xAC440000` — `0xAC44` = 44100 decimal) — the "CD" rate from the app's rate-presets popup (SPEC §6).
4. **Whole-document enforcement**, matching the About text verbatim (*"You can only do this on the whole document, not just on a selection or on some channels... you will be asked if you want to resample the whole sound"*): `ModMaxChSize(&mi, &maxSize)` fetches the *absolute* (not selection-relative) document size, then the code checks all four of `selSt==0`, `selEnd==maxSize`, `firstSelCh==1`, `lastSelCh==numChans` — i.e. literally "is every byte of every channel currently selected." If not, it calls `ModShowError(mi, 3, withCancel=true)` (`STR# 1000` #3: *"Resampling at a different rate can only be done on the whole sound, not on a selection of it only. Do you want me to proceed and resample the whole sound?"*); Cancel aborts with `kModCancel` (`0xFF80`/−128), OK force-sets `selSt=0`, `selEnd=maxSize`, `firstSelCh=1`, `lastSelCh=numChans` and continues.
5. A single `ModShowProgress(0xFFFFFFFF, 0xFFFFFFFF, &t, canCancel=true)` **pre-flight call** — sentinel `cur`/`total` values, used once, before any real work — almost certainly just a "let the user bail before committing to a long, monolithic, non-interruptible operation" cancel-check, since the module has **no visibility into `ModResampleSoundRate`'s internal progress** (there's no per-sample loop in this module at all to report through). This is the one module examined so far where `canCancel` is passed **`true`**, not the hard-coded `false` seen in Robotize/Echo/Delay/Noise/Smooth — but note it can only ever be checked *once*, at the very start, not throughout the actual resampling.
6. **The delegation**: `(*glue->ModResampleSoundRate)(&mi, targetRate)` — the entire rate-conversion algorithm, for every channel, at whatever bit depth, executes inside this one host call. The module's own code resumes only after it returns.
7. `ModCloseProgress`, return the propagated result.

No `GetSample8`/`GetSample16` calls, no per-channel loop, no `HLock`/`HUnlock` of any channel handle — confirming there is no sample-level code in this resource for the module to be responsible for. Contrast this with every previously-disassembled module (Robotize, Weird Tempo, Echo, Delay, Noise, Smooth), all of which own their full sample loop directly.

## The app-level dead end (documented so it isn't re-attempted blind)

`ModResampleSoundRate` resolves, via the same `CODE 0` jump-table technique used for the Meters/Feet investigation, to glue-table pointer-field index 38 (`GlueRec` offset `0x9A`), landing at `A5+0x9C2` → `CODE 8` ("Effects") routine-offset 19552. Disassembling there does **not** yield the resampler: it decodes cleanly as a real function (confirming the address-resolution mechanics are sound) but that function manipulates `'Setp'` resources (`Count1Resources`/`Get1IndResource`, ASCII `'Setp'` = `0x53657470` visible directly in the code) — i.e. named-setup management, unrelated to rate conversion. The same failure mode hit during the Meters/Feet search: the linker did **not** lay out `CODE 8`'s routines in `GlueRec` declaration order past roughly the first 20 pointer fields (dialog-engine functions, index 0–19, resolved correctly and were cross-checked); later fields are reordered/grouped by source file in a way that breaks naive sequential-index resolution. Actually locating `ModResampleSoundRate`'s body would need a real cross-reference pass (e.g. disassembling the whole 20 KB segment and pattern-matching a routine that takes a `Fixed` ratio and touches every `SEChanRec`), not the shortcut that worked for the first 20 fields — a materially bigger undertaking than any module examined so far, on par with or exceeding the Flanger effort.

## Reconstructed C (Ricci-style, drop-in against the SDK)

```c
typedef struct { Fixed targetRate; } ResampleSettingsRec, **ResampleSettingsHandle;

pascal OSErr effect(ModParamsPtr mi, GluePtr glue, ModSettingsHandle *prefs)
{
    StringHandle progressString;
    ResampleSettingsHandle settings = (ResampleSettingsHandle)*prefs;
    Fixed targetRate;
    unsigned long maxSize;
    short t;
    OSErr result;

    UseResFile(mi->refNum);
    progressString = (StringHandle)Get1Resource('STR ', 128);
    if (!progressString) return kModCouldntLoadMyRes;
    (*glue->ModSetupProgress)(mi, &t, progressString);
    ReleaseResource((Handle)progressString);

    if (mi->selSt == mi->selEnd)
        { mi->selSt = 0L; (*glue->ModMaxRelChSize)(&mi, &mi->selEnd); }

    targetRate = (*settings)->targetRate;
    DisposHandle((Handle)*prefs);  *prefs = 0L;

    (*glue->ModMaxChSize)(&mi, &maxSize);

    if (!(mi->selSt == 0 && mi->selEnd == maxSize &&
          mi->firstSelCh == 1 && mi->lastSelCh == mi->numChans))
    {
        if ((*glue->ModShowError)(mi, 3, true) == kModCancel)
            { (*glue->ModCloseProgress)(); return kModCancel; }
        mi->selSt = 0;  mi->selEnd = maxSize;
        mi->firstSelCh = 1;  mi->lastSelCh = mi->numChans;
    }

    if ((*glue->ModShowProgress)(0xFFFFFFFF, 0xFFFFFFFF, &t, true) == kModNoError)
        result = (*glue->ModResampleSoundRate)(&mi, targetRate);   /* the entire algorithm lives here, in the host */
    else
        result = kModCancel;

    (*glue->ModCloseProgress)();
    return result;
}
```

Other entry points: `settings()` draws the "Resample the sound (previously at N Hz) at [__] Hz" dialog with the rate-presets popup (`MENU 1001`), reading `mi->rate` for the "previously at" text (`STR# 1001`); `about()` is `ModNoAbout.c`.

## Porting notes (web / Eurorack)

- **This module is not where you'd look for the resampling algorithm in a from-scratch reconstruction** — implement rate conversion as a genuine host/engine service (exactly as the original architecture does), and treat "Resample Rate" the effect as a thin UI/validation shim over it, not a DSP kernel to port.
- Keep the **whole-document-only** constraint and its confirmation dialog — it's a real behavioral quirk (you cannot resample "just the selection," and saying yes silently discards/ignores whatever was selected in favor of everything).
- Keep the CD-rate (44100 Hz) fallback default for a first-ever run with no saved settings.
- The pre-flight `cur=total=0xFFFFFFFF` cancel-check-then-commit pattern is worth preserving as a UX idea even without knowing the host's internal progress granularity: offer one last "are you sure" moment right before a long, monolithic, uninterruptible operation.
