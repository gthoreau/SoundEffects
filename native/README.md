# SoundEffect — native (JUCE)

The native rebuild of SoundEffects 0.9.2: a **standalone app** and a **DAW plugin**
(VST3 / AU / AAX) from one codebase — the VCV Rack Pro delivery model. Loadable in
a host as either an audio effect or an instrument.

The TypeScript project in the repo root is the **reference spec** (byte-accurate
model, effects, file formats, tests). This `native/` tree ports the engine to C++
and wraps it in JUCE. Where behaviour is subtle, the C++ cites the TS file it came
from.

## Layout

```
native/
  CMakeLists.txt            JUCE via FetchContent → Standalone + VST3 + AU
  Source/
    PluginProcessor.{h,cpp} the ONLY JUCE/host boundary; owns the engine
    PluginEditor.{h,cpp}    hosts the canvas + mixer + toolbar (Mosaic, zoom reset,
                            and the zoom / grid / pan readout); New/Open live at the
                            top of the mixer panel
    ui/                     JUCE UI (message thread); reads engine UI-safe snapshots
      CanvasComponent       the board: the Window grid, zoom/pan, tint palette
      WindowComponent       one System-7 sound Window: info strip, track lanes
                            (waveform + L/R indicators), transport wired to engine,
                            corner grow box
      MixerComponent        main mixer: New/Open, then one HORIZONTAL strip per
                            Window (name · mute · one trough carrying both the dB
                            fader and the VU meter), ≤250px wide, scrolling
    engine/                 host-agnostic core — ZERO JUCE includes
      SampleFormat.h        packed offset-binary / big-endian sample access
      Track.h               one lane: independent buffer + output channel + volume
      SoundDocument.{h,cpp}  the document model; add/move/trash tracks
      Mixer.{h,cpp}         per-frame sum, L/R routing, solo/mute, volume steps
      Transport.{h,cpp}     per-window free-running playhead + loop + Advance(2×)
                            + atomic UI snapshot (uiPlayhead / uiState)
      Import.{h,cpp}        build a document/tracks from decoded float audio
      SpscQueue.h           lock-free single-producer/consumer ring buffer
      Engine.{h,cpp}        multi-window ADDITIVE sum + per-window mixer channel
                            + the lock-free UI↔audio window hand-off
```

## Threading (UI ↔ audio)

The **audio thread** solely owns the *live* Window set. The **message thread**
(UI) never touches it directly — it posts commands (add / remove / transport)
into a lock-free SPSC queue (`SpscQueue`), and reads/writes per-Window mixer
params (`gain`, `muted`) which are atomics. `render()` drains the queue at the
top of each block. Removed Windows are handed BACK to the message thread via a
retirement queue and freed there (`collectRetired()`, driven by a 5 Hz timer in
the processor), so **no allocation or deallocation ever happens on the audio
thread**. All message-thread API must be called from one thread (single
producer); `render()` is the single consumer.

## Vocabulary

Canvas › **Window** › **Tracks** › **Track** › *Audio sample* (with a **Selection
area** and an **Output channel** L/R/L+R) · plus a **Transport** and an
**Audio Output → Mixer channel**. NB the original app and the TS reference call a
lane a "channel"; the rebuild renames that to **Track** and reserves "channel"
for output routing and the mixer.

## The model (what makes it SoundEffect)

- **Independent tracks = rudimentary per-track samplers.** Each track is its own
  buffer, and in a playing window each track loops over ITS OWN length
  independently (`pos % len` per track, not one shared playhead) — so tracks
  phase against each other within a window, and windows phase against each other.
- **Loops.** Loop + no selection → every track loops its own length; loop +
  selection → the selected span loops (selected tracks); no loop → each track
  plays once to its end.
- **Silence vs absence.** Silence = real silent samples (audio: selectable,
  erasable, insertable via Insert Silence), drawn as a SOLID line and part of the
  loop length. Absence = past a track's end, drawn as a DOTTED line, contributes
  nothing.
- **Global additive playback.** `Engine` sums *every open window* at once. The
  infinite canvas is the spatial view of a bank of unsynchronised loopers.
- **Per-window mixer.** Each `Window` has a `gain` + `mute` — the real "Level"
  fader the original only stubbed out — plus a `meter` the audio thread publishes
  (its own post-gain peak, decaying ~-60 dB in 300 ms so the UI may sample it at
  any rate). In the panel the fader and the meter share ONE dB trough on ONE
  scale, so a strip reads out and set-point in a single glance: the fill is what
  the channel is putting out, the cap is where its gain sits, and the tick between
  them is 0 dB — which is also 0 dBFS, so fill past the tick is clipping.
- **Destructive edits, declared rate obeyed even when it lies** — carried over
  from the TS core (edits/effects still to be ported).

