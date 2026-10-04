# Sample Hose & Iterative Crossfade — recovery (68k disassembly)

**Source:** `EFCT` 0 from `Effects Folder/Sample Hose` (12009 bytes, v1.0.3) and `Effects Folder/Iterative Crossfade` (3300 bytes, v1.0.2), both ©1995 Michael Norris, group *Mike's Modules*. Disassembled with capstone (M68K/040) via `tools/rez.py`; SANE `FP68K`/`Elems68K` opcodes decoded by hand. These were the last two effects flagged as worth byte-verifying in SPEC §15 step 1, on the hypothesis that they reuse **Granular Synthesis**'s machinery (`disassembly/Granular-Synthesis.md`).

**Verdict — the hypothesis was half right, and one finding is a genuine first:**

1. **Sample Hose's Rate (pitch) parameter uses real LINEAR INTERPOLATION** — `out = a + frac·(b − a)` in double precision, reading **two adjacent source samples**. This is the **only interpolating DSP found anywhere in this project so far**. Every Ricci module (Flanger, Echo/Delay) and the host's own resampler (`Host-Resamplers.md`) compute a fractional position and then *throw the fraction away* by truncating. Norris — a third-party author — did it properly.
2. **Shared machinery confirmed:** Sample Hose reuses Granular's exact `rand()` (the ANSI-C LCG) and a real `pow()` for the Curvature tween — so the documented tween formula stands.
3. **Iterative Crossfade shares nothing** — no RNG, no transcendentals, no tween (it has only two parameters). It is a plain **linear crossfade**, and it ships **no FPU build** at all, consistent with needing only add/sub/mul/div.
4. **Both zero-pad rather than skip** past the end of a short channel — note this differs from Select Peak, which *skips* channels shorter than the scan position (`Select-Silence-Peak.md`).

---

## Sample Hose (`EFCT` 0, 12009 B)

