# Flanger — full algorithm recovery (68k disassembly)

**Source:** `EFCT` 128 (25320 bytes) from `Effects Folder/Flanger`, disassembled with capstone (M68K/040). Module v1.1, ©1994 Alberto Ricci. Cross-checked against `EFCF` 128 (15526 bytes, the 68881-FPU-capable build shipped alongside `EFCT` in the same resource fork) wherever the software-SANE (`_FP68K`, trap `$A9EB`) selector words in `EFCT` were ambiguous — `EFCF` uses real 68881 mnemonics (`fsin`, `fintrz`, `fmul`, …) that resolve those ambiguities unambiguously. `EFCT` is treated as authoritative for control flow and struct layout; `EFCF` was consulted only to name the floating-point operations precisely, per this project's 68k-first convention.

**Verdict — the four headline questions, answered directly:**

1. **Interpolation: none.** The delay position is computed to full 80-bit SANE-extended precision (the whole LFO/envelope-follower pipeline is floating point), but the *very last step* before every sample read is `fintrz` (round-toward-zero) — confirmed by name in `EFCF`, and by a matching single-pointer, no-second-tap `_FP68K` call in `EFCT` — truncating the float delay to a 32-bit integer byte offset. Exactly **one** byte is then read at that truncated position. There is no second read at a neighboring position and no blend between adjacent delay-line samples anywhere in the per-sample loop. Sub-sample delay-time precision is computed and then thrown away at read time.
2. **Feedback tap point: the whole per-sample DSP routine is compiled twice**, once per sweep direction, selected once per call by testing the *Feedback* checkbox (settings offset `0x34`) — not, as an early scan of the entry trampoline suggested, a hardware-FPU/MachineInfo dispatch. Feedback **off** walks the channel buffer **backward** (write cursor decrements); feedback **on** walks it **forward** (write cursor increments) — the exact same forward-recursive / backward-non-recursive trick already documented for Echo vs. Delay (`disassembly/Echo-Delay.md`). Because every read is `(a2 − d3)` with `d3 ≥ 0` bytes behind the write cursor `a2`, backward sweep can only ever read already-existing (pre-effect) history, while forward sweep will, once the sweep has advanced further than the current delay offset, read back its **own already-mixed (dry+wet) output** from earlier in the same pass — that is the entire feedback mechanism. There is no separate feedback-gain constant; the recursion's effective gain is whatever the Flange-percentage wet weight already is.
3. **LFO: real trigonometry for Sinusoidal, a linear ramp/accumulator (no table, no trig) for Triangular and Sawtooth**, phase driven by a plain per-channel **sample counter** (not wall-clock, not a separate phase accumulator advanced by a fractional-Hz increment):
   - Sinusoidal: `offset = Delay + Width · sin(2π · n / Period)`, `n` = samples processed so far in this channel, `Period` in samples — computed with a genuine `fsin` (68881 build) / SANE sine call (68k build), not a lookup table.
   - Triangular: a running float accumulator stepped by `±4·Width/Period` per sample, with the *sign of the step* flipped (reflection) whenever the accumulator would cross `Delay+Width` or `Delay−Width`.
   - Sawtooth: the same accumulator stepped by `+2·Width/Period` per sample, **hard-reset** to `Delay−Width` (not reflected) whenever it exceeds `Delay+Width`.
4. **Envelope Follower: block-peak, not RMS, not a single-pole smoother.** A true peak (`ModGetPeak`, `GlueRec` offset `0x66`) is scanned over 1024-sample windows — once at the very end of the selection to get a fixed reference peak, then again every 1024 samples as the sweep advances — and the per-sample envelope value used for modulation is a **linear interpolation between consecutive 1024-sample block peaks**, weighted by the sample's position within the current block. `envelopeAdjustment = interpolatedPeak · Depth_bytes / maxSampleValue`, added to the delay offset when "Louder means Longer", subtracted when "Shorter"; the result is clamped to ≥ 0 before use.

The final dry/wet **mix**, in contrast to all of the above, is plain 32-bit truncating integer math (`mulu.l`/`divs.l`), matching every other Ricci module's "final sample math is always integer" convention: `output = dry·(100−flange%)/100 + wet·flange%/100`, written back **in place** over the dry sample (replace, not add).

## Behavior, precisely

### Dispatch and preamble

