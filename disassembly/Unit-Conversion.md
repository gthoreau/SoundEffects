# Unit conversion — Meters/Feet constant & the full unit system (68k disassembly)

**Source:** main application `CODE 7`, disassembled with capstone (M68K/040) via `tools/rez.py` + `tools/code.py`. Two mirror-image routines implement every duration-unit conversion in the app:

- **bytes → display string** (`A5+0x822`, jump-table index 256 → `CODE 7 @ 0x4544`)
- **display string → bytes** (`A5+0x82a`, JT 257 → `CODE 7 @ 0x47ac`)

Both use SANE (`_FP68K`, trap `$A9EB`) for the arithmetic. These are the routines the `type_editFieldUnitDependent` dialog items call (e.g. Select Peak's margin field, every effect module's duration fields via the host glue).

**Verdict — the Meters/Feet constant, answered directly:**

- **Speed of sound = 344 m/s** (meters) and **1128 ft/s** (feet), stored as **plain unsigned 16-bit integers** (`0x0158` = 344, `0x0468` = 1128), *not* high-precision SANE extended values.
- **Milliseconds** use the integer **1000** (`0x03E8`) in the same table.
- The three constants sit as a consecutive `int16` triple **`04 68 01 58 03 e8`** = `{1128, 344, 1000}` immediately after each converter's `rts` — once at `CODE 7 @ 0x47a6` (bytes→string) and once at `CODE 7 @ 0x4a9a` (string→bytes). They appear **nowhere else** in the app binary.
- Conversion is symmetric: bytes→display **multiplies** duration-in-seconds by the constant (`FMUL`, op word `$2004` = FFINT|FMUL, 16-bit integer operand); display→bytes **divides** by it (`FDIV`, op word `$2006` = FFINT|FDIV).

So a duration is treated as the distance sound travels in that time: `meters = seconds · 344`, `feet = seconds · 1128`, and the inverse. The physical length of the *selection/effect* is literally "how far the sound wave moved."

### Consistency note (a real quirk)

344 and 1128 were each rounded to whole integers **independently**, and they are not consistent with each other:

- 344 m/s → 344 × 3.28084 = **1128.6 ft/s**, but the app uses **1128**.
- Equivalently, the app's implied foot = 344/1128 = **0.30496 m**, vs. the true 0.3048 m (+0.05%).

So the meters reading and the feet reading of the *same* duration disagree by ≈0.05%. 344 m/s corresponds to dry air at roughly **21–22 °C** (the "room temperature" speed of sound); it is not the 0 °C 331.3 m/s nor the common textbook 343 m/s. A faithful prototype should hard-code the integers **344** and **1128** and reproduce the tiny inconsistency rather than deriving feet from meters.

## The unit-code dispatch (both routines share it)

Units are selected by the popup's zero-based code (`ModUnits.h`), read from the `unitsCode` argument. The dispatch ladder (`CODE 7 @ 0x455c` forward, `0x47c4` reverse) is:

| code | unit | bytes→display | display→bytes |
|---|---|---|---|
| 0 | **samples** | `bytes → samples` (`A5+0x4a`), print integer | parse integer, `samples → bytes` (`A5+0x42`) |
| 1 | **bytes** | print the byte count directly | parse integer = byte count |
| 2 | **seconds** | `samples / rate`, print real | `value · rate`, round to samples → bytes |
| 3 | **milliseconds** | seconds · **1000** | value / **1000** → seconds |
| 4 | **meters** | seconds · **344** | value / **344** → seconds |
| 5 | **feet** | seconds · **1128** | value / **1128** → seconds |
| <0 or ≥6 | — | no-op (returns unchanged) | no-op |

(3 = milliseconds is independently confirmed by the Flanger presets in `disassembly/Flanger.md`, which all store `unitsCode = 3`.)

### Sample rate is a Fixed 16.16 value

Seconds/ms/meters/feet all need seconds = samples / sampleRate. The rate is passed as a **Fixed (16.16)** number and converted to SANE extended by the helper at `CODE 7 @ 0x11b6`: the integer half (`>>16`) and the fractional half (`& 0xFFFF`, then `FDIV` by the long **65536** at `0x121a`) are summed. This is why the app can represent the "Standard" 22254.5454 Hz rate exactly enough — it carries a 16-bit fraction. `bytesPerSecond = rate · frameSize` where `frameSize = ceil(bps/8)`.

### FP68K operand-format words seen here (for reference)

`$A9EB` operation word = `(sourceFormat << 8) | opcode`. Formats observed: `$00xx` extended(80), `$20xx` integer(16), `$28xx` long(32), `$30xx` comp(64). Opcodes: `04` FMUL, `06` FDIV, `0E` convert-to-extended (FZ2X), `10` convert-from-extended/round, `04`/`16` are the `FMUL`/`FnegX`-class as used. The meters/feet multiply is `$2004` (FMUL by int16) forward and `$2006` (FDIV by int16) reverse.

## Reproduction (web prototype)

```js
const SPEED = { meters: 344, feet: 1128 };   // integers, as in the ROM app
const MS_PER_S = 1000;

// bytes -> displayed value in the chosen unit
function bytesToUnit(bytes, unitCode, rate /*Hz, may be fractional*/, frameSize) {
  const samples = bytes / frameSize;
  switch (unitCode) {
    case 0: return samples;                     // samples
    case 1: return bytes;                        // bytes
    case 2: return samples / rate;               // seconds
    case 3: return (samples / rate) * MS_PER_S;  // milliseconds
    case 4: return (samples / rate) * SPEED.meters; // meters
    case 5: return (samples / rate) * SPEED.feet;   // feet
  }
}
// displayed value -> bytes: invert (divide by the same constant, * rate, round to whole
// samples, * frameSize). Keep meters and feet as separate integer constants — do NOT
// derive feet from meters, or you lose the authentic 0.05% disagreement.
```

## Provenance / confidence

- **CONFIRMED.** Both conversion directions disassembled in full; the three integer constants read directly from the bytes immediately after each routine and cross-checked between the forward (`FMUL`) and reverse (`FDIV`) paths; the constant triple occurs exactly twice in the binary and nowhere else. The unit-code ladder is read directly and item 3 = milliseconds independently agrees with the Flanger preset dump. The Fixed-16.16 rate handling and the 65536 divisor are read directly at `CODE 7 @ 0x11b6`/`0x121a`.
- Effect modules that do their own unit conversion (e.g. Flanger's local `ModConvertToBytes`, per `disassembly/Flanger.md`) embed their own copy of the same integers — the Flanger `EFCT` resource does contain the `int16` 344 — but they were not re-disassembled here; the app-level constant recovered above is authoritative for the host UI.