Symbol map (THINK C embeds debugger names inline after each function; the offset is where the name sits, i.e. the function's end): `EFFECT` @0x22d8 · `SETTINGS` @0x293c · `GETMODSETTINGS` @0x2a92 · `ABOUT` @0x2d9a. Everything after 0x2a92 that is unnamed is linked C library code. Math budget: **304 `FP68K`**, **1 `Elems68K`**.

### Rate — linear interpolation (`effect()`, ~0x1c04–0x1fd6)

Per output sample, all in SANE **double** (`FP68K` format `0x08xx`):

```
pos   = pos * rateFactor              ; 0x1c28 FMUL  (rate as a fraction, 100% = unity)
idx   = FTINTX(pos)                   ; 0x1c62 truncate toward zero  -> d3
frac  = pos − (double)idx             ; 0x1cb4 FSUB                  -> the fractional part
a     = src[idx]                      ; 8-bit: eori.w #$80 @0x1e58 / wide: GetSample16 @0x2da2
a2   += frameSize                     ; 0x1e50 / 0x1ecc  -- step to the NEXT sample
b     = src[idx+1]
out   = a + frac·(b − a)              ; 0x1f54 FSUB(b−a) → 0x1f70 FMUL(·frac) → 0x1f8c FADD(+a)
out   = out · gain                    ; 0x1fc2 FMUL
```

The `b − a` / `· frac` / `+ a` triplet at `0x1f4c–0x1f92`, fed by two reads one `frameSize` apart, is unambiguous: this is textbook lerp, not a drop-sample read. Both the 8-bit inline path and the wide `GetSample16` path (`0x1e90`, `0x1ed0`) fetch the pair.

Rate is a **percentage** (dialog: 50 = octave down, 200 = octave up), so `rateFactor = Rate/100` and the interpolation is what makes the pitch shift smooth rather than aliased.

### Curvature tween — a real `pow()`

The tween `v(n) = ((n/l)^(c/100))·(vf − vi) + vi` (shared with Granular Synthesis, SPEC §8 item 13) is implemented with the C library `pow()` at ~0x2c20, which wraps SANE's **`Elems68K` opcode `0x8012` = `FXPWRY` (x^y)** at **0x2c3e** — the module's only transcendental. It carries proper domain-error handling (`move.w #$21, -$7ff0(a4)` = errno into the A4 globals, result zeroed). The `FSUB`/`FMUL`/`FADD` triplets at `0x01f6–0x022e` and `0x0264–0x029c` are the `(vf − vi)·t + vi` half. **The documented tween formula is confirmed.**

### `rand()` — identical to Granular Synthesis (@0x2aa2)

```
seed = seed·0x41C64E6D + 0x3039        ; 1103515245, 12345 — ANSI-C rand()
return (seed >> 16) & 0x7FFF           ; 15-bit, 0…32767
```

Byte-for-byte the same routine as Granular's, but **statically linked here** (Granular reaches it through its runtime-loaded library resource — see `Granular-Synthesis.md`). The seed lives in the A4 globals at `-$8000(a4)`. This drives all five *Variance* sub-parameters, so Sample Hose's randomness is exactly reproducible.

### Factory defaults (`GetModSettings` @0x2948)

Settings handle = **106 bytes (0x6a)**, `NewHandle`d and filled with defaults when no saved prefs exist:

| offset | default | reading |
|---|---|---|
| `$20` | 222000 (`0x36330`) | **New Length** in bytes (≈10 s at 22254.5454 Hz) |
| `$24`,`$28` | 100, 100 | a value pair defaulting to 100% (Rate initial/final) |
| `$34`,`$38` | 100, 100 | a second 100% value pair (Gain initial/final) |
| `$44`,`$48` | 50, 50 | **Pan initial/final = 50 = centre** |
| `$10`,`$14` | 10000, 10000 | Period initial/final |
| `$54`,`$58`,`$5c`,`$60`,`$64` | 100 ×5 | **the five Curvatures = 100 = linear** |
| `$68` | 1 (short) | Stereo flag |
| all others (`$0`,`$4`,`$8`,`$c`,`$18`,`$1c`,`$2c`,`$30`,`$3c`,`$40`,`$4c`,`$50`) | 0 | the Variances and Bite (0 = whole sample) |

The five-curvatures-at-100 and Pan-at-50 groupings corroborate the documented "5 parameter types × 5 sub-params" layout (Rate, Gain, Pan, Bite, Period).

### Settings-struct mapping and field types — proven from the dialog descriptor table

`effect()` copies the 106-byte struct into locals (0x2330–0x2436), then builds the dialog's item-descriptor array (0x2446–0x27b8). Each descriptor is a type word followed by a pointer to the local it edits, in dialog order **Initial Value, Initial Variance, Final Value, Final Variance, Curvature**. That order pins every struct offset to its field, replacing the earlier inference:

| parameter | Initial Value | Initial Variance | Final Value | Final Variance | Curvature | field type of the four value/variance fields |
|---|---|---|---|---|---|---|
| Rate | `$24` | `$2c` | `$28` | `$30` | `$54` | 9 (integer) |
| Gain | `$34` | `$3c` | `$38` | `$40` | `$58` | 9 (integer) |
| Pan | `$44` | `$4c` | `$48` | `$50` | `$5c` | 9 (integer) |
| Bite | `$00` | `$08` | `$04` | `$0c` | `$64` | **7 (unit-dependent)** |
| Period | `$10` | `$18` | `$14` | `$1c` | `$60` | **7 (unit-dependent)** |

Also: New Length `$20` is type 7; the units popup is type 6 (local `-$6c`); Stereo `$68` is type 2 (checkbox); every Curvature is type 9. (Item types follow `ModSetups.h`: 2 checkBox, 6 unitsPopup, 7 editFieldUnitDependent, 9 editFieldNumerical.) The DITL itself cannot show this: all its edit fields are plain `0x10` text items.

Consequences:

- **Bite and Period, and their Variances, follow the units popup**, as the Read Me says ("expressed in the units displayed on the unit popup"). They are stored in bytes like New Length, which is why the defaults (Period 10000, New Length 222000) read as the same numbers in *Samples* at 8-bit mono.
- **Variance is in its value's own unit**, not a percentage: Rate/Gain/Pan variances are percentage points (the DITL puts a `%` after them), Bite/Period variances are in the popup's units (the DITL puts no `%` there). The Read Me's example (Rate 100, variance 20 → between 80 and 120) gives the absolute ± form. The exact random draw (`base + (2·rand/RAND_MAX − 1)·variance`) is still the port's reading rather than traced instruction by instruction.
- The three `%` columns and the curvature `%` in the dialog are the DITL's static-text labels, not part of the stored value.

*Caveat:* the offset→parameter mapping is now proven from descriptor order. What remains unrecovered is the **spray scheduling** in `effect()`'s main loop (see SPEC §14 item 21).

---

## Iterative Crossfade (`EFCT` 0, 3300 B)

Symbol map: `EFFECT` @0x81a · `SETTINGS` @0x972 · `GETMODSETTINGS` @0x9da · `ABOUT` @0xbb2. Math budget: **21 `FP68K`**, **0 `Elems68K`**, **no `EFCF`** (no FPU build shipped) — this module needs nothing but add/sub/mul/div, which is why Norris didn't bother.

About text: *"To run this module you must first have two channels selected. This module then creates a new channel, which contains iterations of a gradual crossfade between the two selected samples."* Dialog (DITL 128): **Number of Iterations**, **Silence Length** + units popup.

### The crossfade (0x0596–0x077a) — linear

Per output sample, in SANE **double**:

```
A = ch1[i]                            ; 8-bit inline eori.w #$80 @0x059c / GetSample16 @0xbba
    if i >= len(ch1):  A = 0          ; 0x05ca  moveq #0,d7      -- ZERO-PADS
B = ch2[i]                            ; 8-bit @0x05b8 / GetSample16 @0x0602
    if i >= len(ch2):  B = 0          ; 0x05aa / 0x05ee  suba.l a2,a2

r   = pos / len                       ; 0x066e FDIV   (d4 / a3)
out = A·(1 − r) + B·r                 ; 0x06de FSUB(1−r) · 0x0720 FMUL(A·(1−r))
                                      ; 0x06c0 FMUL(B·r) · 0x0728 FADD
out = FTINTX(out)                     ; 0x074e  truncate toward zero
out = clamp(out, min, max)            ; 0x0766 / 0x0770
```

So `r` sweeps 0→1 across each fade: at `r=0` the output is entirely **A** (first selected channel), at `r=1` entirely **B** (second). The `1.0` constant is the SANE extended `0x3fff8000…` at 0x06c6. Result is **truncated, not rounded** — the house habit — then clamped to the bit-depth's sample limits.

**Short-channel behaviour:** a channel that has run out contributes **digital zero**, not omission. So crossfading a short sound against a long one fades into/out of silence rather than rescaling — worth reproducing, and the opposite of Select Peak's skip-the-short-channel rule.

New length is `n·(l+s) − s` per the docs (`[DOC]`, not re-derived here); zero Silence Length on short samples gives the quasi-granular behaviour the read-me mentions.

---

## Reproduction

```c
// Sample Hose — Rate (the only interpolating path in the app)
pos  = pos * (Rate/100.0);
idx  = (long)pos;                    // truncate toward zero
frac = pos - (double)idx;
out  = src[idx] + frac * (src[idx+1] - src[idx]);   // LINEAR INTERPOLATION
out *= gain;

// Iterative Crossfade
double r = (double)pos / (double)len;
double A = (i < lenA) ? ch1[i] : 0;   // zero-pad, don't skip
double B = (i < lenB) ? ch2[i] : 0;
out = clamp((long)(A*(1.0-r) + B*r), min, max);   // truncate, then clamp
```

## Provenance / confidence

- **CONFIRMED:** Sample Hose's linear interpolation (two reads one `frameSize` apart + the `b−a`/`·frac`/`+a` triplet), its `FXPWRY`-based `pow()`, and its `rand()` LCG (byte-identical to Granular's); Iterative Crossfade's `A·(1−r) + B·r`, its truncate-then-clamp, and its zero-padding of exhausted channels. All read directly from the instructions, with the SANE operand order (`src` pushed before `dst`) applied consistently and cross-checked against Granular Synthesis, where the same conventions produced a formula that matches an independently-recovered published equation.
- **PROVEN (added later):** Sample Hose's full settings-struct mapping and field types, from the dialog descriptor table (above).
- **INFERRED, not proven:** the `r = pos/len` orientation in Iterative Crossfade rests on the operand order plus the semantic (fade A→B) rather than on tracing the loop counters to the dialog fields.
- Neither module was oracle-verified in an emulator.
