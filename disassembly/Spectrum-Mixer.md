# Spectrum window & Mixer strip semantics (68k disassembly)

**Source:** main application `CODE` segments, disassembled with capstone (M68K/040) via `tools/rez.py` + `tools/code.py`. The Special menu (`MENU 134`) items **"Show Spectrum…"** (item 1) and **"Mixer…"** (item 3) are both enabled (`enableFlags = 0xfffffeeb`) and wired through the command dispatcher (`CODE 2 @ 0x38ae` jump table) directly to real window-opening code: Show Spectrum → `CODE 9 @ 0x08a6`, Mixer → `CODE 9 @ 0x04c2`.

**Verdict — answered directly, and it overturns the premise of this item:**

> **Both the Mixer and the Spectrum analyzer are unfinished feature *shells* in 0.9.2 — not working features.** This is corroborated by the app's own **Read Me**, whose "Future improvements" section lists, for the *next* release: *"an Undo command (of course), a Mixer, a Spectrum analyzer, native PowerPC…"*. The menus and window/control resources exist and the windows open, but the core function of each is absent or stubbed. There are therefore **no meaningful DSP "parameters" to recover for the Spectrum, and no functional "strip semantics" for the Mixer** — the honest finding is the incompleteness itself, documented precisely below.

Key hard evidence:

- **No FFT anywhere in the app.** Zero `Elems68K` (`$A9EC`) calls in the entire binary, and zero `FP68K` (`$A9EB`) calls in the Spectrum's segment (`CODE 9`). An FFT/spectrum needs trig (twiddle factors) and magnitude/log scaling; none of that math exists near the Spectrum.
- **The Mixer's "Mix" action is a 4-instruction stub.** `CODE 9 @ 0x0796` = `link a6,#0 / moveq #0,d0 / unlk a6 / rts` — it does nothing and returns 0. It is called by the Mixer opener exactly when the user clicks **Mix**.
- **The Spectrum never touches sample data.** Its whole draw path (`CODE 3 @ 0x4b00–0x5300` and `CODE 9`) contains **no sample-decode** (`eori.b #$80`) and essentially no plotting — only frame/axis QuickDraw. Its only control is an **OK** button.

Both windows are **modal**: each runs its own self-contained `WaitNextEvent` loop (`CODE 9 @ 0x05ac` for the Mixer, a near-identical copy at `CODE 9 @ 0x091e` for the Spectrum), blocking the rest of the app until closed.

## Spectrum window (`Show Spectrum…`)

- **Resource:** `WIND 136`, rect `(50,10,320,490)` = **480 × 270 px**, untitled. **Window kind = 135 (`0x87`)**.
- **Opener** (`CODE 9 @ 0x08a6`): requires the front window to be a sound document (`windowKind == 0x80`), creates `WIND 136` via the creator at `CODE 3 @ 0x513e`, links the source document into the spectrum record at offset `+4`, then runs the modal loop `CODE 9 @ 0x091e`.
- **Controls:** exactly one — **CNTL 500 = "OK"** (standard pushbutton, `procID 8`), placed by the creator (`… move.w #$1f4 (500) … _GetNewControl`).
- **Drawing:** the creator sets the window font/size (`TextFont 1`, `TextSize 9`) and the shared window-frame drawer (`CODE 3 @ 0x4d72`, window-kind case `0x87`) paints only the border/axis lines and a playback-cursor line (`CODE 9 @ 0x084a`). **No frequency data, no waveform, no magnitude bars are ever computed or drawn.** It is an empty, framed placeholder window with an OK button.
- **Conclusion:** the Spectrum "parameters" (FFT size, window function, linear/log axes, dB scaling) that this item asked for **do not exist** — the analyzer was never implemented in 0.9.2.

## Mixer (`Mixer…`)

