# SoundEffects 0.5.0 — working plan

This file is the durable memory of the 0.5.0 work. **Every session starts by
reading it**, and every sub-step that lands updates it in the same commit.

- What to build: [`specs_v0-5-0.md`](../../specs_v0-5-0.md). It is a local
  snapshot of two Figma Specs blocks (file `QmG2Chvizn1QXGPwQVBgnn`): "To-do v.0.5.0"
  `998:20646` and "Application Menus — v.0.5.0" `1078:6637`. Figma stays the source
  of truth. Re-sync the snapshot whenever Figma changes; read a block with
  `use_figma` → `figma.spec.getContent({ specNodeId: '<node>' })`.
- What it must look like: the Figma design system (see the node index below).
- Branch: `v0.5.0-dev`, cut from `v0.4.0-dev` after 0.4.1. `main` stays shippable
  0.4.x and is merged at release.
- Rule: **one sub-step = one green commit**. That means `native/tests` pass and the
  standalone builds, then the commit is pushed to `origin/v0.5.0-dev` and the
  journal below gets one line. **One step per session.**
- The plug-in is no longer frozen (since 0.4.1): step 12 builds and checks it.

## Decisions (settled — don't re-ask)

Agreed with the author on 2026-11-14, where the spec is silent or contradicts itself.

| Topic | Decision |
|---|---|
| Initial transport state | Everything off except **Loop on**, Stop included, for a new or newly opened Sound. The spec's table says "Loop: initial state off"; its note wins. |
| Play from the caret | No selection, a caret: **all tracks** of the Window start at the caret position. Loop off: to the end of each track, once. Loop on: start at the caret, then each track loops over its whole length (first lap partial). A caret past a track's end leaves that track silent until its next lap. |
| Bug 2 (Loop off) | With a selection, only the selected tracks play, over the selected span. |
| Loop-on behaviour | Must not regress: step 3 pins today's loop-on behaviour with engine tests before touching anything. |
| Zoom Sound keys | Zoom In **⌘=** (⌘+ accepted too), Zoom Out **⌘-**, Fit Window **⌘⇧0**, Fit Selection **⌘⇧=**. The Figma menu's ⌘+ / ⌘= / ⌘⇧= collide on a Mac keyboard (+ is ⇧=). |
| Zoom Canvas keys | `+` / `-` move into the command table. **Zoom 100 % ⌘1, Zoom to Fit ⌘0**, as the spec says (the reverse of 0.4). |
| ⌘← / ⌘→ | Unchanged from 0.4: **⌘← = Extend to Beginning, ⌘→ = Extend to End.** The Figma transcription shows them swapped; fix Figma. |
| Track number square | It is the mixer-slot box in the Sound-info row (`526:4845`). |
| Curvature menu | "Log - 50%" → 50, "Linear - 100%" → 100, "Expo - 200%" → 200, "Custom" for any other value. |
| Magnetophon placement | Left panel, **below the Mixer** (layout `700:14314`, component `1199:16274`), with its own collapsible title bar. The Mixer's Main Level footer sits between them. |
| Magnetophon tape | **Ephemeral.** The tape and its takes live for the session; Send to canvas is how to keep them. Warn on closing a canvas whose tape holds unsent audio. |
| Magnetophon source | Main (after Main Level), one Sound's post-fader output, the external input or the DAW input. Reuse `AudioSourcePopup`. The tape's own playback is never re-captured by the Main source. |
| Magnetophon design gaps | The author's design is light on detail: fill gaps with the existing OS8 conventions, then tweak with the author. |
| MIDI | The author has the hardware (KeyStep 37, Push 3 / LinnStrument). **All of it ships in 0.5.0**: notes, MPE pitch slide, and DAW MIDI in the plug-in. |
| MIDI voice model | The keyboard palette is mono: last-note priority with a held-note stack. MPE per-note pitch bend → continuous rate via `setRateScale` (zone range, 48 semitones by default); plain pitch bend ±2 semitones. |
| Asset names | `Assets/<assetId> <name> - track T.wav`. `assetId` is a per-canvas counter stored in the `.sefx`; old projects get ids on open and their files are renamed on the next save. |

## Figma node index

