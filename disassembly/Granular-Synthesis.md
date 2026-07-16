# Granular Synthesis — grain envelope & RNG recovery (68k disassembly)

**Source:** `EFCT` 0 (6040 bytes) from `Effects Folder/Granular Synthesis`, disassembled with capstone (M68K/040), cross-checked against `EFCF` 0 (5179 bytes, the 68881 build) using `tools/fpu68881.py` (capstone's M68K mode does not decode `0xF2xx` coprocessor ops, so this project now carries a small hybrid 68k+68881 disassembler). Module v1.0, ©1995 Michael Norris, group *Mike's Modules*. Per this project's convention `EFCT` is authoritative for control flow; `EFCF` was used to name the floating-point operations unambiguously — and here it reads almost like source.

**Verdict — the grain envelope, answered exactly. For once, the documentation is telling the truth.**

With `x` = offset into the grain (`0 ≤ x ≤ n`), `n` = grain length, and `f` = **Fade Level ÷ 100** (`0…1`):

```
t    = 5x/n − 2.5
P    = (1 − e^(−t²))³
y    = (1 − P) + P·f          ← what the code literally computes
```

which is exactly the linear blend

```
E(x) = 1 − (1 − e^(−(5x/n − 2.5)²))³      ← Roads' "truncated curve"
y    = E(x)·(1 − f) + f
```

- **Fade Level 0% (`f=0`) → `y = E(x)`** — the full truncated-curve envelope.
- **Fade Level 100% (`f=1`) → `y = 1`** — no enveloping at all.
- Intermediate values lift the curve linearly toward unity (at `f=0.2`, the grain edges sit at ≈0.205 instead of ≈0.006).

This **matches the module's Read Me verbatim** — including its claim that "with a value of 100%, the formula y = 1 is used, and with the value of 0%, the formula below is used". Unlike **Smooth** (where the About text described a 3-point average and the binary did a center-excluding 2-point one), Norris's documentation is accurate. Notable, because the formula was only ever published as a *picture*.

### Where the published formula actually was

The Read Me's formula is an **embedded image**, which is why it was never transcribed into the SPEC (`[ASM]`). But `PICT 1001` in the Read Me's resource fork is not a bitmap — it carries the original **Expressionist equation source** as text:

```
equal[char[y],plus[num[1,"1"],minus[power[id[plus[num[1,"1"],minus[over[num[1,"1"],
power[char[e],power[id[plus[over[times[num[5,"5"],char[x]],char[n]],
minus[num[2.5,"2.5"]]]],num[2,"2"]]]]]]],num[3,"3"]]]]]
```

which parses to `y = 1 − (1 − 1/e^((5x/n − 2.5)²))³` — i.e. `E(x)` above. The disassembly below independently confirms the shipped code computes precisely this, then blends it with Fade Level.

## The envelope routine — `EFCT @ 0x088c` (`EFCF @ ~0x0640`)

Pascal calling convention. `EFCT` args: `$8(a6)` = result pointer (extended), `$c(a6)` = **x**, `$10(a6)` = **n**, `$14(a6)` = **f**. `EFCF` returns in `fp0` so it has no result pointer and every arg shifts down by 4 (`$8`=x, `$c`=n, `$10`=f) — the two builds agree field-for-field.

Recovered constants (SANE extended in `EFCT`, IEEE double in `EFCF`):

| value | `EFCT` extended @ | `EFCF` double |
|---|---|---|
| 2.5 | `0x4000a000…` @ 0x892 | `0x4004000000000000` |
| 5.0 | `0x4001a000…` @ 0x8d2 | (folded into `5x`) |
| 1.0 | `0x3fff8000…` @ 0x956/0x974/0x9e6 | `0x3ff0000000000000` |
| 2.0 (pow exponent) | `0x4000`/`0x80000000` @ 0x920 | `0x4000000000000000` |
| 3.0 (pow exponent) | `0x4000`/`0xc0000000` @ 0x9a6 | `0x4008000000000000` |

The `EFCF` body, which is self-documenting:

```
fdiv     fp1, fp4                  ; fp4 = 5x / n
fsub.d   #2.5, fp4                 ; fp4 = t = 5x/n − 2.5
fmove.d  #2.0, fp3
push fp3 ; push fp4 ; jsr pow()    ; fp0 = t²
fmove    fp0, fp5 ; push fp5
jsr      exp()                     ; fp0 = e^(t²)
fmove    fp0, fp4
fmove.d  #1.0, fp0 ; fdiv fp4, fp0 ; fp0 = 1/e^(t²)  = e^(−t²)
fmove.d  #1.0, fp5 ; fsub fp0, fp5 ; fp5 = 1 − e^(−t²)
fmove.d  #3.0, fp3
push fp3 ; push fp5 ; jsr pow()    ; fp0 = P = (1 − e^(−t²))³
fmove    fp0, fp4
fmul.x   $10(a6), fp0              ; fp0 = P · f            ← Fade Level
fmove.d  #1.0, fp5 ; fsub fp4, fp5 ; fp5 = 1 − P
fadd     fp0, fp5                  ; fp5 = (1 − P) + P·f    ← RESULT
```

The `EFCT` build is the same sequence in SANE `FP68K` calls (`FMUL`/`FDIV`/`FSUB`/`FADD`, `FZ2X` to widen the integer `x`/`n`), with `pow` and `exp` reached through the stub table described below.

### Why `EFCT` appears to contain no transcendentals

A scan of `EFCT` finds **zero `Elems68K` (`$A9EC`)** traps and only basic `FP68K` ops (`FADD/FSUB/FMUL/FDIV/FCMP/FTINTX/FZ2X/FX2Z`) — which initially reads as "this module cannot compute `e^x`, so the documented formula can't be what ships." That inference is **wrong**, and the trap is worth recording:

`EFCT @ 0x171e` is a **stub table** of 12-byte entries, each `jsr $104.l` followed by inline `(selector, offset)` params. The dispatcher at `EFCT @ 0x104` reads those inline params off the return address, calls **`_GetResource`** with a type/base-ID held in the module's A4 globals, `MoveHHi`/`HLock`s the result, runs it through the decompressor at `0x38`, and jumps in. The module's C math library (`exp`, `pow`) therefore lives in a **separately loaded, compressed code resource** — it is simply not present in the `EFCT` bytes. `0x174e` (1-arg) = `exp`, `0x175a` (2-arg) = `pow`.

The `EFCF` build settles it directly: it links the library in locally, and its envelope calls `exp()` at **`EFCF @ 0x6ac`** (C entry `0x1108` → raw `fetox` at `0xff6`) between the two `pow()` calls at `0x69e` and `0x6ea`. Both builds compile from one source, so `g = exp` is confirmed, not inferred. (Methodological note: the callers are reached by **absolute** `jsr` (`4eb9`), not PC-relative — a PC-relative-only caller scan finds nothing and invites exactly the wrong conclusion.)

## How the envelope is applied — `effect()` @ `EFCT 0x0a46`

Per grain, per sample (loop around `0x13fa–0x14ff`):

1. **Read source sample**: 8-bit (`frameSize==1`) inline as `move.b (a3,d3.l),d4 ; ext.w ; eori.w #$80,d4` (offset-binary → signed, matching `ModGetSetSamples.h`); 9–32-bit via the `GetSample16` equivalent stub at `0x171e`.
2. Widen the integer sample to extended (`FP68K FZ2X`, format int32).
3. `jsr 0x088c` → envelope `y` for this `x` within the grain (`d3` = x, `d6`/frame args = n, plus `f`).
4. **`FP68K FMUL`** — `out = sample · y` (`EFCT 0x147c`).
5. Clamp against the min/max sample limits (`FP68K FCMP` at `0x14b0` / `0x14fe`, substituting the limit on overflow) and write back.

So the envelope is applied as a **floating-point per-sample gain**, then clamped — not the integer-only convention of the Ricci modules (Norris shipped FPU and PowerPC builds precisely because this module is float-heavy).

## Bonus recoveries

- **The `Variance` RNG is the textbook C `rand()`** — `EFCT @ 0x0fd6` (called from `EFCF 0x56c`/`0x5ec`):
  ```
  seed = seed·0x41C64E6D + 0x3039        ; 1103515245, 12345
  return (seed >> 16) & 0x7FFF           ; 15-bit result, 0…32767
  ```
  This is the ANSI-C LCG verbatim, so grain-length jitter is reproducible exactly. The seed lives in the module's small A4 global block (globals base = code + `0x176c`, ~44 bytes).
- **The Curvature tween** (`EFCT @ 0x05a4`, `EFCF` calls `pow()` at `0x53a`) uses a genuine `pow()`, consistent with the documented shared formula `v(n) = ((n/l)^(c/100))·(vf − vi) + vi` (curvature 100 = linear, 0 = convex, 200 = concave). Its result is truncated with `FTINTX` (`0x13ca`) before use as a byte count — the same truncate-don't-round habit seen throughout this app.

## Reproduction

```c
// x = 0..n offset within grain, n = grain length, f = FadeLevel/100 (0..1)
double t = 5.0*x/n - 2.5;
double P = pow(1.0 - exp(-t*t), 3.0);
double y = (1.0 - P) + P*f;          // == E(x)*(1-f) + f
out = clamp(sample * y, min, max);
```

Grain edges sit at `y≈0.0058` and the centre at exactly `1.0` when `f=0`; the curve is flatter-topped than a Gaussian (that's the cubing), which is precisely the Gaussian/rectangle compromise Roads describes.

## Provenance / confidence

- **CONFIRMED.** Both builds disassembled and agree field-for-field; every constant (2.5, 5.0, 1.0, 2.0, 3.0) read directly from the code in two independent encodings (SANE extended and IEEE double); `exp`/`pow` call sites proven in `EFCF`; the result matches the Read Me's published Expressionist formula, recovered independently from `PICT 1001`. The three-way agreement (code ⇄ 68881 build ⇄ published equation) makes this the best-corroborated effect in the project.
- Not oracle-verified in an emulator; the argument order of `pow(x,y)` is taken from the constant pushed (2.0 / 3.0) being the exponent, which is unambiguous given the formula it reproduces.
