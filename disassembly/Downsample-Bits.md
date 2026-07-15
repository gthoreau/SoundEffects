# Downsample Bits — full algorithm recovery (68k disassembly)

**Source:** `EFCT` 128 (4618 bytes) from `Effects Folder/Downsample Bits`, disassembled with capstone (M68K). 68k only. Module v1.1, ©1994 Alberto Ricci.

**Verdict: like Resample Rate, the actual bit-depth reduction is a host service call** — `ModResampleSoundBits(modData, newBps)` (`GlueRec` offset `0x96`), pushed with exactly the two arguments documented in `Glue.h`, nothing more. The module's own code is entirely pre-processing: validating the whole-document requirement (identical machinery to Resample Rate), then — when "Expert features" is on — **injecting scaled white noise into every sample of the whole document, in place, before handing off to the host**:

```
noiseScale = round_via_SANE( 2^(currentBps − targetBps) )   /* computed once, from the bit-reduction ratio */
for every sample in the whole document (not just the selection):
    sample = clamp( sample + Random()·noiseScale / 32767 )
(*glue->ModResampleSoundBits)(&mi, targetBps)                 /* the actual truncation happens here, host-side */
```

This is a genuine **noise-shaping-flavored dither**: the injected noise amplitude is auto-derived from how many bits are about to be discarded (`2^bitsRemoved`, i.e. roughly the size of one target-resolution quantization step), not a fixed constant — so a 16→8 downsample gets much louder dither than a 16→14 one, automatically. This resolves the SPEC's open "noise shaping filter `[ASM]`" item with hard evidence. See the caveat below on exactly which of the dialog's two radio options (**Dither** vs **Noise shaping**) this traced path corresponds to.

## Behavior, precisely

