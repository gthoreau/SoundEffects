# Edit & Selection menu commands — Leave Silence, Shift Sound, Select Exactly, Set/Select Loop, Extend (68k disassembly)

**Source:** main application `CODE` segments (`SoundEffects 0.9.2`), disassembled with capstone (M68K/040) through `tools/rez.py` + `tools/code.py`. Dispatch: the menu handler in `CODE 2 @ 0x33b4` switches on the menu ID; **Edit** (MENU 130) jumps through the item table at `CODE 2 @ 0x38f2`, **Selection** (MENU 131) through `CODE 2 @ 0x38d0`.

| Menu item | Handler | JT |
|---|---|---|
| Edit 8 Leave Silence… | `CODE 12 @ 0x0162` (dialog `@ 0x0540`), called with the option-key flag | 355 |
| Edit 9 Shift Sound… | `CODE 12 @ 0x0958`, worker `@ 0x1042` | 353 |
| Selection 4 Extend to Beginning | `CODE 4 @ 0x584e` | 168 |
| Selection 5 Extend to End | `CODE 4 @ 0x592a` | 169 |
| Selection 7 Select Exactly… | `CODE 4 @ 0x2514` | 155 |
| Selection 11 Set Loop | `CODE 4 @ 0x1f10` | 153 |
| Selection 12 Select Loop | `CODE 4 @ 0x1f62` | 154 |

Document record offsets used below (the app's own sound record, not the SDK's `ModParamsRec`): `+0x1c` selStart, `+0x20` selEnd, `+0x24` loopStart, `+0x28` loopEnd, `+0x2c` rate (Fixed), `+0xd0` bps, `+0xd2` numChannels, `+0xd4` firstSelChan, `+0xd6` lastSelChan, `+0xda` function-key single-channel focus, `+0xdc` units popup code. `A5+0x9a` = max size over **all** channels, `A5+0xa2` = max size over the **selected** channels. `CODE 4 @ 0x6496` (JT 176) = silence-out `(doc, ch, ch, start, end)`.

## The remembered duration

Leave Silence, Shift Sound and Select Exactly share **one** global, `A5−0x5a8` (an extended float, **seconds**). Each dialog opens with `bytes = trunc(seconds × rate × frameSize)`, snapped down to a frame, shown in the document's units; OK stores `bytes / (rate × frameSize)` back. Nothing initialises it, so the first dialog of a session opens at 0. Select Peak's margin does not use it (it always opens at 0). The units popup code is per document (`+0xdc`).

## Leave Silence… (`CODE 12 @ 0x0162`)

**Dialog (DLOG/DITL 142).** Items: 7 field, 8 units popup (CNTL 136), 10 "the Sound", 11 "the Selected Channels". If every channel is already selected (`firstSel == 1 && lastSel == numChannels`, or the single-channel focus), item 11 is **disabled** (`HiliteControl 255`) and item 10 checked; otherwise item 11 is checked. OK writes the scope **into the document's channel range**: "the Sound" → `firstSel = 1, lastSel = numChannels`. **Option key** skips the dialog: the remembered duration, the current channel range.

**Algorithm** (over `firstSel..lastSel`, `N` = bytes):

1. `run = maxAllChannelLen`, `audible = false`. For each channel: measure the leading silent run — 8-bit: bytes `== 0x80`; wider: bytes `== 0x00`, then `run −= run mod frameSize`. If `run_ch < run && run_ch != size`: `run = run_ch`, `audible = true`. Else if `run_ch == size` (silent end to end, or empty): **`SetHandleSize(0)`** — the channel is emptied.
2. If nothing was audible: `run = 0`, `selStart = selEnd = 0`.
3. If `run < N`: memory check (`(N − run) × channels + 0x1400` against `FreeMem`, alert and bail if short).
4. For each channel: empty → grow to `N` and silence `[0, N)`. Else if `run < N` → grow by `N − run`, `BlockMove(p + run → p + N, size − run)`, silence `[0, N)`. Else if `run > N` → `BlockMove(p + run → p + N, size − run)`, shrink by `run − N` (the bytes before `N` are left as they were — silent). Equal → nothing.
5. Selection: if `run < N`: `if (run < selEnd) { selStart += N − run; selEnd += N − run } else selStart = selEnd = N`. Else, with `d = run − N`: `selStart = d < selStart ? selStart − d : 0`, likewise `selEnd`.

So the **shortest** lead-in among the audible channels sets one shift that all the selected channels share: they stay aligned, and a channel that started later keeps its extra silence. This supersedes SPEC §14 #15 (the TS reading normalised each channel separately and padded per channel).