- `bra.w` at offset `0x18` → message dispatcher at `0x4910`; the `kMsgRun` ('Mrun') case first calls a settings-loader stub at `0x4814` (a `GetModSettings`-style fallback: `LoadEffectSettings` via `glue+0x12`, and on failure `NewHandle($3c)` = **60 bytes**, filled with hard-coded defaults — Flange%=80, Feedback=off, Modulation=1 (Sinusoidal), EnvelopeFollower=off, Louder=1 (Longer), Delay/Width/Period/Depth left at whatever garbage `NewHandle` returns, since a full extended-precision default for those wasn't worth hand-coding), then falls into `effect()` at file offset `0x1d6` — same convention as every other module in this project.
- `effect()`'s first real decision, at `0x1ea` (`tst.w $34(a0)` on the doubly-dereferenced settings pointer), is the Feedback-flag branch described above: `beq` (Feedback off) → `jsr $22e` (backward-sweep implementation, ~8700 bytes); fall-through (Feedback on) → `jsr $2430` (forward-sweep implementation, ~8350 bytes). Each of these two functions is a **complete, independent copy** of the whole per-channel pipeline — confirmed by `UseResFile`/`Get1Resource`/`ReleaseResource`/`DisposeHandle`/`HLock`/`HUnlock` each appearing **exactly twice** in the whole 25 KB resource, once inside each variant. Ricci paid for this in code size to avoid testing the Feedback flag inside the hot per-sample loop.
- Progress string `STR 128` = *"Flanging…"*. `STR# 1000` #3 = *"The Flanger effect can only be applied to an existing sound. Please open or record one before trying to apply the effect again."* `about` (`TEXT 1000`) is the full multi-paragraph help text already quoted in the SPEC's item 12 (confirms the About-box wording verbatim). `ModShowProgress` is called with `canCancel` hard-coded **false** (a `clr.b -(a7)` pushed right before the call, in both sweep variants) — Flanger is **non-cancellable**, same convention as Robotize/Echo/Delay/Noise/Smooth despite being by far the most expensive module examined so far.
- No `doExtend` interaction anywhere in the resource (no `AskDoExtend` call, no `SetHandleSize`): Flanger is a fixed-length in-place effect. The selection's length and `mi->selEnd` are never touched.

### Settings struct (in-memory, 60 bytes) vs. `Setp`/prefs record (on-disk, 36 bytes)

`effect()` dereferences the settings handle twice (`(**prefs)`) to get a struct pointer, then reads:

```
offset  size  field
0x00    12    Delay   { short unitsCode; extended80 valueInThatUnit; }   (2+10, no padding)
0x0C    12    Width   { short unitsCode; extended80 valueInThatUnit; }
0x18    12    Period  { short unitsCode; extended80 valueInThatUnit; }
0x24    12    Depth   { short unitsCode; extended80 valueInThatUnit; }
0x30     4    FlangePercent (long)
0x34     2    Feedback (short, boolean)
0x36     2    Modulation (short, 1=Sinusoidal 2=Triangular 3=Sawtooth)
0x38     2    EnvelopeFollower (short, boolean)
0x3A     2    LouderMeans (short, 1=Longer 2=Shorter)
                                                            total = 0x3C = 60 bytes
```
Confirmed field-by-field: `0x34` is the Feedback branch already described; `0x32` (the low word of the `0x30` long) is read, clamped to `[0,100]`, and `100 − value` cached as the dry weight — this is the Flange-percentage clamp, matching the DITL's "Flange percentage:" field exactly; `0x36`/`0x38`/`0x3A` are read into locals right before the modulation-type/envelope-follower dispatch chain (`cmpi.w #1 → Sinusoidal`, `#2 → Triangular` via `addq+bpl`, `#3 → Sawtooth` via `subq+beq`).

The four 12-byte duration groups are each converted to a plain byte count by a shared subroutine (file offset `0x6046`) that takes the group's `{unitsCode, extended}` pair plus `mi->rate` (`ModParamsRec+0x24`) and `mi->bps` (`+0x28`) and returns a `long` — i.e. exactly the semantics `ModConvertToBytes` would have, but implemented locally in the module using SANE extended math rather than delegated to the host. This is called once per channel for Delay, Width, Period and Depth (`-$66(a6)`, `-$6a(a6)`, `-$6e(a6)`, `-$98(a6)` in the backward-sweep variant), which is why the module needs so much floating point even for effects (LFO centerline/amplitude/period) that end up as plain byte counts.

The **on-disk `Setp` record is a different, more compact 36-byte layout** — it is the host's serialized dialog-item list (already-converted-to-bytes values, as `type_editFieldUnitDependent` items are documented to store, plus a remembered display-units code for each so the dialog can show the right unit again on reopen):

```
offset  size  field
0x00     4    FlangePercent (long, 0–100)
0x04     2    Feedback (short, boolean)
0x06     4    Delay  (long, bytes)      0x0A  2  DelayUnits  (ModUnits.h code)
0x0C     4    Width  (long, bytes)      0x10  2  WidthUnits
0x12     4    Period (long, bytes)      0x16  2  PeriodUnits
0x18     2    Modulation (1/2/3)
0x1A     2    EnvelopeFollower (boolean)
0x1C     4    Depth  (long, bytes)      0x20  2  DepthUnits
0x22     2    LouderMeans (1/2)
                                                            total = 0x24 = 36 bytes
```
Recovered empirically (not guessed): every one of the 9 factory presets decodes with this layout into **clean, round millisecond values** at the "Standard" 22254.5454 Hz rate (see table below) — that internal consistency across 9 independently-authored presets, several of them sharing exact byte values for Delay/Width, is what confirms the field boundaries and the reference rate simultaneously. All `unitsCode` fields are `3` = milliseconds (`ModUnits.h`) in every preset. Because the stored values are **byte counts, not unit-tagged reals**, a preset's *effective* millisecond timing will only match the table below when applied to audio at 22254.5454 Hz; applied to a document at a different sample rate, the same byte counts convert to different real-world millisecond values (the display-units code is cosmetic, remembered only so the dialog shows the right popup — it plays no role in `effect()`'s math).