## The canvas: grid, size, colour, view

The canvas is an unbounded board. The Windows live in a transparent "world" layer
that carries the view transform, so zoom and pan are one `AffineTransform` and
JUCE maps mouse events through it — every Window keeps working at any zoom.

**It reaches every way out of the origin.** The world's origin — where the grid is
anchored — is at the *centre* of the world layer, not at a corner, and the view
starts with it parked 48px in from the viewport's top-left. So there is as much
board above and to the left of the first Window as below and right of the last
one, and nothing is pinned against an invisible corner.

**The grid.** Layout is a virtual, unbounded grid, on by default. Each Window
holds a cell address (`gridCol`, `gridRow`, on the engine's `Window`) and columns
are *independent stacks*:

- a column has **one width**, and every Window in it is drawn at that width;
- a cell is exactly as tall as **its own** Window, so heights vary freely down a
  column and rows deliberately do **not** line up across columns;
- there is no padding — cell (0, 0) starts at the origin and the Windows touch.

**Both indices may be negative**, and nothing is ever renumbered to 0..n-1. Row 0
sits on the origin line and the negative rows stack *upward* from it, exactly as
the negative columns stack leftward, so the grid grows out of the origin in all
four directions and neither direction shunts the other along: a column appearing
to the left leaves column 0 where it is, and a Window dropped above a column lifts
that column's top rather than shoving everything in it down. `Window::kNoCell`, not
"negative", is what "the canvas hasn't placed this one yet" means.

**A grow box resizes the column, not the Window.** The whole column adopts the
width the drag asks for, so narrowing works exactly like widening — the alternative
("column = its widest member") made a narrowing drag do nothing visible until some
other Window shrank, which read as a broken grip.

Resizing therefore reflows the whole column and shifts the columns beside it.
Dragging a Window re-targets its cell — the column under the dragged corner,
inserted above the first Window whose middle is below it — and the rest reflow
live around the gap it will drop into, the source column closing up at once. Where
it lands decides which way the column grows: **above the top** takes a row further
into the negatives and moves nothing else, **below the bottom** takes a row past
the last, and **in between** makes the rows below it (only) give way. Dragging past
*either* end column pulls the Window out into a new column of its own (except when
it is already alone in that end column, or it would drift outwards for ever). A new
Window opens a fresh column while the row of columns still fits the viewport, and
drops into the shortest column once it doesn't.

**Mosaic** toggles the grid off: Windows stay where they are and drag moves them
freely. Toggling it back on re-packs them.

**Resizing a Window** never scales its chrome. Total height is always
`chrome + numTracks × laneHeight`, so a vertical drag changes the height of one
track lane and the title bar, info strip, transport, L/R output toggles and
up/down/trash controls keep their fixed size. Only the waveforms grow.

**Tint.** Each new Window is painted in one of twelve pastels, chosen at random
but never the same as the one before it, persisted as an index in `Window::tint`.
The palette is explicit hex in `kTints` (even hue spacing, HSL ~ *h*/55%/84%) so a
single entry can be nudged without disturbing the rest. The whole surface takes
the colour; the waveform bed is the same hue washed 72% toward white so the trace
stays readable.

**Highlight.** A Window's border weight carries its state: 1px plain, 2px hovered,
4px active. "Active" means *holding keyboard focus* — the Window that ⌘X/⌘C/⌘V
and Delete actually act on — rather than a separate notion of selection, so the
highlight makes an otherwise invisible state visible. Clicking bare canvas moves
focus to the canvas and clears it. The border is painted last so the highlight is
never partly overdrawn, and it grows inwards, so a thick border never disturbs the
neighbours it touches in the grid.

**Transport.** Five square buttons, centred as a group at the foot of the Window:
record · stop · play/pause · 2× · loop. The symbols are drawn as paths inside one
shared box rather than as text — `●■▶↻` have wildly different metrics at any one
point size, so glyphs cannot be made to match — and the strip's height derives
from the button size (`kBtnSize`), not the other way round. Play and pause are **one toggle** —
`Transport::pause()` already flips playing↔paused, so only a *stopped* transport
calls `play()` (calling it while paused would restart from the top). 2× is a
toggle too, and because `Transport::advance()` starts playback when the transport
isn't rolling, engaging it also starts it; `play()` resets `advanceScale` to 1.0,
so anything that (re)starts playback clears the 2× state with it. The record dot
is red but **not wired up yet** — recording is a later increment.

**View controls.**

| | |
|---|---|
| `Ctrl`/`⌘` + wheel, or pinch | zoom about the pointer, 20%–400% |
| wheel / `⇧`+wheel | scroll the board vertically / horizontally |
| `H` | enter pan mode — hand cursor, drag anywhere pans (Windows stop taking clicks) |
| `V` or `Esc` | leave pan mode |
| `0` | reset zoom, and put the origin back at the viewport's top-left |
| `+` / `-` | zoom about the viewport centre |

## Build

Requires CMake ≥ 3.22, a C++17 compiler, and (first configure) network access to
fetch JUCE. On macOS the Command Line Tools are enough (full Xcode NOT required) —
use the Ninja generator, not `-G Xcode`.

```sh
# one-time tools (macOS/Homebrew): brew install cmake ninja
cd native
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target SoundEffect_Standalone   # or omit --target for all formats
open build/SoundEffect_artefacts/Debug/Standalone/SoundEffect.app
```

Artifacts land under `build/SoundEffect_artefacts/` (Standalone `.app`, `.vst3`,
`.component`). Verified building & running on macOS arm64 (AppleClang, CLT-only,
JUCE 8.0.4) 2026-07-18.

## Status

- **Engine core**: done and verified (see the two checks below).
- **UI/UX increment 1** (this pass): canvas hosting draggable System-7 Windows,
  each drawing its tracks' waveforms + a working transport (play/stop/pause/
  advance/loop) wired to the engine; a per-Window main mixer (dB fader + mute);
  File > Open via JUCE decoders; New; Mosaic. **Builds and runs** (Standalone,
  macOS arm64, JUCE 8.0.4) — multiple windows, per-track waveforms, transport, and
  the per-window mixer all render and work. Engine additions it relies on
  (uiPlayhead/uiState atomics, Import, window-id enumeration) are also verified via
  smoke + ThreadSanitizer.
