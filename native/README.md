# SoundEffects — native (JUCE)

The native rebuild of SoundEffects 0.9.2: a **standalone app** and a **DAW plugin**
(VST3 / AU) from one codebase, on the VCV Rack Pro delivery model. Only the
standalone is released today; the plugin builds from the same sources but is
frozen until it is hardened (see *Known gaps*).

The TypeScript project in the repo root is the **reference spec** (byte-accurate
model, effects, file formats, tests). This `native/` tree ports the engine to C++
and wraps it in JUCE. Where behaviour is subtle, the C++ cites the TS file it came
from. The 0.4.0 work, its settled decisions and its journal are in
[`docs/0.4.0/PLAN.md`](../docs/0.4.0/PLAN.md); departures from the original are
marked `[NATIVE]` in the [SPEC](../SoundEffects-0.9.2-SPEC.md).

## Layout

```
native/
  CMakeLists.txt             JUCE via FetchContent → Standalone (+ VST3 / AU)
  packaging/                 package.sh (universal DMG + zip), DocumentTypes.plist
  tests/                     JUCE-free test builds: effect-fixtures, engine-tests, state-tests
  Source/
    PluginProcessor.{h,cpp}  the ONLY JUCE/host boundary; owns the engine and the recorder
    PluginEditor.{h,cpp}     menu bar, side panels, canvas, palettes; the app's commands
    StandaloneApp.cpp        our own JUCEApplication: the window, files, the settings file
    engine/                  host-agnostic core — ZERO JUCE includes
      SampleFormat.h, Track.h, SoundDocument   the document: independent byte buffers per track
      Edit                   cut / paste / duplicate / silence / Select Exactly, Peak, Silence…
      Mixer                  per-frame read mapping (directions, loops) and the track sum
      Transport              one Window's playhead, loop, Advance, direction, live re-aim
      Engine                 every Window summed, per-Window mixer, the UI↔audio hand-off
      Recorder, CaptureRing, RecordConvert, Resample   recording: capture, drain, convert
      EngineState, Project, Wav, Import   the canvas as bytes, as a project folder, WAV I/O
      SpscQueue.h            lock-free single-producer/consumer ring
    effects/                 the module host (the original's Glue ABI), registry, setups,
                             and modules/: the 37 effects
    ui/                      JUCE UI (message thread); reads engine UI-safe snapshots
      Os8, Os8Widgets, Os8Icons, Os8Popup   the Mac OS 8 design system: tokens, painters,
                             widgets, icons, popup frame (`--gallery` shows them all)
      AppMenuBar, AppCommands   the in-app menu bar and the one table of commands and keys
      CanvasComponent        the board: grid / free placement, zoom / pan, themes
      WindowComponent        one Sound Window: info strip, selection fields, track lanes,
                             overview, transport
      MixerComponent         the Mixer panel: one strip per Window, in slots
      EffectsPanel, EffectLayouts   the Effects panel and each effect's tray
      SidePanel              the frame of both side panels (270 px, collapsible)
      ViewPalette, KeyboardPalette, KeyboardArt   the canvas palettes
      EditPopups, FilePopups, AudioSourcePopup   the popups
      Reorder.h              drag-to-reorder: lift, sliding neighbours, glide in or home
      AppPrefs               app-wide preferences (unit, Auto-apply, setups, panels)
```

## Threading (UI ↔ audio)

The **audio thread** solely owns the *live* Window set and each Window's audio
document. The **message thread** never touches them: it posts commands (add /
remove / transport / record / document swap) into a lock-free SPSC queue, drained
at the top of every `render()`, and reads and writes the per-Window mixer values
(gain, balance, mute, solo), which are atomics.

**Editing is RCU.** The UI owns its own copy of each document and edits it freely.
`commitEdit()` publishes an immutable clone, which the audio thread swaps in with
one pointer store; the old one goes back through a retirement queue and is freed
on the message thread (`collectRetired()`, on a 30 Hz timer in the processor). A
change of selection, Set Loop region or speakers needs no samples, so
`commitMeta()` sends just those values and the audio thread writes them into its
document in place. Either way **nothing is allocated or freed on the audio
thread**, and an edit lands mid-loop without a glitch.