### The per-sample loop (both sweep directions share this body)

Per selected channel: clamp/frame-align `start`/`end` to the channel size (standard preamble, identical to every other module); then **advance `start` by `(Delay_bytes + Width_bytes)`** — the first `Delay+Width` bytes of the selection are consumed as unprocessed delay-line seed/history and never themselves become an output position (their audio is still readable as history for later positions). `HLock`, then for `frameSize==1` (8-bit) vs. `frameSize 2–4` (9–32-bit, mirrored code at file offset `0x12d6` using the standard big-endian sign-extended multi-byte read/write convention) — these are two more full copies of the loop, giving 2 (feedback) × 2 (bit depth) × {3 waveforms × 2 (envelope-follower on/off), mostly unrolled} ≈ a dozen near-identical compiled bodies. One representative body (backward sweep, 8-bit, Sinusoidal, envelope follower on) runs, per sample:

1. **Dry read**: `dry = *(a2) XOR 0x80` (offset-binary → signed) at the current write cursor `a2` — *before* any delay offset is applied.
2. **LFO delay offset** `d3`, entirely in SANE-extended float: `phase = 2π·n/Period_samples; d3f = Delay + Width·sin(phase)` (Sinusoidal — see Triangular/Sawtooth formulas above), then **`fintrz`/SANE round-to-zero → 32-bit integer** `d3`.
3. **Envelope-follower adjustment** (only if the checkbox is on): `interpolatedPeak = refPeak + (chunkPeak − refPeak)·(chunkPos/1024)`; `adj = interpolatedPeak · Depth_bytes / maxSampleValue`; `d3 += adj` (Louder = Longer) or `d3 -= adj` (Shorter). `chunkPeak` is re-scanned via `ModGetPeak` every 1024 samples as the sweep crosses a block boundary; `refPeak` is the peak of the last ≤1024 bytes of the whole selection, scanned once before the loop starts.
4. **Clamp** `d3 = max(d3, 0)` — the delay can never go negative (never reads ahead of the write cursor).
5. **Wet read**: `wet = *(a2 − d3) XOR 0x80`.
6. **Mix, plain integer math**: `out = clamp(dry·(100−flange%)/100 + wet·flange%/100, min, max)`.
7. **Write in place**: `*(a2) = out XOR 0x80` — replaces the dry sample, does not add to it.
8. `ModShowProgress(processed, total, &t, false)` (non-cancellable); advance `a2` (decrement here, increment in the Feedback-on variant), advance the chunk-local counter `d6`, loop.

