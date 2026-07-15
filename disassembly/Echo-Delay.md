# Echo & Delay — full algorithm recovery (68k disassembly)

**Source:** `EFCT` 128 from `Effects Folder/Echo` (2646 bytes) and `Effects Folder/Delay` (2422 bytes), disassembled with capstone (M68K). 68k only (no FPU/PowerPC builds present in either resource fork). Module v1.1, ©1994 Alberto Ricci. Both use only integer math — no SANE.

**Verdict: Echo and Delay are the *same* offset-and-scale mix routine run in opposite directions**, and that direction is the whole trick:

```
Echo  (forward pass,  i = start … mixLimit−D−1):
    dst[i+D] = clamp( dst[i+D] + trunc(src[i] · strength/100) )

Delay (backward pass, i = mixLimit−1 … start+D):
    dst[i]   = clamp( dst[i]   + trunc(src[i−D] · strength/100) )
```

Both are literally `y[n] = clamp(y[n] + trunc(y[n−D]·strength/100))` — the *same* single-tap feedback recurrence Robotize uses (`disassembly/Robotize.md`), with `D` and `strength` now user parameters instead of Robotize's fixed 190/75. What differs is only the sweep direction over the in-place buffer:

- **Echo sweeps forward.** By the time the loop reaches index `n`, index `n−D` has *already been overwritten* by an earlier iteration (which wrote to `(n−D)+D = n`... more precisely: the write at source index `i` lands at `i+D`, and that destination is read again as a *source* once the forward cursor reaches `i+D`). This makes it genuinely recursive: one pass produces a whole train of decaying echoes at `D, 2D, 3D, …` — the delay literally feeds back into itself.
- **Delay sweeps backward.** The read at `i−D` always happens *before* position `i−D` is ever visited as a write target (writes only ever land at `≥ start+D`, and the backward cursor only decreases). So every read is the pristine original sample — no feedback, no repeats. Exactly **one** offset copy gets mixed in, precisely matching the About text: *"A copy of the selection is offset... and is added to the existing selection."*

This is a single shared code skeleton (settings load → frame-align → per-channel clamp → optional buffer-grow-and-silence → HLock → sample loop → HUnlock) with two small, deliberate differences: how far to extend the mix range (`mixLimit`), and which direction to walk it in.

## Shared preamble (both effects)

Identical to Robotize/Weird Tempo's opening, resolved against `Glue.h`'s `ModParamsRec`/`GlueRec` layout (offsets confirmed by cross-referencing every field access against `Gain.c`):

1. `UseResFile(mi->refNum); progressString = Get1Resource('STR ',128);` — fails with `kModCouldntLoadMyRes` (23) if missing. `ModSetupProgress`, `ReleaseResource`.
2. Empty selection ⇒ whole sound (`selSt=0`, `ModMaxRelChSize`).
3. `frameSize = bps/8 + (bps%8!=0)`; `samplesToProcess = GetBytesToProcess(mi)/frameSize` (for the progress bar only).
4. Settings loaded via the handle passed in `*prefs` (populated upstream by `LoadEffectSettings`/the settings dialog), fields copied into locals, delay **frame-aligned** (`delay -= delay % frameSize`), then `DisposHandle(*prefs); *prefs = 0L;` — the settings handle is transient, copied out immediately.
5. **`doExtend == 2` ("Ask Each Time")** triggers a live call to `glue->AskDoExtend` (offset `0x42` in `GlueRec`); otherwise the preference's own value is used directly. This is the `[SOURCE]`-grade confirmation of the SPEC's §7.2 *doExtend* / "Ask Each Time" alert path.
6. `ModGetSampleValueLimits(bps, &min, &max)`.
7. Per channel (`firstSelCh−1 … lastSelCh−1`, independent buffers): clamp `start`/`end` to that channel's size, frame-align both, skip if empty.

## Echo-specific: computing `mixLimit` ("until it decays")