- **Resource:** `WIND 132`, rect `(50,10,320,490)` = **480 × 270 px**, untitled. **Window kind = 131 (`0x83`)**. (Note: `WIND 132` is the window; `DLOG/DITL 132` is a *different* resource — the Preferences dialog — despite sharing the ID.)
- **Opener** (`CODE 9 @ 0x04c2`): requires a sound-document front window, creates `WIND 132` via `CODE 3 @ 0x3d3a`, runs the modal loop `CODE 9 @ 0x05ac`, and **on Mix (loop result 0) calls the stub `0x0796`**; on Cancel (result `0xff80`) it just closes. Either way, no audio is produced.
- **Window sizing:** the creator reads the source document's channel count (`doc+0xd2`) and sizes the window width to **`channelCount × 128 + 136` px** (`d7 = numChannels << 7 + 0x88`), i.e. **one 128-px-wide strip per channel** — capping the visible strip count to what fits. This is the "per-channel strip" layout the SPEC anticipated.
- **Controls created** (once each, from `CNTL` templates):
  | control | resource | kind | purpose |
  |---|---|---|---|
  | volume fader | **CNTL 200** | `procID 16` (CDEF 1, custom), 16 × 90 px vertical | channel level fader |
  | **Mix** button | **CNTL 201** | pushbutton, 50 × 16 | commit (→ stub) |
  | **Cancel** button | **CNTL 202** | pushbutton, 50 × 16 | dismiss |
  | **"Mix to:"** popup | **CNTL 203** | popup (`procID 1016`) → **MENU 158** | destination |
  - **MENU 158 "Mix to:"** = **`{ "New Document", "New Track" }`** — confirms the SPEC's guess exactly.
- **Interaction that *does* work** (`CODE 9 @ 0x0004`, content handler for kind `0x83`): `FindControl`/`TrackControl` on the fader (thumb-drag via action proc `A5+0xa52`) and on the popup; the **Mix** button (`0x024c`) sets the loop result to `0` and the **Cancel** button (`0x025c`) sets it to `0xff80`, each exiting the modal loop.
- **What does *not* work:** the actual mixdown. The only place it could happen — the post-loop Mix handler `0x0796` — is a do-nothing stub. The fader values are never applied to any audio, and no new document/track is ever created. The creator also instantiates only the single fader template plus the three global controls (no completed per-strip fader/label array), consistent with an abandoned in-progress UI.
- **Conclusion:** the Mixer is a live but **non-operational shell**: draggable fader, working Mix/Cancel/"Mix to:" chrome, sized for N channel strips, but **Mix performs no mix**.

## SPEC corrections this item forces

1. §3 "Other windows": **Mixer (WIND 132)** and **Spectrum (WIND 136)** should be marked **unfinished/non-functional in 0.9.2** (present in the build, listed as future features in the Read Me), not as working windows to reproduce behaviorally.
2. §5/§Special-menu: **"Mixer…" has no ⌘M shortcut** in `MENU 134` (item 3, `cmdKey = 0`) — the SPEC's "Mixer… ⌘M" is wrong.
3. Item 10's framing ("Spectrum window parameters; Mixer strip semantics") resolves to: *Spectrum has no implemented analysis; Mixer strip = one 128-px vertical-fader-per-channel shell with a New Document/New Track destination popup and a stubbed Mix.* A faithful prototype may either omit both (matching 0.9.2) or implement them as the obvious finished versions — but should not claim to be reproducing 0.9.2 behavior if it makes them work.

## Provenance / confidence

- **CONFIRMED.** Menu enable-state and dispatch read directly (`MENU 134`, `CODE 2 @ 0x38ae`); both openers, both modal loops, the content/button handlers, and the `0x0796` stub disassembled in full; window kinds, control resources (CNTL 200/201/202/203/500), and MENU 158 read from the resource fork; absence of FFT established by zero `Elems68K` app-wide and zero `FP68K`/sample-decode in the Spectrum path; independently corroborated by the shipping Read Me's "Future improvements" list.
- Not determined (immaterial to the verdict): the fader's exact value→gain mapping, since it is never consumed. Left unspecified deliberately.
