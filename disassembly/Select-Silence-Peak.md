# Select Silence & Select Peak — full algorithm recovery (68k disassembly)

**Source:** main application `CODE` segments (`SoundEffects 0.9.2`, creator `√SFx`), disassembled with capstone (M68K/040) using `tools/rez.py` + `tools/code.py` (the `CODE 0` jump-table resolver built for `Easter-Eggs.md`). Both commands live in `CODE 4`: **Select Peak** at file offset `0x2b90` (jump-table index 157, `A5+0x508`), its peak-finder helper at `0x3090` (JT 159), and **Select Silence** at `0x61f8` (JT 175, `A5+0x598`). Dispatched from the Selection-menu handler in `CODE 2` (menu-item jump table at `CODE 2 @ 0x38d0`; MENU 131 item 8 → Select Peak, item 9 → Select Silence).

**Verdict — the headline questions, answered directly:**

1. **Select Silence threshold: there is none — silence is *exact* digital zero.** 8-bit: the byte equals `0x80` (offset-binary zero). 9–32-bit: **every byte of the frame equals `0x00`**. No amplitude tolerance, no RMS window, no hysteresis. One sample of value ±1 terminates the "silence".
2. **Select Silence needs an insertion point, not a selection.** If `selStart != selEnd` the command silently does nothing (no dialog, no beep). From the caret it scans backward and forward through exactly-zero bytes in **each selected channel**, then **intersects** the per-channel silent ranges (start = max of starts, end = min of ends).
3. **Select Silence leaves a one-frame margin of silence *unselected* at each silent/non-silent boundary** — the selection is the *interior* of the silent run. The margin is dropped at a buffer edge (silence running to the start or end of the channel selects all the way to the edge).
4. **Select Peak margin math: margin is symmetric and clamped, applied to a one-frame peak selection.** The peak frame is selected (`[peakPos, peakPos+frameSize)`), then `selStart = max(0, peakPos − marginBytes)` and `selEnd = min(maxSelChannelLen, peakPos + frameSize + marginBytes)`. Margin is converted from the dialog's units field to **bytes** and is identical on both sides.
5. **The peak metric is the absolute value of the *sum across selected channels*** at each frame position (not a per-channel max): 8-bit samples are `XOR 0x80`-decoded and summed in a 16-bit register; multi-byte samples are big-endian sign-extended (first ≤4 bytes of the frame) and summed in 32 bits. **Ties: the earliest (leftmost) position wins** (strictly-greater comparison).
6. **SPEC correction — option-key Select Peak is *not* "repeat last settings".** The option key (dispatcher passes `modifiers & optionKey`, `$800`, tested in `CODE 2 @ 0x33d6`) skips the dialog and runs with **margin = 0 and "the Selection" scoping** — the last-used margin is not stored anywhere (only the units *popup code* is remembered, per-document, at doc+`0xdc`; the margin field always opens as `0`).

## Select Peak (`CODE 4 @ 0x2b90`) — behavior, precisely

### Preamble

`FrontWindow` → require `windowKind == 0x80` (a sound-document window) → `GetWRefCon` = document handle `a3`. Bail out via error path if the document has no audio (`maxAllChannelLen == 0`, helper `A5+0x9a`): `GetWTitle` + alert, exactly the "can only be applied to an existing sound" convention. Frame size `d5 = ceil(bps/8)` from doc+`0xd0`.

### The dialog (DLOG/DITL 141), item-accurate

| item | what | code behavior |
|---|---|---|
| 1 / 2 | OK / Cancel | modal loop exits only on 1 or 2 (`SetDialogDefaultItem/CancelItem/TracksCursor` via `$AA68` selectors `0x304/0x305/0x306`) |
| 7 | radio "the Sound" | sets scope flag `d3 = 1` |
| 8 | radio "the Selection" | sets `d3 = 0` |
| 10 | margin edit field | opens as `"0"`; **re-parsed to bytes on every item hit** via the app's string→bytes unit converter (`A5+0x82a`, args: frameSize, sound handle, &text, &marginBytes, unitsCode) |
| 11 | units popup (CNTL 136) | on change, `unitsCode = ctlValue − 1` (doc+`0xdc` remembered code is the initial value) and the field text is rewritten in the new units via the bytes→string converter (`A5+0x822`) |