1. `UseResFile`, `Get1Resource('STR ',128)` → progress string *"Downsampling sound…"*, fails with `kModCouldntLoadMyRes` if missing.
2. Empty selection ⇒ whole sound (`ModMaxRelChSize`), same convention as every other module — largely moot given step 4.
3. Settings struct, confirmed by both the field-read code and the `NewHandle(16)` fallback loader: **16 bytes total** — `{ short targetBps; Byte expertOn; Byte radioFlag; long ditherConst1; long ditherConst2; long ditherConst3; }`. Copied out and the handle disposed immediately, same transient-settings pattern as every module examined.
4. **Sanity check**: if `targetBps >= mi->bps`, silently returns `kModCancel` with no user-facing message (the friendly *"You can't use Downsample to set the sample size to a higher value... use Upsample instead"* alert, `STR# 1000` #3, lives in the settings **dialog**'s own validation, not here — `effect()`'s check is just a defense-in-depth backstop for the "run with last settings, no dialog" path).
5. **Whole-document enforcement** — byte-for-byte the same logic as Resample Rate: `ModMaxChSize` for the true document size, check `selSt==0 && selEnd==maxSize && firstSelCh==1 && lastSelCh==numChans`; if not, `ModShowError(mi, 4, withCancel=true)` (`STR# 1000` #4: *"Downsampling can only be done on the whole sound... Do you want me to proceed and downsample the whole sound?"*); Cancel aborts, OK force-expands the selection to everything.
6. `ModSetupProgress`/`ReleaseResource` (deliberately deferred until *after* the validation above — no point drawing a progress window for a request about to be rejected).
7. **Expert-features gate**: `if (!expertOn) goto` straight to step 9 (skip all noise injection). `expertOn` defaults to `1` (Expert features **on**) in the no-saved-settings fallback — a genuinely unusual default for what the dialog frames as an advanced/optional feature.
8. **Noise computation and injection** (only reached when `expertOn` **and** `radioFlag` are both truthy — `radioFlag` also defaults to `1`):
   - `bitsRemoved = mi->bps − targetBps`; `scaleSeed = 1 << bitsRemoved` (an integer power of two).
   - Four chained `_FP68K` (SANE) calls convert `scaleSeed` through extended-precision floating point and back to a 16-bit integer `noiseScale` — the exact intermediate transform (selectors `0x280E`, `0x0004`, `0x0016`, `0x2010`) wasn't identified against a SANE opcode table, but the round-trip (int → float → … → int) strongly implies a **rounding/scaling step done in float for precision**, not a transcendental function — functionally, `noiseScale ≈ 2^bitsRemoved` after SANE-precision rounding.
   - Whole-channel loop (every byte of every channel, since the selection was already forced to the entire document): for each sample, `sample = clamp(sample + Random()·noiseScale/32767)` — a plain **additive white-noise dither**, amplitude auto-matched to the target bit depth, applied identically on the 8-bit (`XOR 0x80`) and 9–32-bit (big-endian sign-extended, `nbytes = min(frameSize,4)`) sample paths using the same conventions as every other module (§2.2). Progress is polled every sample via `ModShowProgress(..., canCancel=false)` — non-cancellable, same as Robotize/Echo/Delay/Noise/Smooth.
9. `(*glue->ModResampleSoundBits)(&mi, targetBps)` — the actual bit-depth reduction, entirely host-side; the module never touches sample values for truncation itself.
10. `ModCloseProgress`, return the propagated result.

## The Dither-vs-Noise-Shaping ambiguity (flagged, not guessed)

The dialog (`DITL 128`) has a checkbox *"Expert features:"* gating two mutually-exclusive radio buttons, *"Dither [__] LSB in amplitude"* and *"Noise shaping"* — with **only the Dither option carrying a user-editable numeric field**. The traced noise-injection path computes its amplitude **entirely internally** (from `2^bitsRemoved`, no user value consulted) — which is a noise-shaping design by definition (auto-scaled to the quantization step), not a user-specified-LSB dither. The settings struct's three trailing `long` fields (default `0x3FFD3FFD`, `0xAAAAAAAA`, `0x615DAB10` — plausibly a packed SANE extended constant, or independent fractional constants; not conclusively resolved) are copied out of the settings handle at load time but **were never referenced anywhere in the traced code path** — they are very likely the "Dither" branch's actual amplitude input, reached via a code path this pass did not locate. Two honest possibilities, not adjudicated here:
- **A locate-and-trace gap**: the module's ~4600-byte `EFCT` resource does have more code than what was walked for this doc (the settings-dialog handler at file offset `0x60c` was not traced), and a second, simpler additive-noise loop reading one of those three longs directly may exist there or be reached by a branch this pass missed.
- **A dead UI path**: this codebase has a *confirmed* precedent for shipping a fully-wired option that silently does nothing — the Undo menu item and its entire backing infrastructure (SPEC §1.2). It is plausible "Dither" is similarly unwired in this 0.9.2 build and only "Noise shaping" (or *both* checkbox+radio defaulting to on, which every fallback default confirms) actually does anything.

Either way, **the noise-shaping formula itself — the SPEC's specific open question — is solidly recovered**; only its exact radio-button binding carries residual uncertainty, called out explicitly rather than asserted.

## Reconstructed C (Ricci-style, drop-in against the SDK)

```c
typedef struct {
    short targetBps;
    Boolean expertOn;
    Boolean radioFlag;         /* 0/1 -- exact Dither-vs-NoiseShaping mapping not independently confirmed */
    long   ditherConst1, ditherConst2, ditherConst3;   /* unused in the traced path -- see note above */
} DownsampleSettingsRec, **DownsampleSettingsHandle;

pascal OSErr effect(ModParamsPtr mi, GluePtr glue, ModSettingsHandle *prefs)
{
    StringHandle progressString;
    DownsampleSettingsHandle settings = (DownsampleSettingsHandle)*prefs;
    short targetBps; Boolean expertOn, radioFlag;
    unsigned long maxSize;
    OSErr result = kModCancel;
    short t;

    UseResFile(mi->refNum);
    progressString = (StringHandle)Get1Resource('STR ', 128);
    if (!progressString) return kModCouldntLoadMyRes;

    if (mi->selSt == mi->selEnd)
        { mi->selSt = 0L; (*glue->ModMaxRelChSize)(&mi, &mi->selEnd); }

    targetBps = (*settings)->targetBps;
    expertOn  = (*settings)->expertOn;
    radioFlag = (*settings)->radioFlag;
    DisposHandle((Handle)*prefs);  *prefs = 0L;

    if (targetBps >= mi->bps) { ReleaseResource((Handle)progressString); return kModCancel; }

    (*glue->ModMaxChSize)(&mi, &maxSize);
    if (!(mi->selSt == 0 && mi->selEnd == maxSize &&
          mi->firstSelCh == 1 && mi->lastSelCh == mi->numChans))
    {
        if ((*glue->ModShowError)(mi, 4, true) == kModCancel)
            { ReleaseResource((Handle)progressString); return kModCancel; }
        mi->selSt = 0;  mi->selEnd = maxSize;
        mi->firstSelCh = 1;  mi->lastSelCh = mi->numChans;
    }

    (*glue->ModSetupProgress)(mi, &t, progressString);
    ReleaseResource((Handle)progressString);

    if (expertOn && radioFlag)
    {
        short bitsRemoved = mi->bps - targetBps;
        long  scaleSeed = 1L << bitsRemoved;
        short noiseScale = SANERoundToShort(scaleSeed);      /* 4 chained _FP68K calls, precision-rounding round-trip */
        unsigned long processed = 0, total = (*glue->GetBytesToProcess)(mi) / 1; /* whole doc, all channels */
        short channel, frameSize = mi->bps/8 + (mi->bps%8 != 0);
        OSErr pr = kModNoError;

        for (channel = 0; channel < mi->numChans; channel++)
        {
            Handle h = (*mi->hands)[channel].chan;
            unsigned long size = (*mi->hands)[channel].size;
            if (!size) continue;
            HLock(h);
            Ptr p = *h, end = *h + size;
            for (; p < end; p += (frameSize==1?1:frameSize), processed++)
            {
                signed long s = (frameSize==1) ? GetSample8(p) : GetSample16(p, mi->bps);
                signed long min, max;
                (*glue->ModGetSampleValueLimits)(mi->bps, &min, &max);
                s += (signed long)Random() * noiseScale / 32767;
                if (s & 0x80000000) { if (s < min) s = min; } else if (s > max) s = max;
                if (frameSize==1) SetSample8(p, s); else SetSample16(p, s, mi->bps);
                if (t && (pr = (*glue->ModShowProgress)(processed, total, &t, false))) break;
            }
            HUnlock(h);
            if (pr) break;
        }
    }

    result = (*glue->ModResampleSoundBits)(&mi, targetBps);   /* the actual truncation, entirely host-side */
    (*glue->ModCloseProgress)();
    return result;
}
```

Other entry points: `settings()` draws a custom dialog (`DLOG 128` + `dctb`/`PICT` for the header graphic) built from the standard `DITL 128` items — OK/Cancel buttons, "Downsample to [__] bits", "Expert features:" checkbox gating "Dither [__] LSB in amplitude" / "Noise shaping" radios; validates `targetBps < mi->bps` itself and shows the friendly error (`STR# 1000` #3) before ever reaching `effect()`. `about()` is `ModNoAbout.c`.

## Porting notes (web / Eurorack)

- **Mirror the Resample Rate architecture**: implement bit-depth truncation as a genuine host/engine service, with this module reduced to a validation + optional-dither-preprocessing shim, exactly as the original splits the work.
- The **auto-scaled noise formula is the reusable idea**: `noiseAmplitude = 2^(oldBits − newBits)` ties dither strength directly to the size of the discarded LSBs — a clean, parameter-free default worth keeping even if the UI for Dither's manual LSB entry is reimplemented as a real, independently-working control (recommended, given the original's ambiguity/possible dead path here).
- Keep the **whole-document-only** constraint, its confirmation alert, and the "can't downsample to a higher value, use Upsample instead" guardrail.
- Non-cancellable progress during the noise-injection pass, consistent with every other disassembled module except Resample Rate.