**Recording** runs the other way: the audio thread pushes the chosen source into a
`CaptureRing` of float frames, and the `Recorder` drains it on the message thread,
converts it to the Sound's rate and depth and writes it into the UI document.

All message-thread API must be called from one thread (single producer);
`render()` is the single consumer. `engine-tests` covers these paths, and is run
under ThreadSanitizer for every engine change.

## Vocabulary

Canvas › **Window** › **Tracks** › **Track** › *Audio sample* (with a **Selection
area** and its **Output channels**, L and R) · plus a **Transport** and a **Mixer
channel**. NB the original app and the TS reference call a lane a "channel"; the
rebuild renames that to **Track** and reserves "channel" for output routing and
the mixer.

## The model (what makes it SoundEffects)

- **Independent tracks = rudimentary per-track samplers.** Each track is its own
  buffer, and in a playing window each track loops over ITS OWN length
  independently, so tracks phase against each other within a window, and windows
  phase against each other.
- **Loops.** Loop with no selection: every track loops its own length. Loop with a
  selection: the selected span loops in the selected tracks. Loop with a Set Loop
  region: that region, across every track. No loop: one pass. A running loop
  **follows the document**: change the selection or the region while it plays and
  it re-aims at the next block, each track keeping the frame it is on.
- **Directions.** Forward, Backward or Ping-pong, per Window. The transport's
  position is one monotonic clock and each track reads it through its own phase, so
  a direction change only re-anchors the phases, under a ~2 ms crossfade.
- **Silence vs absence.** Silence = real silent samples, drawn as a SOLID line and
  part of the loop length. Absence = past a track's end, drawn as a DOTTED line,
  contributes nothing.
- **Global additive playback.** `Engine` sums *every open window* at once. The
  canvas is the spatial view of a bank of unsynchronised loopers.
- **Mixer.** Each Window has a gain, a linear balance (unity at centre, the far side
  attenuated), mute and solo. Solo is cumulative and a filter; mute outranks it.
  None of them stops a transport: an unheard Window is still rendered and thrown
  away, so it stays in phase. The audio thread publishes each Window's post-fader
  stereo peaks for the strip's VU, decaying on their own.
- **Destructive edits, declared rate obeyed even when it lies.** As the original.

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

**Grid or free.** `G`, or the layout toggle on the View palette, turns the grid
off: Windows stay where they are and a drag moves them freely. Turning it back on
re-packs them. In the grid a dragged Window is *carried* (`Reorder.h`): it lifts
past a 4 px threshold, floats translucent on a shadow, its neighbours slide out of
the way, and on release it glides into its gap, or home if dropped off the board.

**Resizing a Window** never scales its chrome. Total height is always
`chrome + numTracks × laneHeight`, so a vertical drag changes the height of the
track lanes, and the title bar, info strip, overview and transport keep their
fixed size. Only the waveforms grow.

**Output channels.** Each track has two speaker toggles at the head of its lane,
left and right, so a track goes left, right, both or nowhere. **A stereo file routes
itself**: `makeDocumentFromFloat` sends track 1 left and track 2 right rather than
both to both.

**Renaming.** Double-click a Window's title bar for an inline editor; `Return` or
clicking away commits, `Esc` cancels, and an empty box keeps the old name. The
name is UI-only — the audio thread never reads it — so a rename does NOT go
through `commitEdit()`, which would clone every sample to publish a string. The
mixer strip polls `uiDoc->name` on the editor's timer, since nothing else would
tell it that its channel had been renamed.

**Theme.** Each Window wears one of the 24 named themes of the design system
(`os8::kThemes`), persisted as an index in `Window::tint`. A new Window gets one at
random from those no open Window is wearing. The theme fills the Window, its
speaker buttons and its mixer strip; the waveform bed stays white.