Settings struct recovered from field offsets (`(**prefs)` accesses): **`{ long delay; short strength; Boolean onlyOn; short onlyCount; }`** — 10 bytes, matching the `NewHandle(10)` fallback-defaults path seen in Echo's `GetModSettings` loader. Matches the DITL exactly: Delay, Strength, "Only ⟨N⟩ echoes" checkbox+field.

- **"Only N echoes" checked:** `mixLimit = end + onlyCount·delayBytes` — a hard cap, no decay math at all.
- **Unchecked (default — "until it decays"):** simulated *before* touching any samples, purely to decide how far the buffer must grow:
  ```
  mixLimit = end;  amp = 100;  echoCount = 1;
  do {
      mixLimit += delayBytes;
      amp = amp * strength / 100;      /* integer, truncating */
      echoCount++;
  } while (amp > 8 && echoCount < 80);
  ```
  Recovered constants, straight from the binary: the decay stops once the running amplitude estimate drops to **≤ 8%** of full scale, or after **80** echoes, whichever comes first (at strength=70%, that's `⌈log(8/100)/log(0.7)⌉ = 8` echoes; the 80-echo cap only bites at very high strengths close to 100%). This loop never touches audio — it exists solely to size the mix range; the actual decay curve heard is an *emergent property* of the single recursive pass below, not of this precomputed count.
- Then, regardless of branch: **if `doExtend` resolved false, `mixLimit` is clamped back down to `end`** — no tail growth, decay confined to the selection's own overlap.
- If extending and `mixLimit > channelSize`: `SetHandleSize`, then **`GetHandleSize` re-queried** (graceful degradation — if the grow only partially succeeds under memory pressure, `mixLimit` silently shrinks to whatever was actually achieved, no error surfaced), `(*hands)[channel].size` updated, and the **new tail is silenced** via `ModSilenceOut(mi, ch+1, ch+1, oldSize, mixLimit)` before any echo is mixed into it (freshly grown handle memory is uninitialized garbage otherwise).
- `mi->selEnd` is extended to the **maximum `mixLimit` across all processed channels** (channels drift independently, same philosophy as Weird Tempo).

## Delay-specific: computing `mixLimit` (exactly one tap)

Settings struct: **`{ long delay; short strength; }`** — 6 bytes, no checkbox, no count. `mixLimit = doExtend ? (end + delayBytes) : end` — always exactly one delay-length, never a decay loop. Same grow/re-verify/silence/track-max-selEnd machinery as Echo, just sized for one echo instead of N.

## The mixing loop itself

Both effects branch on `frameSize==1` (8-bit, `XOR 0x80` conversion) vs 9–32-bit (big-endian sign-extended, built up byte-by-byte into the top of a 32-bit word and arithmetic-shift-right by `(4−nbytes)·8`, matching SPEC §2.2 exactly). Progress is polled every sample via `ModShowProgress(processed, total, &t, false)` — **`canCancel` is hard-coded `false` in both modules**, same non-cancellable convention as Robotize.

- **Echo:** `srcPtr` starts at `start` and walks forward to `mixLimit−delayBytes`; `dstPtr = srcPtr + delayBytes` walks alongside. `dst[dstPtr] = clamp(dst[dstPtr] + src[srcPtr]·strength/100)`. Because `dstPtr` eventually becomes a future `srcPtr` value, the recursion is live — this single loop *is* the whole decaying echo train, the precomputed `echoCount`/`mixLimit` from above having only decided the range.
- **Delay:** `dstPtr` starts at `mixLimit−1` and walks **backward** to `start+delayBytes`; the read pointer is always `dstPtr − delayBytes`. `dst[dstPtr] = clamp(dst[dstPtr] + src[dstPtr−delayBytes]·strength/100)` — every source read precedes its own position ever being a write target, so it's always the untouched original: one clean copy, no repeats.
- Both: if `mixLimit ≤ start+delayBytes` (selection shorter than one delay, or decay/echo range degenerates), the channel is skipped as a no-op after the buffer-size bookkeeping still runs.
- Saturating clamp identical to Robotize: `if (ahead & 0x80000000) { if (ahead<min) ahead=min; } else if (ahead>max) ahead=max;`

## Reconstructed C — Echo (Ricci-style, drop-in against the SDK)

```c
typedef struct { long delay; short strength; Boolean onlyOn; short onlyCount; }
    EchoSettingsRec, **EchoSettingsHandle;

pascal OSErr effect(ModParamsPtr mi, GluePtr glue, ModSettingsHandle *prefs)
{
    StringHandle progressString;
    EchoSettingsHandle settings = (EchoSettingsHandle)*prefs;
    Handle channelHand;
    Ptr chanBase, srcPtr, dstPtr, dstLoopEnd;
    signed long here, ahead, min, max, ampPercent;
    unsigned long delayBytes, mixLimit, channelSize, start, end, maxSelEnd, oldSize;
    unsigned long samplesToProcess, processedSamples = 0, echoCount;
    short strength, onlyCount, frameSize, t, channel;
    Boolean onlyOn, doExtendNow;
    OSErr progressResult = kModNoError, result = kModCancel;

    UseResFile(mi->refNum);
    progressString = (StringHandle)Get1Resource('STR ', 128);
    if (!progressString) return kModCouldntLoadMyRes;
    (*glue->ModSetupProgress)(mi, &t, progressString);
    ReleaseResource((Handle)progressString);

    if (mi->selSt == mi->selEnd)
        { mi->selSt = 0L; (*glue->ModMaxRelChSize)(&mi, &mi->selEnd); }

    frameSize = mi->bps/8 + (mi->bps%8 != 0);
    samplesToProcess = (*glue->GetBytesToProcess)(mi) / frameSize;

    delayBytes = (*settings)->delay;
    delayBytes -= delayBytes % frameSize;              /* frame-align */
    strength   = (*settings)->strength;
    onlyOn     = (*settings)->onlyOn;
    onlyCount  = (*settings)->onlyCount;
    DisposHandle((Handle)*prefs);  *prefs = 0L;

    doExtendNow = (mi->doExtend == 2) ? (*glue->AskDoExtend)() : (Boolean)mi->doExtend;
    (*glue->ModGetSampleValueLimits)(mi->bps, &min, &max);

    maxSelEnd = mi->selEnd;

    for (channel = mi->firstSelCh-1; channel < mi->lastSelCh; channel++)
    {
        start = mi->selSt;  end = mi->selEnd;
        channelSize = (*mi->hands)[channel].size;
        if (start > channelSize) start = channelSize;
        if (end   > channelSize) end   = channelSize;
        start -= start % frameSize;  end -= end % frameSize;
        if (end == start) continue;

        channelHand = (*mi->hands)[channel].chan;

        if (onlyOn)
            mixLimit = end + (unsigned long)onlyCount * delayBytes;
        else
        {
            mixLimit = end; ampPercent = 100; echoCount = 1;
            do {
                mixLimit += delayBytes;
                ampPercent = ampPercent * strength / 100;   /* truncating */
                echoCount++;
            } while (ampPercent > 8 && echoCount < 80);      /* decay to ≤8%, capped at 80 echoes */
        }

        if (!doExtendNow)
            mixLimit = end;                                  /* tail beyond selection is dropped */
        else if (mixLimit > channelSize)
        {
            oldSize = channelSize;
            SetHandleSize(channelHand, mixLimit);
            mixLimit = GetHandleSize(channelHand);            /* actual size achieved, may be less */
            (*mi->hands)[channel].size = mixLimit;
            (*glue->ModSilenceOut)(&mi, channel+1, channel+1, oldSize, mixLimit);
        }
        if (mixLimit > maxSelEnd) maxSelEnd = mixLimit;
        if (mixLimit <= start + delayBytes) continue;

        HLock(channelHand);
        chanBase   = *channelHand;
        srcPtr     = chanBase + start;
        dstPtr     = srcPtr + delayBytes;
        dstLoopEnd = chanBase + mixLimit - delayBytes;

        if (frameSize == 1)
        {
            for (; srcPtr < dstLoopEnd; srcPtr++, dstPtr++, processedSamples++)
            {
                here  = GetSample8(srcPtr);
                ahead = GetSample8(dstPtr);          /* may already carry earlier echoes -- recursive */
                ahead += here * strength / 100;
                if (ahead & 0x80000000) { if (ahead < min) ahead = min; }
                else if (ahead > max) ahead = max;
                SetSample8(dstPtr, ahead);
                if (t && (progressResult = (*glue->ModShowProgress)(processedSamples, samplesToProcess, &t, false)))
                    break;
            }
        }
        else
        {
            for (; srcPtr < dstLoopEnd; srcPtr += frameSize, dstPtr += frameSize, processedSamples++)
            {
                here  = GetSample16(srcPtr, mi->bps);
                ahead = GetSample16(dstPtr, mi->bps);
                ahead += here * strength / 100;
                if (ahead & 0x80000000) { if (ahead < min) ahead = min; }
                else if (ahead > max) ahead = max;
                SetSample16(dstPtr, ahead, mi->bps);
                if (t && (progressResult = (*glue->ModShowProgress)(processedSamples, samplesToProcess, &t, false)))
                    break;
            }
        }
        HUnlock(channelHand);
        result = kModNoError;
        if (progressResult) break;
    }

    mi->selEnd = maxSelEnd;
    (*glue->ModCloseProgress)();
    return result;
}
```

## Reconstructed C — Delay (Ricci-style, drop-in against the SDK)

Identical skeleton; only the settings struct, `mixLimit`, and loop direction change:

```c
typedef struct { long delay; short strength; } DelaySettingsRec, **DelaySettingsHandle;

pascal OSErr effect(ModParamsPtr mi, GluePtr glue, ModSettingsHandle *prefs)
{
    StringHandle progressString;
    DelaySettingsHandle settings = (DelaySettingsHandle)*prefs;
    Handle channelHand;
    Ptr chanBase, dstPtr, loopLow;
    signed long here, ahead, min, max;
    unsigned long delayBytes, mixLimit, channelSize, start, end, maxSelEnd, oldSize;
    unsigned long samplesToProcess, processedSamples = 0;
    short strength, frameSize, t, channel;
    Boolean doExtendNow;
    OSErr progressResult = kModNoError, result = kModCancel;

    UseResFile(mi->refNum);
    progressString = (StringHandle)Get1Resource('STR ', 128);
    if (!progressString) return kModCouldntLoadMyRes;
    (*glue->ModSetupProgress)(mi, &t, progressString);
    ReleaseResource((Handle)progressString);

    if (mi->selSt == mi->selEnd)
        { mi->selSt = 0L; (*glue->ModMaxRelChSize)(&mi, &mi->selEnd); }

    frameSize = mi->bps/8 + (mi->bps%8 != 0);
    samplesToProcess = (*glue->GetBytesToProcess)(mi) / frameSize;

    delayBytes = (*settings)->delay;
    delayBytes -= delayBytes % frameSize;
    strength   = (*settings)->strength;
    DisposHandle((Handle)*prefs);  *prefs = 0L;

    doExtendNow = (mi->doExtend == 2) ? (*glue->AskDoExtend)() : (Boolean)mi->doExtend;
    (*glue->ModGetSampleValueLimits)(mi->bps, &min, &max);

    maxSelEnd = mi->selEnd;

    for (channel = mi->firstSelCh-1; channel < mi->lastSelCh; channel++)
    {
        start = mi->selSt;  end = mi->selEnd;
        channelSize = (*mi->hands)[channel].size;
        if (start > channelSize) start = channelSize;
        if (end   > channelSize) end   = channelSize;
        start -= start % frameSize;  end -= end % frameSize;
        if (end == start) continue;

        channelHand = (*mi->hands)[channel].chan;

        mixLimit = doExtendNow ? (end + delayBytes) : end;   /* always exactly one echo's worth */
        if (doExtendNow && mixLimit > channelSize)
        {
            oldSize = channelSize;
            SetHandleSize(channelHand, mixLimit);
            mixLimit = GetHandleSize(channelHand);
            (*mi->hands)[channel].size = mixLimit;
            (*glue->ModSilenceOut)(&mi, channel+1, channel+1, oldSize, mixLimit);
        }
        if (mixLimit > maxSelEnd) maxSelEnd = mixLimit;
        if (mixLimit <= start + delayBytes) continue;

        HLock(channelHand);
        chanBase = *channelHand;
        loopLow  = chanBase + start + delayBytes;   /* lowest byte ever written */
        dstPtr   = chanBase + mixLimit;              /* one past the last byte written */

        if (frameSize == 1)
        {
            for (dstPtr -= 1; dstPtr >= loopLow; dstPtr--, processedSamples++)
            {
                ahead = GetSample8(dstPtr);
                here  = GetSample8(dstPtr - delayBytes);   /* always pristine -- no feedback */
                ahead += here * strength / 100;
                if (ahead & 0x80000000) { if (ahead < min) ahead = min; }
                else if (ahead > max) ahead = max;
                SetSample8(dstPtr, ahead);
                if (t && (progressResult = (*glue->ModShowProgress)(processedSamples, samplesToProcess, &t, false)))
                    break;
            }
        }
        else
        {
            for (dstPtr -= frameSize; dstPtr >= loopLow; dstPtr -= frameSize, processedSamples++)
            {
                ahead = GetSample16(dstPtr, mi->bps);
                here  = GetSample16(dstPtr - delayBytes, mi->bps);
                ahead += here * strength / 100;
                if (ahead & 0x80000000) { if (ahead < min) ahead = min; }
                else if (ahead > max) ahead = max;
                SetSample16(dstPtr, ahead, mi->bps);
                if (t && (progressResult = (*glue->ModShowProgress)(processedSamples, samplesToProcess, &t, false)))
                    break;
            }
        }
        HUnlock(channelHand);
        result = kModNoError;
        if (progressResult) break;
    }

    mi->selEnd = maxSelEnd;
    (*glue->ModCloseProgress)();
    return result;
}
```

Other entry points (both modules): `settings()` shows the host-driven declarative dialog (Delay/Strength/[Only-N-echoes] items, unit-dependent Delay field per the `CNTL 128 "Units"` resource); `about()` is `ModNoAbout.c`. A `GetModSettings`-style loader exists in both resource forks that falls back to `NewHandle(sizeof(settings))` with baked-in defaults (`rate/4` for delay, plus small constants for strength/onlyCount) if `LoadEffectSettings` ever fails outright — a deeper fallback than the DITL's visible defaults (3500/70 for Echo; empty for Delay), only reachable before any prefs have ever been saved to disk.

## Porting notes (web / Eurorack)

- **The direction *is* the feature.** A single "delay + scale, in-place" kernel with a runtime-chosen sweep direction gives you both a decaying comb (forward) and a clean single-tap echo (backward) for free — worth preserving as one primitive with a `recursive: bool` flag rather than two separate DSP paths.
- Keep the **8%/80-echo decay cap** for Echo's "until it decays" mode — it's what bounds the buffer growth and, since the cap is on the *pre-computed sizing pass* rather than the audio itself, the actual heard decay is a pure emergent property of the recursive single pass (get the sizing loop right and the audible decay comes along for free).
- Keep integer `×strength/100` truncation and hard clamping (same character as Robotize).
- Keep doExtend semantics: Ask-Each-Time calls the host live; Don't-Extend clips the tail with no error, silently dropping the part of the echo/delay that would've fallen outside the selection; Extend grows the (possibly-independent-length) channel buffer, degrading gracefully (not erroring) under memory pressure, and always silences fresh territory before mixing into it.
- Non-cancellable progress in both, same as Robotize — neither module offers a Stop button.
- `mi->selEnd` tracks the *maximum* tail across independently-processed channels — a stereo pair with different content can end up different lengths after Echo/Delay, same "channels drift" philosophy as Weird Tempo.