## Shift Sound… (`CODE 12 @ 0x0958`, worker `@ 0x1042`)

**Scope — no user choice; the selection decides** (`@ 0x0ae2`), and STR# 139 builds the sentence `"Shift " + scope + " by:"`:

- single-channel focus → "channel number N", that channel, `[0, its size)`;
- (caret, or `selStart == 0 && selEnd == maxAll`) **and** every channel selected → "the whole sound", all channels, `[0, maxAll)`;
- (caret, or `selStart == 0 && selEnd == maxSel`) → "channel number N" / "channels N to M" (selected), `[0, maxSel)`;
- otherwise → "the selection", `[selStart, selEnd)` on the selected channels.

**Dialog (DITL 139).** 7 field, 8 units, 9 "Shift Left", 10 "Shift Right" — **Shift Right** is checked on open. A zero amount does nothing. Errors: an empty sound gets the "can only be applied to an existing sound" alert before the dialog.

**Worker**, params `{amount A, start, end, firstCh, lastCh, right}`:

- **Left.** If `start < A`: `g = A − start`; memory check; then **every** non-empty channel of the document — scoped or not — grows by `g` with silence prepended (`BlockMove` up, silence `[0, g)`); `start += g`, `end += g`, `selStart += g`, `selEnd += g`. Then for each scoped channel: `s = min(start, size)`, `e = min(end, size)`, `len = e − s`; `BlockMove(p + s → p + s − A, len)`; if `e == size`: shrink by `len > A ? A : len`; else if `len > A`: silence `[e − A, e)`; else silence `[s, e)`. Finally `selStart −= A`, `selEnd −= A`.
- **Right.** Memory check for the growth; for each scoped channel with `s != e`: if `e + A > size`, grow to `e + A` and silence the new tail; `BlockMove(p + s → p + s + A, e − s)`; silence `[s, min(s + A, e))`. Finally `selStart += A`, `selEnd += A`.

Consequences worth knowing: lengths are **not** preserved (Right grows a channel the range reaches the end of; Left shortens one); Right overwrites the `A` bytes after the range; **Shift Left on the whole sound is a net no-op** (the pad is moved straight back), and on some channels only it leaves the *unscoped* channels padded — i.e. later than the shifted ones. This supersedes SPEC §14 #14 (the TS reading slid within the buffer, preserving length).

## Select Exactly… (`CODE 4 @ 0x2514`)

**Dialog (DITL 140).** 7 field, 8 units, 10 "Before", 11 "Centered on", 12 "After" — **After** is checked on open; item 9 is "of sound" and the last line is STR# 142: "the selection." when there is one, else "the insertion point.". OK, with `N` bytes and `maxSel` = max size over the selected channels:

- **Before:** `selEnd = selStart`; `selStart = N < selStart ? selStart − N : 0`.
- **Centered on:** `selStart = (selStart + selEnd) >> 1`, snapped down to a frame; `selEnd = selStart`; `selStart = N/2 > selStart ? 0 : selStart − N/2`; then `selEnd = (maxSel − selStart > N) ? selStart + N : maxSel`.
- **After:** `selStart = selEnd`; `selEnd = (maxSel − selEnd > N) ? selEnd + N : maxSel`.

Both ends are then snapped down to a frame. The channel range is untouched. A clamped end is **not** compensated (no re-centring, no shifting the other end) — so "exactly" is at most N.

## Set Loop / Select Loop / Extend

- **Set Loop** (`@ 0x1f10`): `loopStart = selStart`, `loopEnd = selEnd`. No clamp, no check (a caret sets an empty loop).
- **Select Loop** (`@ 0x1f62`): bails if the sound is empty; `selStart = loopStart`, `selEnd = loopEnd`, and **every channel** is selected (`firstSel = 1`, `lastSel = numChannels`).
- **Extend to Beginning** (`@ 0x584e`): bails if `maxSel == 0`; `selStart = 0`. Channel range untouched.
- **Extend to End** (`@ 0x592a`): bails if `maxSel == 0`; `selEnd = maxSel` (the selected channels' longest end, not the sound's).

## Confidence

**CONFIRMED** — each routine disassembled in full; dialog items cross-checked against DITL 139/140/142 and STR# 139/142; record offsets cross-checked (Set Loop's `+0x1c → +0x24` copy fixes selStart/loopStart; the SDK's `ModParamsRec` order agrees). Not traced: the bodies of the unit converters (`CODE 7 @ 0x4544 / 0x47ac`, already ported as `se::bytesToUnit / unitToBytes`) and the memory-check alert texts.