- **Initial radio:** "the Sound" is pre-selected iff the current selection is empty **or** spans the full document (selStart == 0 and selEnd == maxAllChannelLen) *and* all channels are selected (`firstSelChan == 1 && lastSelChan == numChannels && no function-key single-channel focus (doc+0xda == 0)`); otherwise "the Selection". `SelIText(dialog, item 10, 0, 0x7fff)` focuses the margin field.
- **Modal filter:** chosen by units code — `< 2` (samples/bytes) uses the integer-only keystroke filter (`A5+0x5ca`), `≥ 2` (seconds/ms/meters/feet) the decimal filter (`A5+0x5d2`).
- **Option key held when the menu item was chosen:** the entire dialog is skipped (`tst.w $8(a6)` at `0x2c18` → `0x2f20`), which explicitly sets `d3 = 0`, `marginBytes = 0`, `itemHit = OK`.

### Scoping (what range gets searched)

- **"the Sound"** (`d3=1`): `selStart = 0`, `selEnd = maxAllChannelLen` (`A5+0x9a` = max size over **all** channels), channel range forced to `1..numChannels` (doc+`0xd4` = 1, doc+`0xd6` = doc+`0xd2`).
- **"the Selection"** (`d3=0`):
  - if a single-channel focus is active (doc+`0xda` ≠ 0, the function-key soloing): channel range = that channel only, `selStart = 0`, `selEnd = maxSelChannelLen` (`A5+0xa2` = max size over the **selected** channel range);
  - else if the selection is empty (`selStart == selEnd`): `selStart = 0`, `selEnd = maxSelChannelLen`;
  - else: the existing selection and channel range are used as-is.

### The peak search (`CODE 4 @ 0x3090`)

For each position `pos` from `selStart` to `selEnd` (exclusive), stepping **1 byte** when `frameSize == 1`, else `frameSize` bytes:

1. `metric = 0`. For each channel in `firstSelChan-1 .. lastSelChan-1` (8-byte channel records: `+0` sample Handle, `+4` sizeBytes): **skip the channel if `pos >= channel.sizeBytes`** (unequal channel lengths handled by omission, not padding).
2. 8-bit: `metric += (int8)(byte XOR 0x80)` (16-bit accumulator). Multi-byte: read `min(frameSize, 4)` big-endian bytes into the top of a 32-bit register, `asr.l` by `8·(4 − bytesRead)` to sign-extend, accumulate in 32 bits (frames wider than 4 bytes contribute only their first 4 bytes).
3. `metric = |metric|`; if `metric > best` (strictly), `best = metric`, `*peakPos = pos` — so the **first** occurrence of the maximum wins.

After the multi-byte loop the result is frame-aligned: `peakPos -= peakPos mod frameSize` (unsigned 32-bit modulo helper `A5+0x52`). The function returns the peak metric in `d0` (Select Peak ignores it).

### The margin (the item-7 question)

```
selStart = peakPos                          ; select exactly one frame
selEnd   = peakPos + frameSize
if marginBytes < selStart: selStart -= marginBytes   else: selStart = 0
room = maxSelChannelLen − selEnd
if room > marginBytes:     selEnd  += marginBytes    else: selEnd = maxSelChannelLen
```

(`0x2fe2–0x302e`.) Margin identical on both sides, clamped independently at each end; no re-centering when clamped. Then port is set, selection redrawn, window updated (`A5+0x3f2` with message 0x106).

## Select Silence (`CODE 4 @ 0x61f8`) — behavior, precisely

### Preconditions

FrontWindow must be a sound document; `GetWRefCon` = doc. **`selStart == selEnd` is required** (`0x6234`) — with a non-empty selection the command returns immediately, changing nothing. The caret byte offset is saved (`caret`), then the selection is provisionally set to `selStart = 0`, `selEnd = maxSelChannelLen` (these are the intersection accumulators). `frameSize = ceil(bps/8)`.

### Per selected channel (`firstSelChan .. lastSelChan`)

Let `w = caret`, adjusted `w -= frameSize` if the caret sits exactly at `channel.sizeBytes`. Channels with `sizeBytes <= w` are skipped. `buf = **sampleHandle`, `size = channel.sizeBytes`.

