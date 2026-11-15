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
| Recording with Set Loop | With Loop on and a region set, a take stays inside the region: the transport loops it and the take records round and round inside it, into the selected tracks (2 at most) or into new tracks padded with silence up to the region. Loop off ignores the region. Chosen by the author 2026-11-14 ("A: take follows region"); built in 5.2. |
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
- **Transport:** `WindowComponent::perform` / `applyCues` / `updateTransportButtons` driven by `se::TransportButtons` (`engine/TransportButtons.h`, since 5.1/5.2); engine in `engine/Transport.cpp` (`play`, `stop`, `pause`, `advance`). Record arms (R) and ⇧R records directly since 5.2; FF is momentary since 5.3 (button hold, F via `wantsKeyUpDownCallbacks`).
- **Bug 2:** in play-once, `readSpan` (`engine/Mixer.cpp`) does not filter tracks and `Transport::play` never sets the selected track range. A caret counts as no selection, so the whole Sound plays from 0.
- **Set Loop:** `se::setLoop` / `unsetLoop` / `hasLoop` (`engine/Edit.cpp`); drawn in the zoom-bar overview and, since 4.2, in the lanes (`WindowComponent::paintTracks`, outside the region in the Mute look). The Recorder ignores the region.
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
- [x] 3.1 Engine tests that pin today's loop-on behaviour (selection, Set Loop, per-track own-length loops)
- [x] 3.2 Bug 2: play-once with a selection plays only the selected tracks
- [x] 3.3 Play from the caret (all tracks), loop off and loop on; the drawn playhead stays inside what sounds

### 4. Set Loop
- [x] 4.1 Unset Loop ⌘⇧L (greyed when no loop is set)
- [x] 4.2 Outside-the-loop audio drawn muted, per the `Set-loop=yes` variants of `196:4395`, with selection and recording too

### 5. Transport state machine
- [x] 5.1 Pure, JUCE-free button-state model in `engine/` (Record off/armed/on, Stop latched, Play, Pause, held FF, Loop, direction disabled); one test per row of the spec table
- [x] 5.2 Wire it: arm/disarm, Play on armed starts recording, disarm mid-take keeps playback glitch-free, ⇧R direct recording, Stop latches and resets, Pause re-arms; initial state on create/open
- [x] 5.3 Momentary FF (mouse hold or F held, via `keyStateChanged`) in its Play / Pause / Stop cases, with an engine release that restores the previous state

