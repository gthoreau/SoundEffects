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
                            (waveform + the L / L+R / R output selector), navigator
                            (zoom in / out / whole sound, ◀ ▶, and a well showing
                            the view in blue over the selection in black),
                            transport wired to engine, inline rename, grow box
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
      EngineState.{h,cpp}   the whole canvas as bytes: save / load / host state
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
track lane and the title bar, info strip, transport, output-channel selector and
up/down/trash controls keep their fixed size. Only the waveforms grow.

**Output channel.** Each track carries one three-way selector at the head of its
lane — `L` / `L+R` / `R`, stacked top to bottom, the chosen cell inverted. It is a
selector, not two independent switches, because those are the only three routings
worth having: the old pair of toggles could also be set to *neither*, which is a
silent track dressed up as a routing. (A document that arrives that way still
draws honestly — no cell lit.) **A stereo file routes itself**: two tracks in from
one file ARE the two output channels, so `makeDocumentFromFloat` sends track 1
left and track 2 right rather than folding both to both. Mono, and anything with
more than two tracks, keeps the `L+R` default.

**Renaming.** Double-click a Window's title bar for an inline editor; `Return` or
clicking away commits, `Esc` cancels, and an empty box keeps the old name. The
name is UI-only — the audio thread never reads it — so a rename does NOT go
through `commitEdit()`, which would clone every sample to publish a string. The
mixer strip polls `uiDoc->name` on the editor's timer, since nothing else would
tell it that its channel had been renamed.

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

**Keyboard.** `Space` plays/pauses the ACTIVE Window — the one holding keyboard
focus, which is the one the thick border is already pointing at — through the same
toggle the play button calls, so the two can never disagree. `⌘N` / `⌘O` (`Ctrl`
elsewhere) make a Window and open a file; they are handled on the editor, reached
by bubbling, because neither the focused Window nor the canvas claims them.

**View controls.**

| | |
|---|---|
| `Ctrl`/`⌘` + wheel, or pinch | zoom about the pointer, 20%–400% |
| wheel / `⇧`+wheel | scroll the board vertically / horizontally |
| `H` | enter pan mode — hand cursor, drag anywhere pans (Windows stop taking clicks) |
| `V` or `Esc` | leave pan mode |
| `0` | reset zoom, and put the origin back at the viewport's top-left |
| `+` / `-` | zoom about the viewport centre |

## Saving: state IS the work

There is one notion of "saved" here, and the host's word for it is *plugin state*.
A state blob holds the whole canvas: every open Window, its name, its cell / place
/ size / tint, its mixer channel (gain, mute, solo), and each track's audio bytes,
output channel and volume. Selection, loop region and chunks travel with it. What
does NOT travel is the transport — a restored canvas comes up stopped — and the
Window ids, which are per-session handles.

That one blob is behind all three of these:

| Where | What happens |
| --- | --- |
| Options ▸ Save Current State… / Load a Saved State… | writes / reads it as a file |
| quitting and relaunching the standalone | it goes into `SoundEffect.settings`, so the canvas comes back |
| a DAW session with the plugin in it | the host saves it with the session |

Format and threading live in `engine/EngineState.h`. Three steps on purpose:
`serializeEngine` (message thread — reads the UI-side documents), `parseEngineState`
(any thread; pure, and where untrusted bytes are handled) and `applyEngineState`
(message thread only; goes through Engine's command queue, never at the live Window
set). The processor decodes wherever the host called it and marshals only the apply,
via `AsyncUpdater`. Round trip, refusal of damaged blobs, and a 4000-case mutation
sweep are asserted in `tests/run_state.cpp` — `state-tests`, JUCE-free like the rest.

NB this is our own blob, not an interchange format; byte-accurate AIFF/WAV save
(extended-80 rate, offset-binary payload) is still to come — see `Import.h`.

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
- **Save / load**: `getStateInformation` / `setStateInformation` implemented, so the
  Options menu's Save/Load items, the standalone's quit-and-relaunch, and a DAW
  session all carry the real canvas (see *Saving* above). Engine side verified by
  `state-tests` (field-by-field round trip, damaged blobs refused, ASan/UBSan clean
  over a 4000-case mutation sweep); verified in the built app too — a three-Window
  canvas written into `SoundEffect.settings` came up on screen with its tints,
  sizes, byte counts, independent track lengths, mute and solo intact, and was
  written back out byte-identical apart from the grid's own re-pack of x/height.