**Forward scan (right edge, `0x62da–0x6352`):**
- 8-bit: advance `q` from `w` while `buf[q] == 0x80` and `q < size`.
- multi-byte: advance `q` while `buf[q] == 0x00` and `q < size`, then align down: `q -= q mod frameSize` (signed modulo helper `A5+0x62`) — a frame is silent only if **all** its bytes are zero.
- `end_chan = q − frameSize` — i.e. the last **whole silent frame is excluded**: one frame of margin. Clamped to ≥ `w` (caret not in silence → collapses); if the end-of-channel caret adjustment fired, `frameSize` is added back.
- **Buffer-edge case:** if the scan reached the last frame (`end_chan == size − frameSize`) and that frame is silent, `end_chan = size` — no margin when silence runs to the channel end. (8-bit tests `buf[size−1] == 0x80`. The multi-byte check at `0x636c` walks `frameSize` bytes **downward** from the last frame's first byte — `buf[size−frameSize−i]`, `i = 0..frameSize−1` — instead of upward across the last frame: an off-by-sign quirk in the original that makes the "extend to end" test straddle the previous frame's tail for `frameSize > 1`. Reproduce faithfully or not; it only affects the final frame.)

**Backward scan (left edge, `0x638a–0x63be`):**
- 8-bit: decrement `p` from `w` while `buf[p] == 0x80` and `p ≥ 0` — exits at the last non-silent byte before the run (or `−1` past the start).
- multi-byte: decrement `p` while `buf[p] == 0x00`, then `p += p mod frameSize` (signed mod, so a `p` of −1 is unchanged).
- `start_chan = p + 2·frameSize`. For 8-bit this is `lastNonSilentByte + 2`: the first silent byte is left **out** — one byte of margin, symmetric with the forward side. (For `frameSize > 1` the exact landing depends on which byte of the last non-silent frame is nonzero — `P_frameStart + 2·(p mod frameSize) + 2·frameSize` — the original does not fully normalize this; for typical 16-bit audio whose non-silent frame ends in a nonzero low byte it lands two frames after that frame's start, i.e. one silent frame of margin.)
- **Inversion guard (`0x63c0`):** if `end_chan < start_chan` (caret wasn't inside silence) both are reset to `caret` — the command is a no-op.
- **Caret clamp (`0x63ca`):** if `start_chan > caret + frameSize`, `start_chan = caret` — the left margin is never allowed to push the selection start past the caret.
- **Buffer-edge case (`0x63d8`):** if `start_chan == frameSize` (the scan ran off the start of the buffer), frame 0 is tested; if silent, `start_chan = 0` — no margin when silence runs to the channel start.

**Intersection across channels (`0x640e–0x6426`):** `selStart = max(selStart, start_chan)`, `selEnd = min(selEnd, end_chan)` — the final selection is the region silent in **every** selected channel (within the margins above).

**Finish (`0x6432`):** if the result is an empty selection equal to the original caret, return with no redraw; otherwise redraw the selection and update the window (same `A5+0x3f2` update call as Select Peak) — including the "silence found but empty after intersection" case, which visibly moves the caret.

## Fidelity checklist for the prototype

- Select Silence: exact-zero test (`0x80` byte / all-zero frame), insertion-point-only, per-channel scan + cross-channel intersection, one-frame interior margin at each silent boundary, margin dropped at channel edges, caret clamp, silent no-op when the caret isn't in silence.
- Select Peak: sum-across-channels |peak| metric with first-max-wins, channels shorter than the position silently skipped, one-frame base selection ± margin bytes clamped at `0` and `maxSelChannelLen`, margin field parsed in the 6-unit system, units code (not the value) remembered per document, option key = skip dialog with margin 0 and Selection scoping.
- SPEC §4's "(option = repeat last settings)" annotation on Select Peak must be corrected.

## Provenance / confidence

- **CONFIRMED** for everything stated as behavior above: single self-contained routines, all constants read directly from the code, dialog items cross-checked against DITL 141, menu-item mapping cross-checked against MENU 131 + the dispatch jump table at `CODE 2 @ 0x38d0`, and the option-key flag traced to the `$800` modifier test at `CODE 2 @ 0x33d6`.
- The two `frameSize > 1` boundary quirks in Select Silence (the backward-scan landing formula and the downward last-frame test) are read directly from the instructions but have no oracle run behind them; they affect at most one frame at each boundary. Everything else, including all 8-bit behavior, is exact.