| What | Node |
|---|---|
| Specs | To-do `998:20646`, Application Menus `1078:6637` |
| App layout (with Magnetophon and Main Level) | `700:14314` |
| Sound-info (slot number square) | `526:4845` |
| Placeholder-waveform (Selection × Mute × Recording × **Set-loop**) | `196:4395` |
| Mixer panel (header new-open, footer main-level) | `636:10485` |
| Curvature dropdown | `499:4926` |
| Magnetophon | `1199:16274` |
| Menus | source design `543:6437`; File `569:13307`; Edit `569:12670`; Selection `569:13068`; Sound `569:13467`; Zoom Sound `997:13518`; Canvas `569:13665`; Zoom Canvas `998:13725` |
| Colour styles | `Window-themes/macos8-gray` #dddddd, `Window-themes/macos8-lavender` #ccccff |

The 0.4.0 index (`docs/0.4.0/PLAN.md`) still holds for everything else (UI elements,
Sound Windows, side panels, effect trays, popups).

## Where things are (found 2026-11-14)

- **Menus:** one table in `native/Source/ui/AppCommands.cpp`; menu contents in `PluginEditor.cpp` `getMenuForIndex` and `windowMenu()`; the standalone mirrors them into the native bar (`StandaloneApp.cpp`).
- **Transport:** `WindowComponent::perform` / `togglePlayPause` / `updateTransportButtons`; engine in `engine/Transport.cpp` (`play`, `stop`, `pause`, `advance`). Record starts at once (no arming); FF latches.
- **Bug 2:** in play-once, `readSpan` (`engine/Mixer.cpp`) does not filter tracks and `Transport::play` never sets the selected track range. A caret counts as no selection, so the whole Sound plays from 0.
- **Set Loop:** `se::setLoop` (`engine/Edit.cpp`) copies the selection; it is drawn only in the zoom-bar overview.
- **Slot number:** `WindowComponent.cpp` status row uses `os8::drawText` (fractional x); `MixerComponent.cpp` `drawSlotNumber` rounds x — the model to follow.
- **Assets:** `engine/Project.cpp` `writeProject` names WAVs `NN <name> - track T.wav`, NN = Window index in engine order; stale files are already deleted on save.
- **Mixer:** no master gain anywhere. **Effects:** header is painted inside the scrolling `Content` (`EffectsPanel.cpp`).
- **Themes:** `kThemes` in `ui/Os8.h`; canvas background hard-coded `0xfffafafa` in `CanvasComponent::paint`.
- **Recording:** `Engine::render` → single `CaptureRing` → `Recorder` on the message thread.
- **MIDI:** none; `processBlock` ignores its `MidiBuffer`. `KeyboardPalette::strike/release` take a note number.

## Steps and sub-steps

Legend: `[ ]` todo · `[~]` in progress · `[x]` done (see the journal).

### 0. Setup
- [x] 0.1 Branch `v0.5.0-dev`, `specs_v0-5-0.md` snapshot, this plan

### 1. Small UI fixes
- [x] 1.1 Slot-number square: centre the digit on a rounded x, as `drawSlotNumber` does; compare with `526:4845`
- [x] 1.2 Curvature menu: labels and values Log 50 / Linear 100 / Expo 200, reverse mapping, defaults in `EffectLayouts.cpp`; check in `Norris.h` that "Log" really curves log

### 2. Menus 0.5.0
- [x] 2.1 Transport → **Sound**, Window → **Canvas**, with the spec's order, separators and keys. Color ⌘T and Rename… ⌘R move to Sound; Flip Direction (D); Arm Record (R) and Direct Recording (⇧R) greyed until step 5; Select Set Loop ⌘⇧A; Unset Loop ⌘⇧L greyed until step 4
- [x] 2.2 Zoom Sound and Zoom Canvas submenus: window zoom commands (`zoomBy`, show all), new **Fit Selection**; canvas `+`/`-` into the table; swap ⌘0/⌘1

### 3. Playback range
- [ ] 3.1 Engine tests that pin today's loop-on behaviour (selection, Set Loop, per-track own-length loops)
- [ ] 3.2 Bug 2: play-once with a selection plays only the selected tracks
- [ ] 3.3 Play from the caret (all tracks), loop off and loop on; the drawn playhead stays inside what sounds

### 4. Set Loop
- [ ] 4.1 Unset Loop ⌘⇧L (greyed when no loop is set)
- [ ] 4.2 Outside-the-loop audio drawn muted, per the `Set-loop=yes` variants of `196:4395`, with selection and recording too

