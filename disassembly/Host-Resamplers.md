# Host resamplers — `ModResampleSoundRate` & `ModResampleSoundBits` (68k disassembly)

**Source:** main application `CODE` segments, disassembled with capstone (M68K/040) via `tools/rez.py` + `tools/code.py`. These are the two host `GlueRec` services that the **Resample Rate**, **Upsample Bits** and **Downsample Bits** modules delegate to (they contain no conversion code of their own — see `disassembly/Resample-Rate.md`, `disassembly/Downsample-Bits.md`). This note recovers what the host actually does.

**Verdict — answered directly, and it resolves the last `[ASM]` DSP question:**

- **Sample-rate conversion is drop-sample / zero-order-hold — NOT interpolation.** `ModResampleSoundRate` steps a SANE-extended floating-point position through the source by `oldFrameSize · oldRate/newRate` **bytes** per output sample, **truncates it toward zero** (`FTINTX`), and copies that one source frame to the output. There is no linear (or any) interpolation between neighbouring samples — upsampling duplicates samples, downsampling drops them. (This settles the "drop/duplicate vs. linear" question left open in SPEC §8 item 25: **drop/duplicate**.)
- **Bit-depth conversion is plain truncation / zero-extension — no rounding, no dither.** `ModResampleSoundBits` keeps the most-significant bytes when narrowing (or masks off the low `oldBps−newBps` bits within the same byte count) and shifts left / zero-pads when widening. Any dither or noise-shaping is the *module's* pre-pass (`Downsample-Bits.md`), never the host's.
- **These are bespoke, hand-written routines, not the OS SoundConverter.** The app *does* lean on the Mac Sound Manager's SoundConverter (`_SoundDispatch`, `'NONE'` format) for **playback, recording and format I/O** — but the two document resamplers are self-contained integer/SANE code in `CODE 4`. (An early pass of this investigation mis-attributed resampling to SoundConverter after finding it in the playback path; the actual `GlueRec` targets, located via the glue table below, are separate.)

## How the glue targets were located

The `GlueRec` handed to modules is a **runtime-built A5 global** (not in the 390-byte `DATA` image). Its builder is **`CODE 8 @ 0x0012`**, which writes 41 function pointers to consecutive A5 slots (`base = A5−0x2ba`, so `glueVers` at −0x2ba, first pointer at −0x2b8). Each slot is `lea <jt_off>(a5),a0 ; move.l a0,<slot>(a5)`. Reading the two resample slots (struct offsets `0x96` / `0x9a`, i.e. A5 −0x224 / −0x220):

| GlueRec field | slot | `lea` jt-off | resolves to |
|---|---|---|---|
| `ModResampleSoundBits` | A5−0x224 | `0x942` | JT 292 → **CODE 8 @ 0x3672** (wrapper) |
| `ModResampleSoundRate` | A5−0x220 | `0x98a` | JT 301 → **CODE 8 @ 0x39b6** (wrapper) |