- **UI/UX increment 2**: interactive tracks + selection + clipboard, on an RCU
  editing foundation (the UI mutates a UI-authoritative doc and publishes an
  immutable snapshot to the audio thread — edits apply mid-loop, no glitch, no
  audio-thread alloc). Add/move/trash track, L/R output toggle, selection drag,
  and ⌘X/⌘C/⌘V. Engine layer verified (ASan/UBSan + ThreadSanitizer incl.
  concurrent edits during playback); UI drive-tested (add/trash track, selection,
  cut/paste with the independent-track shortening all confirmed on real audio).
- **UI/UX increment 3**: the canvas proper — resizable Windows (corner grow box,
  chrome fixed, waveforms stretch), the virtual column grid described above with
  drag-to-cell and live reflow, the 12-pastel tint palette, and zoom/pan
  (`Ctrl`+wheel, pinch, `H`/`V`). **Builds and runs** (Standalone, macOS arm64,
  JUCE 8.0.4); drive-tested end to end — grid packing, column resize (both ways),
  drag between columns and out into a new one either side, drag above a column's
  top so it climbs past the origin line, tints, zoom, pan, and the grid/free toggle
  all confirmed on screen.
- **UI/UX increment 4**: the mixer panel proper — New/Open moved to its head, one
  horizontal strip per Window (name · mute · the combined fader/VU trough), a
  ≤250px column with a scrolling list, and everything in it hand-painted System 7
  (including the scrollbar). Engine side: `Window::meter`. **Builds and runs**
  (Standalone, macOS arm64, JUCE 8.0.4); drive-tested on screen — Open from the
  panel loads a file, the meter fills and falls with playback, dragging a fader
  swaps the name cell for a live dB readout (−∞ … +6.0 dB) and the meter follows
  the new gain, mute inverts the button and empties the meter, double-click
  returns the fader to unity, and the list scrolls once the strips outgrow it.
- **Next increments**: System 7 chrome polish; the effects port; recording;
  byte-accurate AIFF/WAV save; persisting canvas layout across launches (cell,
  size and tint live on the engine's `Window` but are not yet saved to disk).

## Known TODO / not-yet-safe

- **Window lifecycle hand-off: DONE** (lock-free command + retirement queues;
  gain/mute atomics). Verified with ThreadSanitizer (200k-iteration concurrent
  churn, no races) and `leaks` (0 leaks, incl. destructor reclaim).
- **Intra-window edits are NOT yet safe.** The audio thread reads a Window's
  document (track buffers, selection, loop) while playing. Destructive effects,
  selection changes, and add/move/trash *track* while a Window is playing still
  need their own hand-off (same command-queue idea, or a per-Window immutable
  document swap). Editing a stopped Window is fine.
- **`TrackMonitor` uses `std::set`.** Soloing/muting a *track* (not a window)
  mutates a set — allocation. Fine today because it goes through the command
  queue applied on the audio thread only via `play` payloads, but a live
  solo/mute path must avoid `std::set` on the audio thread.
- Plugin format AAX needs the AAX SDK path set; CLAP needs the clap-juce-extension
  wrapper. Only Standalone/VST3/AU are enabled by default.