**Highlight.** The border is 2 px at rest, 3 px under the pointer and 5 px on the
**selected** Window, always outside the body so it never disturbs a neighbour.
"Selected" is the canvas's choice (a click), not keyboard focus, on purpose: focus
moves the moment a side panel is used, and the Window the panels act on must not
evaporate under them.

**Transport.** Record (with **Source** beside it), Stop, Pause, a combined Play
button whose strip opens the direction menu, Fast Forward (2×) and Loop. `play()`
resets the 2× state, so anything that restarts playback clears it. Recording forces
Forward and greys the direction menu.

**Keys.** Every shortcut lives in one table, `ui/AppCommands`, routed to the selected
Window. Single keys (`R F L M S K G D 1…0`) work only when no text field has focus.

**View controls.**

| | |
|---|---|
| `Ctrl`/`⌘` + wheel, or pinch | zoom about the pointer, 20%–400% |
| wheel / `⇧`+wheel | scroll the board vertically / horizontally |
| right-button drag | pan, from anywhere, over Windows too |
| `H` | pan mode: hand cursor, drag anywhere pans (Windows stop taking clicks) |
| `V` or `Esc` | leave pan mode |
| `0` | reset zoom, and put the origin back at the viewport's top-left |
| `+` / `-` | zoom about the viewport centre |
| ⌘0 / ⌘1 | Zoom 100% / Zoom to Fit (Window menu) |

## The menus, and the canvas as a document

The menu bar is drawn inside the app (`AppMenuBar`), so the standalone and the
plugin get the same **File / Edit / Selection / Transport / Window** menus; the
macOS bar keeps only the application menu. Every item is a command in
`AppCommands`, carrying its name, its keys and (through the editor's
`getCommandInfo`) its greying, so a menu row and its shortcut can never disagree.
The departures from the recovered menus are marked `[NATIVE]` in the SPEC.

What makes the canvas a *document* is one field, `MainWindow::canvasFile`. The
canvas's NAME is that file's, and "Untitled Canvas" when there is not one yet —
deliberately not stored in the blob, so renaming the file in the Finder renames
the canvas and there is no second copy of the name to disagree with the first.
It is what the in-app menu bar shows at its left, what ⌘S writes to without a dialog, what Revert
goes back to, and it is remembered in the settings file (`canvasFile`) so the
name still says what you were working on after a relaunch.

New Canvas and Revert Canvas confirm first. Nothing else here does, and the
Window close box still does not: one Window is cheap to remake, a canvas is not,
and this program has no undo by design.

## Saving: state IS the work

There is one notion of "saved" here, and the host's word for it is *plugin state*.
A state blob holds the whole canvas: every open Window, its name, its cell / place
/ size / theme, its mixer channel (gain, balance, mute, solo, slot), its direction,
and each track's audio, output channels and volume. Selection, loop region and
chunks travel with it. What does NOT travel is the transport (a restored canvas
comes up stopped) and the Window ids, which are per-session handles.

That one format is behind all of these:

| Where | What happens |
| --- | --- |
| File ▸ Save Canvas / Save Canvas as… | a **project folder**: `<Name>/<Name>.sefx` plus `Assets/`, one mono WAV per track |
| File ▸ Export Sound… | one Sound as a self-contained `.sefx`, audio embedded |
| File ▸ Open Canvas… / Import Sound…, double-click, command line | any `.sefx`: a project, an export, or a 0.3 single file |
| quitting and relaunching the standalone | the canvas goes into `SoundEffects.settings` and comes back |
| a DAW session with the plugin in it | the host saves it with the session |

The format is versioned (`kEngineStateVersion`, now 3) and readers accept every
older version; the project folder logic is in `engine/Project`, JUCE-free and
tested through a real folder. A 0.3 canvas opens, and its first save goes through
Save As, which turns it into a project.

The settings file is named after the app, so correcting the name to the plural
would have orphaned every existing canvas — the autosave lives *inside* that file.
`SoundEffectsStandaloneApp::migrateLegacySettings` copies the old
`SoundEffect.settings` to the new name on the first launch that finds no new one,
and leaves the original alone as the way back from a downgrade.

