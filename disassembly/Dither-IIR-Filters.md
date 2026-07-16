# Dither & the IIR filters — recovery (68k disassembly)

**Source:** `EFCT` 128 from `Effects Folder/Dither` (3736 B), `High Pass IIR` (2450 B), `Low Pass IIR` (2444 B) and `Shelving` (2496 B) — all Ricci modules, disassembled with capstone (M68K/040) via `tools/rez.py`, with SANE `FP68K` opcodes decoded by hand. These were the last items flagged in SPEC §15 step 1.

**Verdict — all four match their documented equations, but each carries a quantisation or edge behaviour the docs never mention:**

1. **Dither adds** `(amplitude · Random()) / 32767` per sample using the **Toolbox `_Random` trap** — the same RNG as Noise, *not* the ANSI-C LCG the Norris modules use. Unlike Noise it **adds** rather than replaces. Its percentage mode computes `amplitude = (pct · 2^(bps−1)) / 100`, which reproduces the About text's own examples **exactly** (10% → 12 points at 8-bit, 3276 at 16-bit).
2. **The three IIR filters are pure-integer.** Their *only* floating-point is in `settings()`, used to scale the coefficients for storage. Coefficients are stored as `trunc(coef × 10)` — **quantised to one decimal place, truncated toward zero** — and each filter term is then computed with an independent **truncating integer division by 10**. In a recursive filter that per-term truncation feeds back.
3. **All three IIR filters seed the delay line with the first sample of the selection, not zero**, and leave that first sample unmodified — the same edge habit as Smooth.
4. **No stability validation anywhere** — confirmed. Unstable coefficients are the user's privilege, as the SPEC suspected.

---

## Dither (`EFCT` 128, 3736 B)

**Zero `FP68K`, zero `Elems68K`** — pure integer. Four `_Random` (`$A861`) sites at `0x3bc`, `0x490`, `0x54a`, `0x61e` = **2 modes × 2 bit-depth paths**, each a separate compiled loop.

Dialog (DITL 128) is **module-drawn**, not the host schema: explicit `OK`/`Cancel` buttons, the label *"Add"*, two radio buttons (`type 6`) and two edit fields — `[__] %` / `[__] points` — then *"of random noise"*.

### The kernel (8-bit path shown, `0x3ac–0x414`)

```
x    = *p XOR 0x80                      ; offset-binary -> signed
r    = _Random()                        ; Toolbox trap $A861, signed word −32768…32767
out  = x + (amplitude · r) / 32767      ; mul32 (0x28) then div32 (0x34), integer
out  = clamp(out, min, max)             ; 0x3e4 / 0x3fc
*p   = out XOR 0x80
```

The `32767` divisor is pushed as `pea $7fff.w` at `0x3c2`. `amplitude · r` goes through THINK C's 32×32 multiply/divide runtime helpers (the 68000 has no `muls.l`), so the division **truncates toward zero**.

This is the same noise-injection shape already documented for **Downsample Bits**' noise-shaping pre-pass (`sample += Random()·scale/32767`, see `Downsample-Bits.md`) — a consistent house pattern, and the reason Dither is the recommended companion to downsampling (its own About text makes that argument).

### The two amplitude modes

The radio flag is tested at `0x36c` (`tst.b -$21(a6)`); non-zero branches to the points path at `0x52a`.

| mode | amplitude | where |
|---|---|---|
| **%** | `(pct · 2^(bps−1)) / 100` — integer, truncating | computed once at `0x374–0x398` into `-$26(a6)` |
| **points** | the entered value, used directly | `-$1e(a6)`, read at `0x55a` |

`2^(bps−1)` is built as `1 << (bps−1)` (`0x37c–0x384`) — i.e. **128** for 8-bit and **32768** for 16-bit, one *more* than the true maximum positive sample (127 / 32767). At 100% that overshoots slightly; the per-sample clamp absorbs it.

**Self-check against the module's own documentation** — the About text says *"10% is roughly the same as 12 points in a 8-bit sound, or 3276 in a 16-bit sound"*:

```
8-bit :  (10 · 2^7)  / 100 = (10 · 128)   / 100 = 12      ✓ exactly
16-bit:  (10 · 2^15) / 100 = (10 · 32768) / 100 = 3276    ✓ exactly (truncated)
```

Both land on the documented numbers exactly, including the truncation — strong corroboration that the formula is read correctly.

---

## High Pass IIR · Low Pass IIR · Shelving

Three near-identical builds of one source (2450 / 2444 / 2496 B) differing essentially in one kernel line. Each has exactly **10 `FP68K` calls and 0 `Elems68K`** — and **every one of them is in `settings()`**, never in `effect()`.