- **The Keyboard palette**: the one effect that is not a form, and the only one that
  is always on. A floating strip hangs under the View palette, shown and hidden from
  a button there (the module's own ICN# 128, inverted while it is up), and at any
  moment it plays **the Window that is selected now** — click a key to hear that
  Window at that pitch, click a different Window and the same keyboard is playing
  that one. Apply commits by editing the document's playback rate and **touching not
  one byte of audio**; Resample Rate is how you make the lie true.
  The keyboard itself is recovered exactly — the shipping module's resource fork
  (`SoundEffects 0.9.2 ƒ/Effects Folder/Keyboard`) still carries PICT 128, ICN# 128,
  WIND/CNTL, STR# 1000 and TEXT 1000, and the picture is redrawn from measurements
  of the decoded bitmap (56 white keys on an 8px pitch, eight octaves, black keys
  5 × 20) rather than embedded, so it is pixel-exact and stays crisp. The dot under
  the document's base note is the original's.
  Everything around it is deliberately modern: no modal, no System 7 dialog chrome,
  no "Use selection for looping" checkbox (looping is always on — it was the switch
  that turned a one-shot into an instrument), no Cancel, and **no one-channel
  limitation** — the original could only play the first selected channel and
  apologised for it in STR# 1000 #3; this plays every selected track, and nothing
  raises the apology any more.
  **A key does one of two things**, and the difference is the whole feel of it. On a
  STOPPED Window it starts it looping at that pitch and letting go stops it. On a
  Window ALREADY ROLLING it re-pitches the transport *where it stands* — letting go
  puts the rate back and leaves it running, so you can ride a live loop up and down
  the keyboard and hand it back exactly as you found it. Nothing restarts: no gap,
  no playhead jump, no phase lost against the other Windows, and no re-snapshotting
  of the loop region from a selection that may have moved since. That is what
  `Transport::setRateScale` exists for; save-and-restore would have gapped the loop
  for the length of every note and had to reconstruct state the transport does not
  publish. It also only ever touches the Window it is driving — playback is global
  and additive here, and almost nothing on the canvas is the keyboard's to stop.
  The readout badge wears the **target Window's tint**, the way the mixer's strips
  do: on a one-row strip with no space for a title, the tint is what says which
  Window the keys are about to move.
- **Next increments**: System 7 chrome polish; the effects port; recording;
  byte-accurate AIFF/WAV save.

## Known TODO / not-yet-safe

- **Window lifecycle hand-off: DONE** (lock-free command + retirement queues;
  gain/mute atomics). Verified with ThreadSanitizer (200k-iteration concurrent
  churn, no races) and `leaks` (0 leaks, incl. destructor reclaim).
- **Intra-window edits are NOT yet safe.** The audio thread reads a Window's
  document (track buffers, selection, loop) while playing. Destructive effects,
  selection changes, and add/move/trash *track* while a Window is playing still
  need their own hand-off (same command-queue idea, or a per-Window immutable
  document swap). Editing a stopped Window is fine.
- **`getStateInformation` has no thread escape.** It reads the UI-side documents,
  so it expects the message thread — which is where the standalone and every host
  we target call it. `setStateInformation` is defended (parse anywhere, apply
  marshalled); the save side cannot be, because the host wants the bytes before the
  call returns.
- **The standalone keeps its state in the settings file.** Quitting base64s the
  whole canvas — audio and all — into `~/Library/Application Support/
  SoundEffect.settings`. Fine at SoundEffect sizes (8-bit, 22 kHz), but a canvas
  full of long imported audio makes for a fat settings file and a slower quit.
- **`TrackMonitor` uses `std::set`.** Soloing/muting a *track* (not a window)
  mutates a set — allocation. Fine today because it goes through the command
  queue applied on the audio thread only via `play` payloads, but a live
  solo/mute path must avoid `std::set` on the audio thread.
- Plugin format AAX needs the AAX SDK path set; CLAP needs the clap-juce-extension
  wrapper. Only Standalone/VST3/AU are enabled by default.