Format and threading live in `engine/EngineState.h`. Three steps on purpose:
`serializeEngine` (message thread — reads the UI-side documents), `parseEngineState`
(any thread; pure, and where untrusted bytes are handled) and `applyEngineState`
(message thread only; goes through Engine's command queue, never at the live Window
set). The processor decodes wherever the host called it and marshals only the apply,
via `AsyncUpdater`. Round trip, refusal of damaged blobs, and a 4000-case mutation
sweep are asserted in `tests/run_state.cpp` — `state-tests`, JUCE-free like the rest.

On disk it is `.sefx`, declared to the Finder as the exported UTI
`com.SoundEffects.canvas` (conforming to `public.data`) with the app claiming it at
rank Owner — so a canvas has a name in Get Info and opens SoundEffects when it is
double-clicked. Those keys are in `packaging/DocumentTypes.plist`, spliced into the
Standalone bundle's Info.plist by `CMakeLists.txt`; the reason it is not
`juce_add_plugin`'s `DOCUMENT_EXTENSIONS` is written down in both files. The reader
keys off the `SEFX` magic and not the name, so a canvas saved before the extension
existed still opens.

Opening a file always goes through `MainWindow::loadCanvasFile`, which parses the
blob BEFORE handing it to the processor. That is only so a bad file can be reported:
`setStateInformation` keeps the canvas you already have when a blob does not parse,
which is right for a host but silent, and on a fresh launch "what you already have"
is an empty canvas — indistinguishable from a file that opened fine and was empty.

## Build

Requires CMake ≥ 3.22, a C++17 compiler, and (first configure) network access to
fetch JUCE. On macOS the Command Line Tools are enough (full Xcode NOT required) —
use the Ninja generator, not `-G Xcode`.

```sh
# one-time tools (macOS/Homebrew): brew install cmake ninja
cd native
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target SoundEffects_Standalone   # or omit --target for all formats
open build/SoundEffects_artefacts/Debug/Standalone/SoundEffects.app
```

Artifacts land under `build/SoundEffects_artefacts/` (Standalone `.app`, `.vst3`,
`.component`). Verified building & running on macOS arm64 (AppleClang, CLT-only,
JUCE 8.0.4) 2026-07-18; still the toolchain for 0.4.0.

## Status

**0.4.0 is released** (tag `v0.4.0`). Everything above is built and drive-tested;
the step-by-step record, with what was verified at each step, is the journal in
[`docs/0.4.0/PLAN.md`](../docs/0.4.0/PLAN.md). Tests: `effect-fixtures` (209 golden
cases from the TS reference), `engine-tests` (mixer, directions, loops, recording,
concurrency; run under TSan) and `state-tests` (round trips, damaged blobs, a
mutation sweep), all JUCE-free. See the Verification section of PLAN.md for how to
run them.

## Known gaps

The performance and robustness audit of 0.4.0, with what was fixed before the
release and what comes after, is [`docs/0.4.0/perf-audit-plan.md`](../docs/0.4.0/perf-audit-plan.md).
The main open items:

- **The canvas is saved only at quit or on ⌘S.** The standalone keeps the open
  canvas, audio and all, base64'd in `~/Library/Application Support/
  SoundEffects.settings`, written on quit. A crash loses everything since the last
  save, and a canvas of long audio makes a fat settings file and a slower quit.
  Background autosave is the first item after the release.
- **The plugin is frozen.** Before it ships: render in chunks when a host sends a
  block larger than `prepareToPlay` promised, keep `prepareToPlay` off the
  message-thread registry, and settle a take when the device rate changes.
- **`getStateInformation` has no thread escape.** It reads the UI-side documents,
  so it expects the message thread, which is where the standalone and every host
  we target call it. `setStateInformation` is defended (parse anywhere, apply
  marshalled); the save side cannot be, because the host wants the bytes before the
  call returns.
- **Effects run on the message thread.** A long effect on a long Sound blocks the UI
  until it is done.
- AAX needs the AAX SDK path set; CLAP needs the clap-juce-extension wrapper. Only
  Standalone / VST3 / AU are enabled.