### Coefficient storage — the quantisation the docs omit

`settings()` scales between the dialog's decimal fields and the on-disk longs, using an int16 constant of **10** in all three modules (verified by resolving each `pea (d16,pc)` target — Low Pass `0x90e/0x910`, High Pass `0x912/0x914`, Shelving `0x940/0x942`, all `= 10`):

```
save:  stored_long = FTINTX(coef · 10)      ; 0x846–0x878  FMUL then truncate toward zero
load:  coef        = stored_long / 10       ; 0x79e–0x7bc  FDIV by int16 10
```

So **coefficients have exactly one decimal place of resolution and are truncated, not rounded** — typing `0.55` stores `5` and yields `0.5`. The settings handle is 8 bytes: two longs, `a` at `+0`, `b` at `+4`.

### The kernels — integer, per-term truncation

`effect()` reads the two stored integers as words (`a` at `-$1c(a6)`, `b` at `-$1e(a6)`) and never touches floating point. Each term is `(coef_stored × value) / 10` via the runtime helpers `mul32` (stub `0x28` → `0x80`, three `mulu.w` with swaps) and `div32` (stub `0x34` → `0xf8`), so **each term truncates independently toward zero**:

| module | kernel | site |
|---|---|---|
| **Low Pass IIR** | `y(n) = (A·x(n))/10 + (B·y(n−1))/10` | `add.l d1,d0` @ `0x54a` |
| **High Pass IIR** | `y(n) = (A·x(n))/10 − (B·y(n−1))/10` | `sub.l d0,d1` @ `0x54a` |
| **Shelving** | `y(n) = x(n) + (A·x(n−1))/10 + (B·y(n−1))/10` | `add.l a0,d0` + `add.l d1,d0` @ `0x55c`/`0x55e` |

with `A = trunc(a·10)`, `B = trunc(b·10)` — matching the documented `y(n) = ax(n) ± by(n−1)` and `y(n) = x(n) + ax(n−1) + by(n−1)` exactly. Every sample is then clamped to the bit-depth's min/max.

### Seeding and edges (undocumented, and audible)

```
0x4ee: y(n−1) = first sample of the selection      ; NOT zero
0x50a: (Shelving) x(n−1) = first sample too
0x4fe: cursor = start + 1                          ; the first sample is never written
```

So the delay line starts at the signal's own first value rather than silence — no start-up transient, but also **the first sample of every selection passes through unmodified**, exactly like Smooth's untouched edges. Shelving additionally saves the **original input** as `x(n−1)` (`d7 = a0` at `0x564`, taken from the value read *before* the write), so its feed-forward tap uses pristine input rather than filtered output.

There is **no coefficient range checking anywhere** — enter `b = 2.0` and the filter blows up, sample by sample, clamping into a square wave. That is the intended experience.

## Reproduction

```c
// Dither
long amp = usePercent ? (pct * (1L << (bps-1))) / 100   // 12 @8-bit, 3276 @16-bit for pct=10
                      : points;
long r   = Random();                                     // Toolbox trap: signed word
out = clamp(x + (amp * r) / 32767, min, max);            // ADD, integer, truncating

// IIR filters — A,B are the stored ints = trunc(coef*10)
long yprev = firstSample, xprev = firstSample;           // seeded, not zero
for (i = start+1; i < end; i++) {                        // first sample untouched
    long x = get(i);
    long y = LOWPASS  ?  (A*x)/10 + (B*yprev)/10
           : HIGHPASS ?  (A*x)/10 - (B*yprev)/10
           : /*SHELF*/   x + (A*xprev)/10 + (B*yprev)/10;
    y = clamp(y, min, max);
    set(i, y);  yprev = y;  xprev = x;   // xprev = the ORIGINAL input
}
```

## Provenance / confidence

- **CONFIRMED:** Dither's `_Random`-based add, its `/32767` scaling and both amplitude modes (the `%` formula independently validated against the About text's own 12 / 3276 figures); the `×10` coefficient scaling in all three IIR `settings()` routines (each `pea (d16,pc)` target resolved to the literal `10`); all three IIR kernels including the `add`/`sub`/two-add distinction; the first-sample seeding and the `start+1` write cursor; the per-sample clamps; the absence of any range validation. All read directly from the instructions.
- **INFERRED:** that `-$1c(a6)`/`-$1e(a6)` are coefficients `a`/`b` respectively rests on the operand order matching each module's published equation, not on tracing the fields to their DITL items. Nothing was oracle-verified in an emulator.