### 5. Transport state machine
- [ ] 5.1 Pure, JUCE-free button-state model in `engine/` (Record off/armed/on, Stop latched, Play, Pause, held FF, Loop, direction disabled); one test per row of the spec table
- [ ] 5.2 Wire it: arm/disarm, Play on armed starts recording, disarm mid-take keeps playback glitch-free, ⇧R direct recording, Stop latches and resets, Pause re-arms; initial state on create/open
- [ ] 5.3 Momentary FF (mouse hold or F held, via `keyStateChanged`) in its Play / Pause / Stop cases, with an engine release that restores the previous state

### 6. Clickable selection-info
- [ ] 6.1 Pure functions in `engine/Edit.cpp` with tests: to-start, whole tracks, to-end, each with its no-selection variant; edge cases (empty tracks, uneven lengths, selection past a track's end, empty Sound, frozen playing region)
- [ ] 6.2 Hit zones on the three icons, pointing-hand cursor

### 7. Color Theme Show/Hide
- [ ] 7.1 App-wide pref; hidden → themed elements macos8-gray and canvas macos8-lavender; visible → canvas takes the selected Sound's colour (white when none); Sound ▸ Color greyed when hidden; one `themeColourFor(window)` access point

### 8. Unique asset ids
- [ ] 8.1 Per-canvas `assetId` counter in the `.sefx`, new file names, ids assigned to old projects on open; test: reordering mixer strips then saving renames nothing

### 9. Mixer: Main Level
- [ ] 9.1 Engine master gain (atomic, before the final clamp), saved with the canvas; static Main Level footer; header and footer fixed, only the list scrolls

### 10. Effects panel
- [ ] 10.1 Static header outside the viewport
- [ ] 10.2 Smart auto-scroll: on opening a tray, scroll just enough to show the group and its tray; if taller than the view, align the tray's top under the header

### 11. Magnetophon
- [ ] 11.1 Engine: second capture path independent of `Recorder` (Main / Sound / external / DAW), stereo float at device rate; `engine/Tape` model (growing buffer, takes as start markers, record-over from the position, stop, play, next/previous take); tape playback into the mix but not re-captured. Tests
- [ ] 11.2 Panel under the Mixer: collapsible title bar, turning reels, scrolling waveform strip with "Take N" labels, From / format / time line, Source, Record, Stop, Previous, Play, Next, Send
- [ ] 11.3 Send to canvas popup: whole tape as one Sound, or chosen takes as one Sound each; close-canvas warning for unsent audio

### 12. MIDI
- [ ] 12.1 Notes → keyboard palette: standalone MIDI inputs from the Audio/MIDI Setup dialog, DAW MIDI in the plug-in, all via `processBlock` → SPSC queue → message thread → `strike`/`release`; last-note priority; the palette shows incoming notes. Test on the KeyStep 37
- [ ] 12.2 MPE via `juce::MPEInstrument`: per-note pitch bend → continuous `setRateScale`. Test on Push 3 / LinnStrument
- [ ] 12.3 Plug-in: VST3/AU built, validated (`auval`), notes and MPE from a host

### 13. Release
- [ ] 13.1 Re-sync the spec snapshot, version 0.5.0, README, `package.sh`; tag, GitHub release and merge into `main`, each on the author's go-ahead

## Verification per sub-step
- Tests: `cmake -S native/tests -B native/tests/build -G Ninja && cmake --build native/tests/build`, then **from `native/tests`** run `./build/effect-fixtures`, `./build/engine-tests` and `./build/state-tests`. Run TSan/ASan for engine changes.
- Standalone build: `cmake --build native/build --target SoundEffects_Standalone`.
- UI: capture the running standalone at 1× and compare with the Figma node (method in `docs/0.4.0/PLAN.md`, "How to compare with Figma"). Drive-test the feature in the app.
- Audio: listen to each row of the transport table, including disarm mid-playback with no click.

## Open questions
- **Select Silence key.** The 0.5.0 Selection menu (`569:13068`) shows ⌘⌥A; the app keeps 0.4's ⌘⇧⌫ until the author says which.
- **⌘T.** Sound ▸ Color's row shows no shortcut in Figma (the spec table says ⌘T). 2.1 binds ⌘T to drop the colour list down under the selected Window's title bar; confirm, or say if it should cycle colours instead.
- **Figma fix to make:** the rule above Mute in `569:13467` is a 1 px rectangle, not a `Menu_separator` (the app draws the normal separator). (Direct Recording's ⌘R was corrected to ⇧R in Figma on 2026-11-14.)

## Journal
One line per finished sub-step: date · sub-step · what was verified · commit.

- 2026-11-14 · 0.1 · Read both Figma Specs blocks; branch `v0.5.0-dev` from `v0.4.0-dev`; `specs_v0-5-0.md` snapshot; this plan with the decisions agreed with the author (initial Loop on, play from caret on all tracks, ephemeral Magnetophon tape with full source choice, zoom keys, all of MIDI in 0.5.0). No code changed.
- 2026-11-14 · 1.1 · Slot digit centred on a rounded x and set 3.5 px down as in `526:4845` (was 2 px, baseline now box+11). Captured the running dev build at 1×: slots 2/4/5 crisp, ink rows 4–10 in the 16 px box, 4|5 px side margins for one digit (Figma's "99" leans left the same way, 1|2); Figma's own 1× export smears over half pixels. Tests green, standalone builds. · (this commit)
- 2026-11-14 · 1.2 · Curvature dropdown: Custom (disabled) / Log - 50% / Linear - 100% / Expo - 200%, mapped to 50/100/200, anything else shows Custom. `Norris.h` `tween` is t^(c/100), so 50 = sqrt(t) (fast rise, log-shaped) and 200 = t² (expo-shaped), which matches the new labels; the old "Expo - 0%" gave t⁰ = an instant jump. Defaults in `EffectLayouts.cpp` stay 100 (Linear), so nothing changed there. `--effects-snapshot` Granular and Sample Hose trays show Linear at 100. Tests green, standalone builds. · (this commit)
- 2026-11-14 · 2.1 · Menus renamed **Sound** and **Canvas**, contents in the spec's order and separators (`569:13467`, `569:13665`, `569:13068`). Sound: Color ▸ (greyed "Color" with no Sound selected), Rename… ⌘R, Arm Record R and Direct Recording ⇧R greyed until step 5, Select Audio Source…, Stop / Play / Pause, Flip Direction D (the old Cycle Direction), Fast Forward before Loop, Mute / Solo. Play Forward / Backward / Ping-pong (⌥␣) stay bound but leave the menu. ⌘T drops the colour list under the selected Window. Selection: Unset Loop ⌘⇧L greyed until step 4, Select Set Loop ⌘⇧A. Canvas: Hide Color Theme greyed until step 7, the sound list (the inert selected-sound header row is gone; the list ticks it), Free Canvas G / Hide Keyboard K / Hide Side Panels ⌘., zoom rows until 2.2. `--app-snapshot` menus compared with Figma's 1× screenshots: same rows, order and separators; only Direct Recording's key differs (Figma's ⌘R, see Open questions). Tests green, standalone builds. · (this commit)
- 2026-11-14 · 2.2 · **Sound ▸ Zoom Sound** (`997:13518`): Zoom In ⌘= (⌘+ too), Zoom Out ⌘-, Fit Window ⌘⇧0, with the zoom bar's icons and greying (`WindowComponent::Action::zoomIn/zoomOut/fitWindow`), and new **Fit Selection** ⌘⇧= (the selection's span fills the waveform; shorter than the closest zoom, it is centred at that zoom; greyed with a caret). ⇧= and ⇧0 arrive as '+' and ')' on a US layout, so both spellings are bound. **Canvas ▸ Zoom Canvas** (`998:13725`): + / - moved from `CanvasComponent::keyPressed` into the command table (bare and ⇧ forms), Zoom 100% now ⌘1, Zoom to Fit ⌘0. The menu-width lookup tells the two "Zoom In" rows apart by their shortcut. `--app-snapshot` now also writes each submenu (`-menu-Sound-Zoom Sound.png`, `-menu-Canvas-Zoom Canvas.png`, `-menu-Sound-Color.png`); both zoom submenus match Figma's 1× screenshots row for row, icons included, keys as decided. Keys not drive-tested in the running app. Tests green, standalone builds. **STEP 2 COMPLETE**: next is 3.
