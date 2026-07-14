# Robotize — full algorithm recovery (68k disassembly)

**Source:** `EFCT` 128 (2570 bytes) from `Effects Folder/Robotize`, disassembled with capstone (M68K). 68k only (no FPU/PowerPC builds). Module v1.1, ©1994 Alberto Ricci. Uses SANE (`_Pack4`/FP68K) software floating point solely to compute the delay length; the audio path is pure integer math.

**Verdict: Robotize is a recursive feedback comb filter at a fixed ~117 Hz fundamental**, applied destructively in place:

```
y[n] = clamp( x[n] + trunc( y[n − D] × 75 / 100 ) )
```

with

```
D = trunc( 190 × rate / 22254.545454… )  samples
```

The two magic constants, recovered from the binary: integer **190** (word at code offset 0x8d2) and Fixed **0x56EE8BA3** = 22254 6/11 Hz — the classic Mac 22.254 kHz "Standard" rate. So the delay is 190 samples *at the Mac's native rate*, rescaled to the document's rate: a constant **≈ 8.538 ms** regardless of sample rate, i.e. a comb fundamental of **22254.5454 / 190 ≈ 117.13 Hz** at any rate (376 samples at 44.1 kHz). Speech pushed through a 0.75-feedback comb at 117 Hz acquires a monotone harmonic buzz — the robot voice, "like in the movies."

## Behavior, precisely

1. No settings, no warning dialog. Progress string: "Robotizing sound…". Empty selection ⇒ whole sound (SDK convention).
2. `D` is computed once per run via SANE extended arithmetic: `Fix2X(rate)`, ×190, ÷ `Fix2X(0x56EE8BA3)`, truncate to integral, convert to 64-bit comp; only the low 32 bits are used. At exactly the native rate the division cancels and D = 190.
3. Sample value limits come from the host (`ModGetSampleValueLimits(bps, &min, &max)`); the accumulation **saturates** (hard clamp), never wraps.
4. Per selected channel, independently: clamp selection to the channel, frame-align, then one forward pass:
   - For `i` from `selSt` to `selEnd − D·frameSize` (exclusive): read `y[i]` (current, possibly already-modified value), compute `trunc(y[i]·75/100)` with a signed integer multiply+divide (truncation toward zero), **add into `y[i+D]`**, clamp, write back.
   - Because destinations are written before the read cursor reaches them, this forward in-place pass realizes the recursive comb `y[n] = x[n] + 0.75·y[n−D]` exactly.
   - 8-bit path uses `XOR 0x80` conversion; 9–32-bit path uses the standard big-endian sign-extended read/write.
5. The first `D` samples of the selection are the unmodified seed; every sample from `selSt + D` to `selEnd` receives feedback (sources stop at `selEnd − D`, destinations run to `selEnd`).
6. **Progress is polled every sample with `canCancel = false`** — Robotize is the effect you cannot stop (per-channel, the Stop button is absent).
7. **Selections ≤ D samples (≈ 8.5 ms) are a silent no-op** that still returns success.
8. Output length never changes; `doExtend` is irrelevant and untouched. No dry/wet — the "dry" is simply the seed region propagating through the comb.

## Impulse response (verified by simulation)

Impulse of 100 → `100, 75, 56, 42, 31, 23, 17, 12, …` at intervals of D samples — the integer truncation (`75/100` with trunc-toward-zero) makes the decay slightly faster than a float 0.75 comb and hard-zeros the tail (…3, 2, 1, 0 — where a float comb rings on). This truncation grit is part of the sound; keep integer math in any port.

## Reconstructed C (Ricci-style, drop-in against the SDK)

```c
pascal OSErr effect(ModParamsPtr mi, GluePtr glue, ModSettingsHandle *prefs)
{
    StringHandle progressString;
    Handle h;
    Ptr src, dst, loopEnd;
    signed long here, ahead, min, max;
    unsigned long start, end, size, delayBytes, samplesToProcess, processed = 0;
    long delaySamples;
    extended x;                          /* SANE 80-bit */
    short frameSize, t, channel;
    OSErr pr = kModNoError, result = kModCancel;

    UseResFile(mi->refNum);
    progressString = (StringHandle)Get1Resource('STR ', 128);
    if (!progressString) return kModCouldntLoadMyRes;
    (*glue->ModSetupProgress)(mi, &t, progressString);
    ReleaseResource((Handle)progressString);

    if (mi->selSt == mi->selEnd)
        { mi->selSt = 0L; (*glue->ModMaxRelChSize)(&mi, &mi->selEnd); }

    frameSize = mi->bps/8 + (mi->bps%8 != 0);
    samplesToProcess = (*glue->GetBytesToProcess)(mi) / frameSize;

    x = Fix2X(mi->rate);                  /* document rate                */
    x = 190.0 * x / Fix2X(0x56EE8BA3);    /* 22254.5454… Hz               */
    delaySamples = (long)trunc(x);        /* 190 samples @ native rate    */
    delayBytes   = delaySamples * frameSize;

    (*glue->ModGetSampleValueLimits)(mi->bps, &min, &max);

    for (channel = mi->firstSelCh-1; channel < mi->lastSelCh; channel++)
    {
        start = mi->selSt;  end = mi->selEnd;
        size = (*mi->hands)[channel].size;
        if (start > size) start = size;
        if (end   > size) end   = size;
        start -= start % frameSize;  end -= end % frameSize;
        if (end <= start) continue;

        h = (*mi->hands)[channel].chan;
        HLock(h);
        src = *h + start;
        dst = src + delayBytes;
        loopEnd = *h + end - delayBytes;   /* sources stop D before the end */

        for (; src < loopEnd; src += frameSize, dst += frameSize, processed++)
        {
            if (frameSize == 1) { here = GetSample8(src);  ahead = GetSample8(dst); }
            else { here = GetSample16(src, mi->bps);  ahead = GetSample16(dst, mi->bps); }

            ahead += here * 75 / 100;               /* signed, trunc toward 0 */
            if (ahead & 0x80000000) { if (ahead < min) ahead = min; }
            else if (ahead > max) ahead = max;

            if (frameSize == 1) SetSample8(dst, ahead);
            else SetSample16(dst, ahead, mi->bps);

            if (t && (pr = (*glue->ModShowProgress)(processed, samplesToProcess,
                                                    &t, false)))   /* no Stop! */
                break;
        }
        HUnlock(h);
        result = kModNoError;
        if (pr) break;
    }
    (*glue->ModCloseProgress)();
    return result;
}
```

Other entry points: `settings()` returns `kModHasNoSettings` ("The Robotize effect does not have any options to set."), `about()` is ModNoAbout.c (standard about window).

## Porting notes (web / Eurorack)

- Keep integer `×75/100` with truncation toward zero and hard clamping — the fast, dead-stop decay is the character.
- Keep D derived from the *document's* rate against 22254.5454 Hz so the robot pitch is always ≈ 117.13 Hz. (For the Eurorack module, D is a natural knob: "ROBOT PITCH" sweeping the comb fundamental, with 117 Hz as the detent.)
- Keep the ≤ 8.5 ms-selection no-op and the non-cancellable progress.
- The seed region (first D samples untouched) matters at loop points: repeated application re-combs the already-combed signal — stacking Robotize deepens the resonance, which the original invites since there's no undo.