Both are thin **A5-swapping wrappers** (`movea.l 0x904.w,a5` = load the app's `CurrentA5` because they're entered from a module's world), which is itself confirmation these are the module-facing callbacks. Each wrapper `NewHandle`s a 0xE2-byte internal doc record, copies the caller's `ModParamsRec` fields into it (rate `+0x24`→`+0x2c`, bps `+0x28`→`+0xd0`, numChans `+0x2a`→`+0xd2`, sel/loop `+0x14/18/1c/20`→`+0x1c/20/24/28`), calls the real worker, then copies the results back and disposes the record. Workers:

- rate worker = `A5+0x53a` → **CODE 4 @ 0x42be**
- bits worker = `A5+0x52a` → **CODE 4 @ 0x3d44**

## `ModResampleSoundRate` worker — `CODE 4 @ 0x42be`

Whole document, per channel. Setup (`0x42be–0x438c`):

1. `oldFrameSize = ceil(oldBps/8)` (`d4`); `oldRate = doc.rate` (Fixed 16.16, `+0x2c`); `newRate = arg` (Fixed), **clamped to ≥ 0x10000 (1.0 Hz)**.
2. Both rates → SANE **extended** via the Fixed→extended helper `A5+0x7ba`.
3. **step** (extended) `= oldFrameSize · oldRate / newRate` — the source advance, in *bytes*, per output sample (`FL2X` of `oldFrameSize`, `FMUL` by `oldRate`, `FDIV` by `newRate`). Branch: `newRate ≤ oldRate` → downsample path, else upsample path.
4. New channel byte-size `= round(oldSize · newRate / oldRate)`, applied with `SetHandleSize` (`0x44e6` / `0x450e`).

Per-output-sample kernel (representative, `0x4540–0x4734`), for each channel:

- Maintain an **extended-float position accumulator** (initialised at the channel end; the pass walks the buffer **backward** so reads never collide with earlier writes — the same in-place-safety trick as Echo/Delay/Flanger).
- Each step: `idx = FTINTX(position)` — **round-toward-zero (truncate)** to an integer byte offset (`FP68K op 0x16`), converted to a long (`op 0x3010`).
- Copy the whole source frame at `idx`: 8-bit path (`frameSize==1`) does `*dst = src[idx]` (a single `move.b`); multi-byte path copies `frameSize` bytes. **One source frame, copied verbatim — no blend of neighbours.**
- Advance the accumulator by ∓`step` (`FP68K op 0x02`) and the destination pointer by one frame; repeat.

So: **zero-order-hold resampling.** Upsampling (`newRate>oldRate`, step < frameSize) repeats each source sample across several outputs; downsampling (step > frameSize) skips sources. Sub-sample precision is carried in the float accumulator but discarded at read time by the truncation — audibly this is the classic "resampler aliasing / stairstep," which is exactly the retro character to preserve.

## `ModResampleSoundBits` worker — `CODE 4 @ 0x3d44`

Per channel. `oldFrameSize = ceil(oldBps/8)` (`d5`), `newFrameSize = ceil(newBps/8)` (`d7`). Three cases:

1. **Narrowing bytes** (`newFrameSize < oldFrameSize`, e.g. 16→8 bit) — `0x3da8–0x3e5a`: for each sample, keep the **top `newFrameSize` bytes** of the old sample (source pointer strides by `oldFrameSize`), dropping the low bytes = **truncating the LSBs**. The 8-bit destination re-applies `XOR 0x80` (offset-binary), consistent with the sample model in SPEC §7.4. Then `SetHandleSize` shrinks the buffer.
2. **Same byte count, fewer bits** (`newFrameSize == oldFrameSize`, `newBps < oldBps`, e.g. 16→12) — `0x3e62–0x3f62`: build a bit-mask of the top `newBps` bits and **AND off the low `oldBps−newBps` bits** in place; buffer size unchanged.
3. **Widening** (`newFrameSize > oldFrameSize`, e.g. 8→16) — grows the handle and **shifts each sample left / zero-pads the new low bytes** (8-bit source un-offsets via `XOR 0x80` first).

**No rounding, no dither, no noise-shaping** anywhere in the host path — it is a straight bit-truncate (narrowing) / zero-extend (widening). This is exactly why the **Downsample Bits** *module* bothers to inject auto-scaled white noise into every sample *before* calling this service (see `Downsample-Bits.md`): the host will otherwise just chop the low bits.

## SPEC corrections / resolutions

- §8 item 25 (**Resample Rate**): the interpolation method is **drop/duplicate (zero-order hold), truncating**, not linear — recovered here; the `[ASM]` tag can close.
- §8 items 7/34 (**Downsample/Upsample Bits**): host truncates/zero-extends bytes (or masks bits) with no dither — the module owns all dithering.
- §7.6 (`GlueRec`): `ModResampleSoundRate`/`ModResampleSoundBits` are real host DSP (`CODE 4 @ 0x42be` / `0x3d44` behind A5-swap wrappers `CODE 8 @ 0x39b6` / `0x3672`), **not** delegated to SoundConverter. This was the last unrecovered host DSP.

## Provenance / confidence

- **CONFIRMED.** Glue slots read directly from the located `GlueRec` builder (`CODE 8 @ 0x0012`); both wrappers and both workers disassembled in full; the rate kernel's single-frame `move.b`/frame-copy (no second tap) and `FTINTX` truncation, and the bits kernel's top-byte-keep / low-bit-mask / shift-pad cases, are read directly from the instructions. Byte-exact rounding of the *new size* and the up-vs-down path boundary are read from the code but not oracle-verified against the original in an emulator; the algorithmic character (drop-sample, truncate, no dither) is certain.