### 6. Clickable selection-info
- [x] 6.1 Pure functions in `engine/Edit.cpp` with tests: to-start, whole tracks, to-end, each with its no-selection variant; edge cases (empty tracks, uneven lengths, selection past a track's end, empty Sound, frozen playing region)
- [ ] 6.2 Hit zones on the three icons, pointing-hand cursor
- [ ] 6.3 Shield the three fields from Space and letters (the spec's "ISSUE", added 2026-11-14): a commit (Return, Tab) hands keyboard focus back to the Window, and while a field is being typed in any key that is not part of a number (Space, letters) commits what was typed and goes to the app's commands; Esc, ↑/↓, ⌫ and editing keys stay the field's. `PluginEditor::isTyping` then greys single-key commands only while a field really holds the focus

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

- **Set Loop look with Loop off.** 4.2 greys the outside only while the Window's Loop is on, since play-once ignores the region (3.2); the zoom bar still shows the region either way. Say if it should grey whenever a region is set.

## Journal
One line per finished sub-step: date · sub-step · what was verified · commit.

- 2026-11-14 · 0.1 · Read both Figma Specs blocks; branch `v0.5.0-dev` from `v0.4.0-dev`; `specs_v0-5-0.md` snapshot; this plan with the decisions agreed with the author (initial Loop on, play from caret on all tracks, ephemeral Magnetophon tape with full source choice, zoom keys, all of MIDI in 0.5.0). No code changed.
- 2026-11-14 · 1.1 · Slot digit centred on a rounded x and set 3.5 px down as in `526:4845` (was 2 px, baseline now box+11). Captured the running dev build at 1×: slots 2/4/5 crisp, ink rows 4–10 in the 16 px box, 4|5 px side margins for one digit (Figma's "99" leans left the same way, 1|2); Figma's own 1× export smears over half pixels. Tests green, standalone builds. · (this commit)
- 2026-11-14 · 1.2 · Curvature dropdown: Custom (disabled) / Log - 50% / Linear - 100% / Expo - 200%, mapped to 50/100/200, anything else shows Custom. `Norris.h` `tween` is t^(c/100), so 50 = sqrt(t) (fast rise, log-shaped) and 200 = t² (expo-shaped), which matches the new labels; the old "Expo - 0%" gave t⁰ = an instant jump. Defaults in `EffectLayouts.cpp` stay 100 (Linear), so nothing changed there. `--effects-snapshot` Granular and Sample Hose trays show Linear at 100. Tests green, standalone builds. · (this commit)
- 2026-11-14 · 2.1 · Menus renamed **Sound** and **Canvas**, contents in the spec's order and separators (`569:13467`, `569:13665`, `569:13068`). Sound: Color ▸ (greyed "Color" with no Sound selected), Rename… ⌘R, Arm Record R and Direct Recording ⇧R greyed until step 5, Select Audio Source…, Stop / Play / Pause, Flip Direction D (the old Cycle Direction), Fast Forward before Loop, Mute / Solo. Play Forward / Backward / Ping-pong (⌥␣) stay bound but leave the menu. ⌘T drops the colour list under the selected Window. Selection: Unset Loop ⌘⇧L greyed until step 4, Select Set Loop ⌘⇧A. Canvas: Hide Color Theme greyed until step 7, the sound list (the inert selected-sound header row is gone; the list ticks it), Free Canvas G / Hide Keyboard K / Hide Side Panels ⌘., zoom rows until 2.2. `--app-snapshot` menus compared with Figma's 1× screenshots: same rows, order and separators; only Direct Recording's key differs (Figma's ⌘R, see Open questions). Tests green, standalone builds. · (this commit)
- 2026-11-14 · 2.2 · **Sound ▸ Zoom Sound** (`997:13518`): Zoom In ⌘= (⌘+ too), Zoom Out ⌘-, Fit Window ⌘⇧0, with the zoom bar's icons and greying (`WindowComponent::Action::zoomIn/zoomOut/fitWindow`), and new **Fit Selection** ⌘⇧= (the selection's span fills the waveform; shorter than the closest zoom, it is centred at that zoom; greyed with a caret). ⇧= and ⇧0 arrive as '+' and ')' on a US layout, so both spellings are bound. **Canvas ▸ Zoom Canvas** (`998:13725`): + / - moved from `CanvasComponent::keyPressed` into the command table (bare and ⇧ forms), Zoom 100% now ⌘1, Zoom to Fit ⌘0. The menu-width lookup tells the two "Zoom In" rows apart by their shortcut. `--app-snapshot` now also writes each submenu (`-menu-Sound-Zoom Sound.png`, `-menu-Canvas-Zoom Canvas.png`, `-menu-Sound-Color.png`); both zoom submenus match Figma's 1× screenshots row for row, icons included, keys as decided. Keys not drive-tested in the running app. Tests green, standalone builds. **STEP 2 COMPLETE**: next is 3.
- 2026-11-14 · 3.1 · `loopOnPinTests` in `native/tests/run_engine.cpp` pins today's loop-on playback through `Transport` with the options the Play button (`useLoopRegion`), the Keyboard and the Recorder pass: no selection → each track loops its own length from 0 (100 and 60 drift apart), a caret is no selection (loops from 0 wherever it is), a selection loops its span on its tracks only with the others silent and playhead-less, a selected track shorter than the span is absent past its end then back on the next lap, Set Loop loops the region on every track over any selection or caret, the Keyboard (no `useLoopRegion`) ignores the region, a track that ends before the region stays silent, the published region and track range for each. No engine change. Tests green, standalone builds. · (this commit)
- 2026-11-14 · 3.2 · Bug 2 fixed: `Transport::play` gives play-once the selection's track range (`PlayContext::firstSelTrack/lastSelTrack`, `kAllTracks` when there is no selection) and `readSpan`'s once case filters on it, so a selection plays only its tracks over its span, once, then stops; the published track range follows. The once re-anchor (a direction change mid-pass) took its stop point from track 1, which now may not play: it takes it from the topmost track that does (a mutation back to track 1 fails the new test). **Judgment call:** the Recorder's punch-in sets the new `PlayOptions::allTracks`, so recording into a selection with loop off still hears the other tracks as backing, as in 0.4 (step 5 owns recording). `playOnceSelectionTests`: one track of two, two of three, backward, ping-pong, turned round mid-pass, a short selected track, `allTracks`, no selection unchanged, and through the Engine as the Play button sends it. 3.1's pins unchanged. TSan and ASan clean (engine + state). Tests green, standalone builds. Not drive-tested in the app. · (this commit)
- 2026-11-14 · 3.3 · **Play from the caret**, opt-in through the new `PlayOptions::fromCaret`, which only the Play button sets (`WindowComponent::playButtonOptions`; FF from Stop now posts the same play first, since the transport's last options may be the Keyboard's or the Recorder's). No selection and a caret: every track starts at the caret, whatever track it is in. Loop off: once from the caret to the end (each track to its own end; a track the caret is past is silent with no playhead); backward, from the caret down to 0. Loop on (`Transport::aimLapsFromCaret`): pos starts at the caret on the plain mapping, so each track's first lap is partial and later laps loop its whole length; a track the caret is past waits, silent and playhead-less, until its next lap: forward and ping-pong at the next multiple of its length on the transport clock, backward when the falling read point reaches its last frame (new `PlayContext::silentUntil`, checked in `readFor`, cleared by any re-anchor, so a direction change mid-wait brings it in on the reference frame). Set Loop and a selection win over the caret as before. **Judgment calls:** a caret at 0 or at the end plays from the start as before (at the end there would be nothing to play); the Keyboard and the Recorder keep starting at 0 (the Recorder writes new tracks from 0). `playFromCaretTests`: every decision row, both directions plus ping-pong, the caret on a lap boundary, Advance, Set Loop/selection precedence, moving the caret mid-loop, and through the Engine; removing the gate fails two of them. 3.1 pins and 3.2 tests unchanged. TSan and ASan clean. Tests green, standalone builds. Listened to by the author in the dev build (2026-11-14): all good. **STEP 3 COMPLETE**: next is 4.
- 2026-11-14 · 4.1 · **Selection ▸ Unset Loop ⌘⇧L**: new `se::hasLoop` / `se::unsetLoop` (`engine/Edit.cpp`) clear the Set Loop region and leave the selection alone; `WindowComponent::Action::unsetLoop` greys the row when no region is set (a caret's Set Loop sets none) and publishes through `commitSelection` (commitMeta), like Set Loop. Select Set Loop now greys on `hasLoop` too. `unsetLoopTests`: the pure function (nothing set, caret, cleared, selection untouched, twice), a region loop that is playing re-aims live through `Transport::render` to the selection (only its track sounds, over its span) or, with a caret, to the whole sound with each track on its own length again, the same through `Engine::commitMeta`, and loop off (play-once never reads the region) plays on untouched. TSan and ASan clean (engine + state). Tests green, standalone builds. Tried by the author in the dev build (2026-11-14): Unset Loop works. · (this commit)
- 2026-11-14 · 4.2 · **Set Loop drawn in the lanes** (`196:4395`, the four `Set-loop=yes` variants read with `use_figma`): outside the region every track is drawn exactly as the Mute=on variants draw it (#c0c0c0 wave and line of silence, the selection a #666666 DIFFERENCE layer, the playhead #9d9d9d) and without the Recording look; inside, as usual. `paintTracks` now paints each lane's wave, selection, caret and playhead through one `paintLane (muted, recording)` look, twice under clip regions (outside / inside the region) when a region is set; `drawWave` reads the clip it is given. **Judgment calls:** only while the Window's Loop is on (play-once ignores the region); a muted Window stays grey all over; the playhead inside the region keeps the normal white DIFFERENCE (Figma's Set-loop variants carry the muted #9d9d9d one, which looks copied from the Mute variant); the Recording × Set-loop look follows Figma although the Recorder ignores the region (Open questions). `--app-snapshot` now also writes `<name>-setloop.png` (region over the second half of the selection, 0.43–0.62): track 1 matches Sel=Yes Set-loop=yes (grey #999999 selection with a #5a5a5a wave outside, white inverted inside, black wave in the loop, grey after), track 2 Sel=No; colours equal Figma's composites. Recording looks not captured by `--app-snapshot` (needs a running take). Checked by the author in the dev build (2026-11-14): the outside of the loop draws muted, recording included. Tests green, standalone builds. **STEP 4 COMPLETE**: next is 5.
- 2026-11-14 · 5.1 · `se::TransportButtons` (`engine/TransportButtons.h/.cpp`, JUCE-free): the lights (Record off/armed/on, Stop latched, Play, Pause, held FF, Loop; Direction, FF and Loop disabled while Record is armed or on, their values kept) and, for each press, the ordered `TransportCue`s the Window will turn into engine and Recorder calls (play, resume, pause, stop, takeStart/Pause/Resume/End, fastOn/Off). Initial state: all off but Loop. **Judgment calls:** Pause with nothing rolling does nothing (as today); FF from the initial state plays and lets go back to the initial state (Stop unlit); any other press while FF is held lets FF go where it is, with no restore; a pass that runs out (`engineChanged`, edge-triggered so a not-yet-applied cue is not undone) or a punch-out (`ended`) latches Stop; a take that cannot start (`takeRefused`) puts Record off. `transportButtonsTests` in `run_engine.cpp`: initial state, then one block per row of the spec table (Record arm/disarm/arm-while-playing/disarm mid-take/⇧R from every state/refused, Stop, Direction, Play, Pause with the re-arm and the held take, FF in its Play/Pause/Stop cases plus initial and interrupted holds, Loop) and the engine-following edges; three mutations (Pause not re-arming, FF restoring Stop from the initial state, arming while playing not starting) each fail. Not wired yet. Tests green, standalone builds. · (this commit)
- 2026-11-14 · 5.2 · **Wired** `se::TransportButtons` into `WindowComponent` (`applyCues`; `togglePlayPause` is gone): the Record button and **Sound ▸ Arm Record R** arm/disarm, **Direct Recording ⇧R** records at once, Play / Space on armed starts the take, Pause re-arms (Record blinks) and Play carries on the same take, Stop latches (drawn lit) and resets, Direction / FF / Loop greyed while armed or on (menus too), the initial state on create and open is the model's (all off, Loop on). The Window follows the engine on edges (Keyboard play, a pass running out latches Stop) and the Recorder's end (`takeFinished`: a punch-out ends as a Stop). The direction a take forces to Forward comes back when it ends. Engine: **`Engine::recordJoin`** starts a take on a rolling or paused transport without restarting it (Forward re-anchor, a paused one resumes) and publishes the frame it joined at (`captureStartFrame` / `captureStartGen`, `Transport::readingFrame`); the Recorder waits for it and writes from there (new tracks: silence up to it; a span: entered at that point, punch-out unchanged). **`Engine::recordEnd` / `Recorder::end`** (disarm) stop the capture only: playback carries on untouched, the written tracks stay excluded until the next Play or Stop (no mid-pass entry, so no click), and the loop's live re-aim, which used to be frozen on `excludeMask`, now waits on a separate `Transport::takeOpen` that the disarm clears. **Set Loop (open question, option A):** Loop on and a region set, the take stays inside the region round and round (selected tracks, or new tracks padded to the region's end); Loop off ignores it as before. **Judgment calls:** Esc for Stop not drive-tested (synthetic Escape never arrives); a greyed Loop cannot show its kept value (the design system has no disabled-lit state), the menu still ticks it. `takeJoinTests` (+49 checks: join playing/paused, the joined frame, silence before it, end mid-take keeps the playhead and the output, end on a paused take, joined selection punch, region take wrapping, region off with Loop off, region + selection), case 10 of the deselect tests now on `takeOpen`. TSan and ASan clean (engine + state). Drive-tested in the dev build: R and click arm (blink, FF/Loop/direction greyed), Play and Space start a take, click and R disarm mid-take with the playhead running on, Space pauses (Pause lit) and resumes the same take, Stop latches, ⇧R from Stop, R while playing joins (new tracks start where it was pressed, other playheads untouched), a Set Loop take stays in the region, menus tick and grey as above, New Sound starts in the initial state. Tests green, standalone builds. · (this commit)
- 2026-11-14 · 5.3 · **Momentary FF.** The FF button is held, not clicked (`FastForwardHold` mouse listener: down on press, up on release wherever the mouse is); F is held through `ApplicationCommandInfo::wantsKeyUpDownCallbacks`, so JUCE's `KeyPressMappingSet::keyStateChanged` sends F down and F up (auto-repeat ignored), the editor takes the bare F key press so it does not fall through to a beep, and F's release goes to the Window it went down on even if the selection moved. `WindowComponent::fastForward (down)` runs `TransportButtons::ffDown/ffUp` through `applyCues`, so the release restores the state FF was pressed in: from Play normal speed again, from Pause back to Pause (where it got to), from Stop a Stop (playheads reset), from the initial state the initial state. `advanceEnabled` is gone; the FF light and the menu tick are `buttons.ffOn()`. **Judgment call:** the Sound ▸ Fast Forward menu row cannot be held, so it engages FF and the next choice lets it go. No engine change (the cues use `advance` / `normalSpeed` / `pause` / `stop`); 5.1's FF rows cover the model. Drive-tested in the dev build: mouse hold from Stop (Play and FF lit, Stop off, playhead at 2×; released: Stop lit, playhead reset), F held from Play (35 px in 2 s against 16 px at 1×, Play stays on after), F held from Pause (resumed 33 px further on, back to Pause), the menu row ticks and lets go. Not heard (no audio check here). Tests green, standalone builds. **STEP 5 COMPLETE**: next is 6.
- 2026-11-14 · 1.1 fix · The Effects panel header kept its own copy of the old slot-square code (digit 1 px high, unrounded x), found by the author. Both it and the Window status row now call one `os8::paintSlotBox` (`ui/Os8.cpp`). Capture of the previous build showed the header's "6" a row above the Window's; `--effects-snapshot` header now has the digits on rows 4–10, as the Window. Tests green, standalone builds. · (this commit)
- 2026-11-14 · 5 (author's notes) · From the author's test of step 5: (1) a disarmed take is **heard at once**: when the Recorder publishes it, the new `Engine::recordRelease` (queued after the take's SwapDoc) calls `Transport::releaseExcluded`, which lifts the exclusion and fades those tracks in over ~5 ms (blended against the mix that left them out), so no Stop is needed and nothing clicks in; a paused take is released at once and is heard on resume. (2) Record's **On look** is Figma's `761:17281`: Active's bevel on a #fd0000 face with a white dot (`os8::TransportState::recordOn`; an armed Record blinks between it and Normal). (3) **Double-click on Stop** puts the caret at 0, in the tracks it was in (the first click stops). (4) **Pause is never greyed**: off when stopped (a press there still does nothing). Tests: the disarm test now expects the take released and heard; a direct `releaseExcluded` test checks the ramp (no frame step over 0.01, full level after 5 ms), and removing the fade fails it. TSan and ASan clean. **Not drive-tested:** this time neither injected clicks/keys nor accessibility presses reached the running app (alive, idle); the author tests it. Tests green, standalone builds. · (this commit)
- 2026-11-14 · 5 · Tried by the author in the dev build after the notes above: all good. **STEP 5 COMPLETE** (confirmed): next is 6.
- 2026-11-14 · 5 (paused playhead) · The author wants paused playheads drawn: `WindowComponent::playheadXs` now draws them unless the transport is stopped (a paused transport keeps the positions it last published), so Pause, a held take and FF let go back into Pause show where playback will carry on. Open question closed. Not drive-tested (input still did not reach the running app); the author checks it. Tests green, standalone builds. · (this commit)
- 2026-11-14 · 5 (paused playhead) · Checked by the author in the dev build: all good.
- 2026-11-14 · 6.1 · Re-synced `specs_v0-5-0.md` from `998:20646` (the selection-info section now has the icon list and the Space/letters "ISSUE", planned as 6.3). New `se::selectionToStart` / `selectionToWholeTracks` / `selectionToEnd` (`engine/Edit.cpp`). "The selection's audio" is the longest selected track (with a caret, the tracks the caret is in), snapped down to a frame; the track range and the Set Loop region are never touched; each returns false when nothing changes. **Judgment calls:** start on a selection keeps its end (on an empty track too: it is a time range), a caret goes to 0; whole tracks does nothing when every selected track is empty; end shortens a selection that runs past its audio, and leaves alone one that starts at or past that end (the end would fall before the start); a caret in an empty track goes to 0. `selectionInfoTests` (28 checks: uneven 100/60/0/80 tracks, selections and carets past a track's end, empty tracks, empty sound, a 16-bit ragged tail, the region untouched, and through `Transport`: a playing Set Loop region keeps looping while a playing selection loop re-aims at the whole track); two mutations (the whole sound's length instead of the selection's, no start guard) fail 14. Tests green, standalone builds. · (this commit)
