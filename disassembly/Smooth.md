# Smooth — full algorithm recovery (68k disassembly)

**Source:** `EFCT` 128 (1662 bytes) from `Effects Folder/Smooth`, disassembled with capstone (M68K). 68k only. Module v1.1, ©1994 Alberto Ricci. No settings (`kModHasNoSettings`; `STR# 1000` #1: *"The Smooth effect does not have any options to set."*).

**Verdict: Smooth is a 2-point neighbor average that *excludes the center sample*, and it leaves the selection's first and last sample completely untouched.**

```
out[i] = (in[i−1] + in[i+1]) / 2      for i = start+1 … end−2   (interior samples only)
out[start]  = in[start]                (unmodified — no left neighbor to average)
out[end−1]  = in[end−1]                (unmodified — no right neighbor to average)
```

This directly contradicts a literal reading of the About box (*"averaging every sample with the preceding and the following one"*, which reads like a 3-point `(prev+cur+next)/3`) and of the SPEC's `[DOC]`-tagged "3-point moving average" description — the actual formula recovered from the binary is a **2-point** average of the two *neighbors only*, with the current sample's own value playing no part in its own replacement. And rather than clamping/duplicating an edge sample the way many smoothing filters do, Ricci's code simply **excludes the two boundary samples from the loop entirely** — they read and write nothing for those positions.

## Behavior, precisely

1. `UseResFile`, `Get1Resource('STR ',128)` → progress string *"Smoothing…"*, `ModSetupProgress`, `ReleaseResource`. Empty selection ⇒ whole sound, same convention as every other module.
2. `frameSize = bps/8 + (bps%8!=0)`; `samplesToProcess = GetBytesToProcess(mi)/frameSize` for the progress bar.
3. Per channel (`firstSelCh−1 … lastSelCh−1`, independent buffers): clamp `start`/`end` to the channel's size, frame-align both.
4. **Minimum-size gate:** skip the channel entirely unless `end − frameSize > start` (i.e. at least two frames selected). Because of how the loop bound is set up (next point), an exactly-2-frame selection *does* pass this gate and does `HLock`/`HUnlock` and progress setup, but its loop body never executes (both samples are boundary samples) — no audible effect either way. A real smoothing effect needs **at least 3 frames** selected.
5. `HLock` the channel handle. `srcPtr = chanBase+start` is read *once*, before the loop starts, and cached as the initial "previous" value — this is the seed for sample `start`, whose value is never itself overwritten. The loop's write cursor then starts at `start+frameSize` (the *second* sample) and the loop's upper bound is set to `end−frameSize` (excluding the *last* sample).
6. **Loop body**, per position `p` from `start+frameSize` to `end−2·frameSize` inclusive:
   - Read `next = *(p+frameSize)` (not yet touched — always ahead of the write cursor).
   - `avg = (prev + next) / 2` (unsigned/logical right shift — see the bit-trick below for the 9–32-bit path).
   - Read `prev_for_next_iteration = *p` — capturing the **original, not-yet-overwritten** value of the current position, so the next iteration's "previous" is always a pristine input sample, never an already-smoothed one. Non-cascading: every average is computed from two original neighbors.
   - Write `avg` to `*p`.
7. Progress polled every sample via `ModShowProgress(processed, total, &t, false)` — **`canCancel` hard-coded `false`**, same non-cancellable convention as every other disassembled module so far (Robotize, Echo, Delay, Noise).
8. `HUnlock`. Selection length and channel sizes are **never** touched — Smooth is a pure fixed-length in-place transform, no `doExtend` interaction, no `mi->selEnd` write-back.

## The 8-bit vs 9–32-bit split

- **8-bit (`frameSize==1`):** raw offset-binary bytes are averaged *directly*, with no `XOR 0x80` sign conversion at all. This works because offset-binary is just signed values shifted up by a constant (128) — averaging two offset-binary values and averaging their signed equivalents give the same result modulo that shared constant, so the sign flip can be skipped entirely for a pure average. A small, easy-to-miss optimization only visible in the disassembly.
- **9–32-bit:** each sample is loaded big-endian into the top of a 4-byte buffer (standard `GetSample16` technique, `nbytes = min(frameSize,4)`), then the buffer is **XORed against its own sign-bit mask** (`1 << (nbytes·8−1)`, e.g. `0x8000` for 16-bit) before the arithmetic-shift-into-logical-shift adjustment — flipping two's-complement into an "excess" unsigned representation. Both `prev` and `next` go through this flip, are added and halved with a plain unsigned `lsr.l #1`, and the *result* is flipped back with the same XOR mask before being written out (`SetSample16`-style, top `nbytes` bytes). This is the standard trick for averaging two signed integers via unsigned shift without an intermediate overflow/sign bug — generalized here to work for any sample width from 1 to 4 bytes via the computed mask, not hard-coded to 16 bits.

## Reconstructed C (Ricci-style, drop-in against the SDK)

```c
pascal OSErr effect(ModParamsPtr mi, GluePtr glue, ModSettingsHandle *prefs)
{
    StringHandle progressString;
    Handle channelHand;
    Ptr chanBase, p, loopEnd;
    unsigned long prevRaw, nextRaw, avg;
    unsigned long start, end, channelSize;
    unsigned long samplesToProcess, processedSamples = 0;
    short frameSize, t, channel;
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

    for (channel = mi->firstSelCh-1; channel < mi->lastSelCh; channel++)
    {
        start = mi->selSt;  end = mi->selEnd;
        channelSize = (*mi->hands)[channel].size;
        if (start > channelSize) start = channelSize;
        if (end   > channelSize) end   = channelSize;
        start -= start % frameSize;  end -= end % frameSize;
        if (end - frameSize <= start) continue;          /* need >=2 frames just to enter; >=3 to change anything */

        channelHand = (*mi->hands)[channel].chan;
        HLock(channelHand);
        chanBase = *channelHand;

        p       = chanBase + start + frameSize;           /* second sample: first write position */
        loopEnd = chanBase + end   - frameSize;            /* last sample excluded */

        if (frameSize == 1)
        {
            prevRaw = *(unsigned char *)(chanBase + start);   /* seed: raw offset-binary, no XOR needed for averaging */
            for (; p < loopEnd; p++, processedSamples++)
            {
                nextRaw = *(unsigned char *)(p + 1);
                avg = (prevRaw + nextRaw) >> 1;
                prevRaw = *(unsigned char *)p;                 /* capture original before overwrite */
                *(unsigned char *)p = (unsigned char)avg;
                if (t && (progressResult = (*glue->ModShowProgress)(processedSamples, samplesToProcess, &t, false)))
                    break;
            }
        }
        else
        {
            unsigned long signMask = 1UL << (frameSize*8 - 1);
            prevRaw = (GetSample16(chanBase + start, mi->bps) ^ signMask);  /* two's-complement -> excess */
            for (; p < loopEnd; p += frameSize, processedSamples++)
            {
                nextRaw = (GetSample16(p + frameSize, mi->bps) ^ signMask);
                avg = (prevRaw + nextRaw) >> 1;
                prevRaw = (GetSample16(p, mi->bps) ^ signMask);
                SetSample16(p, (signed long)(avg ^ signMask), mi->bps);      /* excess -> two's-complement */
                if (t && (progressResult = (*glue->ModShowProgress)(processedSamples, samplesToProcess, &t, false)))
                    break;
            }
        }
        HUnlock(channelHand);
        result = kModNoError;
        if (progressResult) break;
    }

    (*glue->ModCloseProgress)();
    return result;
}
```

Other entry points: `settings()` returns `kModHasNoSettings` with the string *"The Smooth effect does not have any options to set."*; `about()` is `ModNoAbout.c`.

## Porting notes (web / Eurorack)

- **Do not "fix" the formula to a 3-point `(prev+cur+next)/3` average** — the recovered behavior is genuinely a 2-point neighbor average, and it's mild enough (roughly a single-pole box-car half as aggressive as a true 3-tap average) that it matches the "gentle declick" character the module is used for in practice.
- Keep the **untouched boundary samples** — the first and last sample of every selection survive Smooth verbatim, no edge padding/clamping/reflection. Repeated application (there's no Undo) will still eventually smooth those positions once a *larger* or shifted selection includes them as interior points.
- Non-cascading by construction (all reads happen before the corresponding write, via the one-sample-ahead cache) — an important detail for a faithful port, since a naive single-buffer in-place `(prev+next)/2` written directly into a growing "prev" reference would silently become a different (IIR-ish) filter if the cache trick is dropped.
- Non-cancellable progress, consistent with every other disassembled module in this project so far.
- No parameters, no length change, no `doExtend` interaction — for the Eurorack module this is another one-button "SMOOTH" press, same philosophy as Robotize/Weird Tempo/Noise.