Triangular and Sawtooth skip step 2's trig call in favor of the ramp/reflect or ramp/reset accumulators described in the verdict, but steps 1 and 3–8 are byte-for-byte identical code, duplicated per waveform. All three waveforms funnel into the same `fintrz`-then-single-read pattern — the "no interpolation" finding is universal across modulation types, not specific to Sinusoidal.

## The 9 factory `Setp` presets, decoded

Reference rate: 22254.5454 Hz (SoundEffects' "Standard" rate), 8-bit mono (`frameSize=1`, so bytes = samples). All Delay/Width/Period/Depth values below are the `Setp`'s raw byte count converted with this rate; Modulation is always `1` (Sinusoidal) and Louder is always `1` (Longer) across all 9 presets; Feedback is `0` (off) in **every** factory preset — none of them actually exercises the feedback code path documented above.

| Preset (`Setp` id) | Flange % | Delay (ms) | Width (ms) | Period (ms) | Env. Follower | Depth (ms) |
|---|---:|---:|---:|---:|:---:|---:|
| Plain Flange – Fast (134) | 50 | 6.02 | 3.01 | 800 | off | — |
| Plain Flange – Medium (7361) | 50 | 11.01 | 2.02 | 1000 | off | — |
| Plain Flange – Slow (14170) | 50 | 11.01 | 2.02 | 2250 | off | — |
| Plain Flange – Slower (24670) | 50 | 11.01 | 2.02 | 3500 | off | — |
| Plain Flange – Slowest (6706) | 50 | 11.01 | 2.02 | 4750 | off | — |
| Soft Flange (26532) | 50 | 11.01 | 2.02 | 2000 | off | — |
| Distorto – High (6776) | 100 | 4.99 | 4.00 | 300 | **on** | 20.00 |
| Distorto – Medium (9615) | 70 | 4.94 | 3.95 | 500 | **on** | 3.95 |
| Distorto – Low (2354) | 70 | 4.99 | 2.97 | 600 | **on** | 1.98 |

Notable structure recovered from the raw bytes, not just the names: the five "Plain Flange" variants and "Soft Flange" are **byte-identical in every field except Period** (all share Delay=245 bytes/11.01 ms, Width=45 bytes/2.02 ms) — "Fast" is the one outlier with its own (smaller) Delay/Width of 134/67 bytes. "Soft Flange" is not algorithmically distinct from the Plain Flange family at all; it is simply a sixth period value (2000 ms) slotted between Medium and Slow, given a friendlier name. The three "Distorto" presets get their harsher, more animated character from 100%/70% flange (near/at wet-only), a much shorter Delay (~5 ms, phaser territory per the About text's own "less than 8 ms" guidance) and a fast Period (300–600 ms), plus Envelope Follower on with Depth 2–20 ms — **not** from the Feedback checkbox, which stays off even in the most aggressive preset.

## Reconstructed C (Ricci-style, drop-in against the SDK)

This reproduces the *logical* algorithm; the shipped binary compiles roughly a dozen near-duplicate copies of the loop body (per Feedback on/off × per bit-depth × per waveform × per envelope-follower on/off) to keep every branch out of the per-sample hot path. `extended` below stands in for the SDK's SANE 80-bit float type.

```c
typedef struct { short unitsCode; extended value; } UnitExtended;      /* 12 bytes, no padding */

typedef struct {
    UnitExtended delay, width, period, depth;   /* 0x00, 0x0C, 0x18, 0x24 */
    long  flangePercent;                         /* 0x30 */
    short feedback;                               /* 0x34 */
    short modulation;                              /* 0x36: 1=Sin 2=Tri 3=Saw */
    short envelopeFollower;                         /* 0x38 */
    short louderMeansLonger;                         /* 0x3A: 1=Longer 2=Shorter */
} FlangerSettingsRec, **FlangerSettingsHandle;         /* 0x3C = 60 bytes */

/* shared by both sweep directions and both bit-depth paths */
static long LfoOffsetBytes(short modulation, double n, double period,
                            double delay, double width, TriState *ramp)
{
    double v;
    switch (modulation) {
    case 1:  /* Sinusoidal -- real trig, not a table */
        v = delay + width * sin(2.0 * M_PI * n / period);
        break;
    case 2: { /* Triangular -- linear accumulator, reflects at delay+-width */
        double step = 4.0 * width / period;
        ramp->phase += ramp->sign * step;
        if (ramp->phase > delay + width || ramp->phase < delay - width)
            { ramp->sign = -ramp->sign; ramp->phase += 2.0 * ramp->sign * step; }
        v = ramp->phase;
        break; }
    default: /* Sawtooth -- linear ramp, hard reset (no reflection) */
        ramp->phase += 2.0 * width / period;
        if (ramp->phase > delay + width) ramp->phase = delay - width;
        v = ramp->phase;
        break;
    }
    return (long)v;                 /* SANE/68881 fintrz: truncate toward zero -- NO interpolation */
}

pascal OSErr effect(ModParamsPtr mi, GluePtr glue, ModSettingsHandle *prefs)
{
    FlangerSettingsHandle settings = (FlangerSettingsHandle)*prefs;
    long delayBytes, widthBytes, periodBytes, depthBytes;   /* via the 0x6046-equivalent bytes-conversion */
    long flangePct, dryPct, minV, maxV;
    Boolean feedbackOn, envFollowerOn, louderLonger;
    short modulation, frameSize, t, channel;
    OSErr progressResult = kModNoError, result = kModCancel;

    UseResFile(mi->refNum);
    /* progressString = STR 128 "Flanging..."; ModSetupProgress(mi,&t,progressString); canCancel = false always */

    delayBytes  = ConvertUnitExtendedToBytes(&(*settings)->delay,  mi->rate, mi->bps);
    widthBytes  = ConvertUnitExtendedToBytes(&(*settings)->width,  mi->rate, mi->bps);
    periodBytes = ConvertUnitExtendedToBytes(&(*settings)->period, mi->rate, mi->bps);
    depthBytes  = ConvertUnitExtendedToBytes(&(*settings)->depth,  mi->rate, mi->bps);
    flangePct   = clampl((*settings)->flangePercent, 0, 100);  dryPct = 100 - flangePct;
    feedbackOn    = (*settings)->feedback != 0;
    modulation    = (*settings)->modulation;
    envFollowerOn = (*settings)->envelopeFollower != 0;
    louderLonger  = (*settings)->louderMeansLonger == 1;
    (*glue->ModGetSampleValueLimits)(mi->bps, &minV, &maxV);
    frameSize = mi->bps/8 + (mi->bps%8 != 0);

    for (channel = mi->firstSelCh-1; channel < mi->lastSelCh; channel++)
    {
        unsigned long start = mi->selSt, end = mi->selEnd;
        unsigned long channelSize = (*mi->hands)[channel].size;
        if (start > channelSize) start = channelSize;
        if (end   > channelSize) end   = channelSize;
        start -= start % frameSize;  end -= end % frameSize;
        start += delayBytes + widthBytes;          /* seed region: unmodified delay-line history */
        if (end <= start) continue;

        Handle channelHand = (*mi->hands)[channel].chan;
        HLock(channelHand);
        Ptr chanBase = *channelHand;

        long refPeak = AbsPeak(mi, glue, chanBase, /* last <=1024 bytes of selection */
                                max(end - 1024, start), end);
        long n = 0;                                 /* LFO sample-phase counter */
        RampState ramp = { delayBytes, +1 };          /* Triangular/Sawtooth start at the centerline */

        /* feedbackOn selects sweep direction: forward here shown; backward is the mirror
           image (a2 decrements, loop bound is start+delayBytes+widthBytes) and, critically,
           can only ever read pre-existing history since every read trails the write cursor. */
        Ptr a2 = chanBase + start;
        Ptr loopEnd = chanBase + end;
        long chunkPos = 0;  long chunkPeak = refPeak;

        for (; a2 < loopEnd; a2 += feedbackOn ? 1 : -1 /* see note above */, n++, chunkPos++)
        {
            if (chunkPos == 1024) {                  /* re-scan every 1024 samples */
                chunkPeak = AbsPeak(mi, glue, chanBase, /* next 1024-byte block */ a2, a2 + 1024);
                chunkPos = 0;  refPeak = /* previous chunkPeak becomes the new low end of interp */ chunkPeak;
            }
            long dry = GetSample8(a2);

            long d3 = LfoOffsetBytes(modulation, (double)n, (double)periodBytes,
                                      (double)delayBytes, (double)widthBytes, &ramp);

            if (envFollowerOn) {
                double interpPeak = refPeak + (double)(chunkPeak - refPeak) * chunkPos / 1024.0;
                long adj = (long)(interpPeak * depthBytes / maxV);
                d3 += louderLonger ? adj : -adj;
            }
            if (d3 < 0) d3 = 0;

            long wet = GetSample8(a2 - d3);          /* single nearest-integer tap -- no interpolation */

            long out = dry * dryPct / 100 + wet * flangePct / 100;   /* plain truncating integer math */
            if (out < minV) out = minV; else if (out > maxV) out = maxV;
            SetSample8(a2, out);                       /* replace, in place -- this IS the feedback path
                                                            when sweeping forward: a future a2-d3 will read
                                                            this very value back */
            if (t && (progressResult = (*glue->ModShowProgress)(n, /* total */ (end-start)/frameSize, &t, false)))
                break;
        }
        HUnlock(channelHand);
        result = kModNoError;
        if (progressResult) break;
    }
    /* mi->selEnd is never modified: Flanger never extends the selection. */
    (*glue->ModCloseProgress)();
    return result;
}
```

Other entry points: `settings()` (file offset `0x44d2`) drives the rich 27-item DITL (Flange%, Feedback checkbox, Delay/Width/Period unit-dependent fields sharing `CNTL 128 "Units"`, a Sinusoidal/Triangular/Sawtooth radio group inside picture frame `PICT 1100`, an Envelope Follower checkbox + Depth field + Longer/Shorter radio group inside `PICT 1101`) through the host's declarative dialog system; `about()` renders `TEXT 1000` verbatim.

## Porting notes (web / Eurorack)

- **Do not add interpolation.** The temptation, seeing that the delay time is computed in double/extended precision, is to "improve" the read with a linear-interpolated tap. That would materially change the character: the recovered algorithm always truncates to the sample below the true (fractional) delay position, which is exactly what gives classic cheap-DSP flangers their slightly gritty, aliased edge at short delay/high sweep-rate settings (the "Distorto" presets lean into this deliberately). Truncate toward zero, matching `fintrz`/SANE round-to-zero, not `floor` (they differ for negative offsets, though the `d3 = max(d3,0)` clamp means negative offsets never actually reach the read in practice).
- **Feedback = direction, not a tap-point rewire.** The cleanest way to port this is exactly like Echo/Delay: one DSP kernel, a `recursive: bool` flag that just flips whether the write cursor advances forward or backward through the buffer. No separate feedback-gain knob exists to reproduce — the wet/dry mix percentage is doing double duty as the implicit feedback gain when Feedback is on.
- **LFO**: implement Sinusoidal with a real `sin()` (table lookup is a legitimate, audibly-transparent substitution for a modern port — nothing here depends on SANE's specific rounding of `sin`), but implement Triangular/Sawtooth as the literal linear accumulators described above (reflect vs. hard-reset) rather than deriving them from `asin`/`fmod` tricks — the reset behavior in particular (an instantaneous jump back to `Delay−Width`, not a smooth wrap) is audible and is part of Sawtooth's identity here.
- **Envelope Follower**: implement as block-peak-with-linear-interpolation, not a one-pole smoother and not RMS. The 1024-sample block size and the "peak of the last ≤1024 bytes of the selection" reference point are specific, recovered constants worth preserving for a faithful port; a continuously-updated peak/RMS follower would react differently (faster and smoother) than what shipped.
- **Setp presets are byte-count-based, not unit-based** — porting them faithfully means either (a) baking in the millisecond table above and re-deriving byte counts at whatever the target sample rate is (recommended — this is what a user would expect), or (b) reproducing the original's quirk of applying the *same absolute byte count* regardless of the current document's rate, which silently changes the real-world timing when a preset is loaded onto non-Standard-rate audio. Point (b) is a legitimate piece of the original's character if bit-exact behavior against the original binary is the goal.
- **Seed region**: the first `Delay+Width` bytes of the selection are always left untouched (needed as read-only history for the LFO's maximum reach) — same "boundary samples survive" philosophy as Smooth.
- Non-cancellable progress, consistent with every other disassembled module in this project, despite Flanger being by a wide margin the most expensive one to run.
