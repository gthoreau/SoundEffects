# SoundEffects 0.6.0 — working plan

This file is the durable memory of the 0.6.0 work. **Every session starts by
reading it**, and every sub-step that lands updates it in the same commit.

- What to build: [`specs_v0-6-0.md`](../../specs_v0-6-0.md), a local snapshot of the
  Figma Specs block "To-do v.0.6.0" `998:20646` (file `QmG2Chvizn1QXGPwQVBgnn`; the
  0.5.0 menus block `1078:6637` is gone). Figma stays the source of truth. Re-sync the
  snapshot whenever Figma changes; read it with `use_figma` →
  `figma.spec.getContent({ specNodeId: '998:20646' })`.
- What it must look like: the Figma design system (node index below). Visuals are read
  with `use_figma` + `node.screenshot()` inline.
- Branch: `v0.6.0-dev`, cut from `main` at `024c65a` (0.5.0 + README/manual). `main`
  stays shippable 0.5.x and is merged at release.
- Rule: **one sub-step = one green commit**: `native/tests` pass and the standalone
  builds, then the commit is pushed to `origin/v0.6.0-dev` with its checkbox ticked and
  one journal line. **One step per session.** A step is complete only when the author
  has tried it in the dev build.
- Version: **0.6.0** is right — `git log v0.5.0..main` holds only README and manual
  commits, and 0.6.0 brings several new capabilities.

## Decisions (settled — don't re-ask)

Agreed with the author on 2026-11-27: he answered four questions himself (marked
"Settled (author)") and confirmed every other row as proposed.

| Topic | Decision |
|---|---|
| Order of work | Small fixes → side-column layout → **declicking** (engine, before multi-selection and MIDI mapping multiply starts and stops) → effects → popups → Magnetophon → multi-selection → MIDI mapping → release. |
| Declick model | One engine-level rule: **no playback event may jump the output.** Every start, stop, pause, resume, loop wrap, keyboard strike / retrigger / release and gain switch (mute, solo, Main Mute) goes through a short ramp or crossfade. One shared length, `se::kDeclickMs` = **5 ms** (to tune by ear). Stop and Pause keep their meaning; the transport flips after the fade-out, so the UI's playhead stops ≤ 5 ms later. Recording and the Magnetophon's capture are untouched (they record, they don't play). Since 2026-11-27 the rule also covers the tape's playback and the tracks' L / R toggles (3.6, 3.7). |
| Loop wraps | Each lap's last 5 ms crossfade into the start of the next (per track, own-length loops, the selection and the Set Loop region alike). The audio file is never changed; Zero Crosser stays useful but is no longer needed for a clean loop. |
| Popup frame | One new frame for every dialog (`1447:9163`): a Mac OS 8 movable modal window, centred title in the title bar, labels in **Headlines** (12). Dragged by the title bar, kept on screen, still opened where 0.5.0 opens it. Return / Esc as before. |
| Clean conversion | Checked by default **every time** a popup offers it (Select Audio Source, Send to Canvas, Import if it has one). The last choice is not remembered. |
| Alerts | Own OS8 alert (`1340:8550`) replaces `juce::AlertWindow` for New / Open / Revert / Quit. "Don't show again" is one pref per alert; while it is set, that action goes ahead without asking. **Settled (author, 2026-11-27): there is no way back in the app** — once hidden, an alert stays hidden (only deleting the settings file restores it). |
| Setups sheet | Save setup as / Manage Setup (`1447:9215`) slide down from the top of the open tray over its settings (~150 ms down, same up), with a 25 % black veil over the rest of the tray (`1458:12238`); nothing else in the app is blocked. "Delete setup…" becomes "Manage setup…": rename + Save, or Delete (no extra confirm). A rename onto an existing name uses 0.5.0's Replace flow. |
| Export Sound | One popup, format menu `.SEFX file` / `.WAV stereo mixdown` / `.WAV mono stems` (`1447:9002`), then the file chooser. **Mixdown** = what the Sound's mixer strip outputs: each track to its L / R / L+R, through the strip's volume and balance, Main Level and Main Mute excluded, the Sound's own mute and solo ignored; at the Sound's rate and bit depth; length = longest track; clipped, not normalised. **Stems** = one mono WAV per track, raw (pre-fader), same rate and depth, in a new folder `<Sound name> stems/`, named `<Sound name> - track T.wav`. |
| Select Audio Source meter | The monitor fader's trough shows a live stereo VU of the chosen source, as the Mixer strips do, also while not recording. |
| Audio/MIDI Setup | Standalone only (`1447:9160`): our own OS8 dialog over `juce::AudioDeviceManager` replaces JUCE's selector — Feedback Loop (mute audio input), output + Test, input + meter, output and input channel pairs, sample rate, buffer size, MIDI inputs. Same settings file as today, so nothing is lost. |
| Waveform Generator | Wave becomes a dropdown (`507:5908`, menu `1447:9097`): Triangle (default), Sawtooth, Sine, Square. Triangle is a new value (`kTriangle = 4`); the stored values 1–3 keep their meaning so saved setups load unchanged. Triangle is `[NATIVE]` (no 0.9.2 reference). |
| Echo bug | Diagnose first against the recovered 0.9.2 code (`disassembly/`) and the TS reference; fix with a golden-vector or fixture test. Likely suspects: "Only N" only caps the tail past the selection, and does nothing when the tail is not extended. |
| Multi-selection model | A canvas-level group: **one primary** (the last clicked member: what the Effects panel, the selection fields, the Sound menu's other items and the Keyboard target) plus any number of members. ⇧-click adds or removes; a plain click on a member makes it primary and keeps the group; a plain click on a non-member or on the empty canvas ends the group. Not saved in the .sefx. Closing a member removes it. |
| Multi-selection look | **Settled (author, 2026-11-27):** every member's title bar is drawn active (stripes), the primary's frame as today, and members' Mixer strips highlighted like the selected one. No Figma design to wait for. |
| What propagates | Mute, Solo, Stop, Play, Pause, Direction, FF (held), Loop, Keyboard (mouse, computer keys, MIDI). It propagates the **resulting state**, not a toggle: Loop on → every member's Loop on; Mute on → every member muted. Play / Pause / Stop reach all members **in the same audio block** (one engine command), so loops started together stay in phase. Record is not shared; a member whose Record is armed ignores what Record disables (Direction, FF, Loop). |
| Disabled in a group | Sound ▸ Color ▸ and Rename… greyed (and the title-bar rename) while the group has 2+ members. |
| Solo swap | ⌘-click on an **un-soloed** solo box (Mixer strip, Sound title bar, Magnetophon): clear every solo, then solo it. ⌘-click on a soloed box keeps 0.5.0's "clear all solos". |
| Main Mute | Engine atomic applied with Main Level (ramped), `M` box in the Main Level footer (`1083:14659`). **Not saved**: a canvas always opens unmuted. |
| Magnetophon Solo | The tape is a solo source like a Sound: any solo silences every unsoloed Sound and the tape unless the tape is soloed too. ⌘-click swap applies. Not saved (the tape isn't). |
| Delete take | Hover a take → trash icon (`1307:6706`); click deletes its audio, later audio closes up, take numbers follow. No undo, no alert. Greyed while recording; if the tape is playing inside that take, the playback moves to the next take's start. |
| Send to Canvas | New design (`1447:9158`), all takes **unchecked** by default, the primary button reads **Send** (the design says "Import"; Figma to fix). |
| Side column | Mixer title bar stays at the top when collapsed; Magnetophon pegged to the bottom; MIDI Mapping pegged on top of the Magnetophon; the Mixer list takes what is left. The spec says "right side panel": it means the column where the Mixer and Magnetophon already are. |
| MIDI notes | **MIDI 12–23 = C0–B0**, in the palette's own naming (its lowest key, MIDI 24, is C1). Those 12 notes never reach the Keyboard; everything from 24 up is unchanged. **Settled (author, 2026-11-27)**, knowing that Ableton names these notes its "C-1" row. |
| MIDI mapping model | 12 pads, each: a Mixer slot number (or none), an action at note start (Play / Stop / Pause) and at note end (Stop / Pause / Play). Start Stop or Pause forces end Play; start Play allows end Stop or Pause (Play greyed). Bound to the **slot**, so reordering the Mixer changes the Sound (as the spec says). |
| Mapping conflicts | Per Sound, the most recent note wins: a note-end only acts if that note is still the latest one holding the Sound (the Keyboard's held-note stack rule), so overlapping notes on one Sound don't cut each other off. A repeated note-start restarts. A slot with no Sound does nothing. |
| Mapping timing | Notes go through 0.5.0's MIDI queue and pump to the message thread, then into the same transport path as a button press: ≤ ~5 ms + one block late. Moved onto the audio thread later only if the author hears drift against a sequencer. |
| Mapping and groups | A mapped note acts like pressing that Sound's button, so if the Sound is in a multi-selection it propagates to the group. |
| Mapping persistence | Saved per canvas in the .sefx (**v6**), hence also in the plug-in's state. Older files open with an empty map. |
| New canvas | **Settled (author, 2026-11-27):** New Canvas and first launch open an **empty canvas, no Sound at all**. The processor stops adding a `newSoundInit()` Sound; File ▸ New Sound still makes one with one empty track. |

## Figma node index

| What | Node |
|---|---|
| Spec | To-do `998:20646` |
| Popups board | `636:21366` |
| Popups (component set) | `1447:9163`: audio-midi-setup `1447:9160`, select-audio-source `1447:9161`, import-sound `1447:9162`, export-sound `1447:9154`, leave-silence `1447:9159`, shift-sound `1447:9157`, select-peak `1447:9155`, select-exactly `1447:9156`, send-to-canvas `1447:9158` |
| Alerts | `1340:8550` (revert-canvas, open-canvas, new-canvas, quit) |
| Setups | set `1447:9215` (save-setup-as, manage-setup); in a tray `1458:12238`; Setup menu `543:10113` |
| Export formats menu | `1447:9002` |
| 1-click effects tray | `543:9781` |
| Waveform Generator tray / Wave menu | `507:5908` / `1447:9097` |
| MIDI Mapping panel / pads | `1451:10974` / `1451:10840` |
| Main Level footer with Mute | `1083:14659` |
| Magnetophon / Reels / Info / tape / Transport | `1307:6817` / `1307:6790` / `1307:6711` / `1307:6706` / `1307:6717` |

The 0.5.0 and 0.4.0 indexes (`docs/0.5.0/PLAN.md`, `docs/0.4.0/PLAN.md`) still hold for
everything else.

## Where things are (found 2026-11-27)

- **Popups:** `ui/Os8Popup` (the 0.5.0 frame, "reads like an effect tray", `showOver`), 25 call sites: `ui/EditPopups`, `ui/FilePopups` (Export / Import), `ui/TapePopups` (Send to Canvas), `ui/AudioSourcePopup` (Clean conversion `clean = false`), `EffectsPanel.cpp` (Save setup as / Delete Setup at ~1356 and ~1509).
- **Alerts:** `StandaloneApp.cpp` `askThenDo` → `juce::AlertWindow::showScopedAsync` (~502, ~515); New / Open / Revert / Quit with `tapeLostNote`. One `showMessageBoxAsync` in `PluginEditor.cpp` ~925.
- **Audio/MIDI Setup:** `StandaloneApp.cpp` `pluginHolder->showAudioSettingsDialog()` (JUCE's selector), reached through `PluginEditor::onShowAudioSettings`.
- **Echo:** `effects/modules/EchoDelay.cpp` (`onlyOn` / `onlyCount`, `mixLimit`, ~126–145); layout `ui/EffectLayouts.cpp` ~205.
- **Waveform Generator:** `effects/modules/Waveform.cpp` (`waveSample`, `waveType` 1 Sine / 2 Square / 3 Sawtooth); layout `ui/EffectLayouts.cpp` ~324 (three radios); defaults ~426.
- **1-click group:** `EffectsPanel.cpp` `autoApply` toggle (~434), pref `AppPrefs::autoApply`.
- **Selection of Sounds:** single `CanvasComponent::selectedId` / `selectedWindowId()`, fanned out in `PluginEditor.cpp` ~77–83 to effects, mixer and keyboard; the menus read `selectedWindowId()` throughout `PluginEditor.cpp`.
- **Mute / Solo:** `se::Window::muted` / `soloed` atomics (`engine/Engine.h`); `Engine::anyWindowSoloed` / `clearAllSolos` / `unmuteAll`; Mixer strip clicks `MixerComponent.cpp` ~405 (⌘ = clear all); Sound menu ~420 in `PluginEditor.cpp`.
- **Transport:** `engine/Transport.cpp` — `stop()` and `pause()` switch at once (no fade); a ~2 ms crossfade exists for direction changes (`startCrossfade`, `xfadeLeft`) and a fade-in (`fadeInLeft`); buttons through `se::TransportButtons` and `WindowComponent::applyCues`.
- **Keyboard voice:** `engine/KeyboardVoice` (`strike` / `release`, `kLowNote = 24` = C1, notes below ignored), owned by the processor; MIDI via `MidiQueue` → `MidiPump` → `MidiKeyboardInput`.
- **Side column:** `PluginEditor::resized` ~1030–1054 (`mixerPanel`, `magnetophonPanel`, `SidePanel::kCollapsedHeight`); today a collapsed Mixer pulls the Magnetophon up under its title bar.
- **Main Level:** `Engine::setMasterGain`, footer in `MixerComponent` (`mainLevel`, `kFooterHeight`).
- **Magnetophon:** `ui/MagnetophonComponent`, model `engine/Tape.h/.cpp` (`se::Tape`, `se::TapeDeck`), audio `engine/TapeStore.h`.
- **New canvas:** `StandaloneApp::newCanvas` → `resetToDefaultState` → new processor, whose constructor (`PluginProcessor.cpp:14`) adds one `newSoundInit()` Sound ("untitled sound", one empty track).

## Steps and sub-steps

Legend: `[ ]` todo · `[~]` in progress · `[x]` done (see the journal).

### 0. Setup
- [x] 0.1 Branch `v0.6.0-dev` from `main`, `specs_v0-6-0.md`, this plan with the decisions agreed

### 1. Quick fixes
- [x] 1.1 **Echo "Only N echoes"**: reproduce, find the cause against the 0.9.2 recovery, fix, fixture test (N = 1, 3, with and without tail extension)
- [x] 1.2 **Clean conversion** checked by default everywhere it appears
- [x] 1.3 **Solo swap**: ⌘-click on an un-soloed box (strip, title bar) clears all solos and solos it; engine test
- [x] 1.4 **Main Mute**: engine flag (ramped with Main Level), `M` box in the footer per `1083:14659`, not saved
- [x] 1.5 **New canvas** opens empty (no Sound); check every panel, menu and the plug-in with zero Sounds

### 2. Side column
- [x] 2.1 Mixer title bar stays at the top when collapsed; Magnetophon pegged to the bottom; a reserved slot above it for MIDI Mapping (empty until step 8); every collapse combination drive-tested

### 3. Declicking (engine)
- [x] 3.1 **Measure first**: engine test scenarios rendering a DC-offset and a full-scale sine through stop, pause, resume, play from the caret, loop wrap off a zero crossing, keyboard strike / retrigger / release, mute / solo / Main Mute; each reports its largest sample-to-sample jump. Pins today's numbers.
- [x] 3.2 **Stop and Pause fade out** over `kDeclickMs`, then switch; start and resume fade in (generalise the direction crossfade); the Transport UI state stays immediate
- [x] 3.3 **Loop wraps** crossfade per track (own-length loops, selection, Set Loop region, both directions, FF)
- [x] 3.4 **Keyboard voice**: strike fades in, retrigger crossfades, release fades out, mouse and MIDI alike; MPE re-rate stays smooth
- [x] 3.5 **Gain switches** ramped: mute, solo, Main Mute, fader jumps; every 3.1 scenario under a threshold. The author listens (KeyStep, Push 3, loops, Stop / Pause)
- [x] 3.6 **The tape's playback**: Magnetophon play, stop, jump, scrub, end of tape and Clear without a click (asked by the author after listening to 3.1–3.5); capture untouched
- [x] 3.7 **Track L / R toggles** ramped while the Sound plays (same)

### 4. Effects
- [x] 4.1 **1-click trays** laid out per `543:9781` (title, Auto-Apply left, Cancel / Apply right)
- [x] 4.2 **Waveform Generator**: Wave dropdown (`507:5908`, `1447:9097`), new Triangle (default); fixture for Triangle; old setups load unchanged

### 5. Popups redesign
- [ ] 5.1 **The new frame** (`Os8Popup` reworked: title bar, movable, Headlines labels, placement as 0.5.0) and the four edit popups on it: Leave Silence, Shift Sound, Select Peak, Select Exactly
- [ ] 5.2 **Alerts** (`1340:8550`): own OS8 alert for New / Open / Revert / Quit, "Don't show again" prefs; the plug-in's message box too
- [ ] 5.3 **Setups sheet**: slide-down Save setup as / Manage Setup over the tray with the 25 % veil; "Manage setup…" in the Setup menu (`543:10113`); rename and delete
- [ ] 5.4 **Select Audio Source** on the new frame with the live stereo VU; **Import Sound** and **Send to Canvas** (`1447:9158`, takes unchecked) on the new frame
- [ ] 5.5 **Export Sound**: format menu; JUCE-free `se::renderMixdown` and `se::splitStems` (`engine/`) with tests (routing, balance, volume, length, bit depth); file / folder writing
- [ ] 5.6 **Audio/MIDI Setup** (`1447:9160`): own dialog over `AudioDeviceManager`; drive-tested with the built-in output and the author's interfaces and MIDI devices

### 6. Magnetophon
- [ ] 6.1 **Visuals**, pixel-perfect against `1307:6817`: bigger reels (`1307:6790`, rotation only), tape path, pinch rollers, head, playhead; counter centred on the playhead (`1307:6711`); 1× and 2× captures compared with Figma
- [ ] 6.2 **Delete a take**: `se::Tape` removes a take's audio (tests: first / middle / last take, during playback, positions and numbering), hover trash icon
- [ ] 6.3 **Solo** in the transport bar (`1307:6717`) under the solo rules; engine tests with Sounds soloed / not

### 7. Multi-selection of Sounds
- [ ] 7.1 **Pure model** `se::SoundGroup` (JUCE-free): add / remove / primary / end, closing a member; tests for each rule in the spec
- [ ] 7.2 **UI**: ⇧-click on Windows and Mixer strips, plain-click rules, the group's look on the canvas and in the Mixer; Color / Rename greyed
- [ ] 7.3 **Propagation** of Mute, Solo, Stop, Play, Pause, Direction, FF, Loop from buttons, keys and the Sound menu; one engine command so members start in the same block; tests that members stay phase-locked
- [ ] 7.4 **Keyboard to the group**: mouse, computer keys and MIDI (MPE included) strike every member at the pitch; release stops them together

### 8. MIDI Mapping
- [ ] 8.1 **Pure model** `se::MidiMap` (pads, constraints, conflict rule) with tests: one Sound on several notes, overlapping notes, Stop/Pause → Play, empty slot, reordered strips
- [ ] 8.2 **Routing**: MIDI 12–23 split off before the Keyboard, standalone inputs and DAW alike, applied through the transport path (and the group)
- [ ] 8.3 **Panel** `1451:10974`: collapsible, 12 note-pads (`1451:10840`) with slot numbers, selected-pad state, incoming notes lit, Sound dropdown, start / end radios
- [ ] 8.4 **Persistence**: .sefx v6 and plug-in state; old files load with an empty map; round-trip test
- [ ] 8.5 **Plug-in check** in a host: a clip of C0–B0 notes drives Sounds, window open and closed

### 9. Release
- [ ] 9.1 Re-sync the spec snapshot, version 0.6.0, README and manual, `package.sh`; tag, GitHub release and merge into `main`, each on the author's go-ahead

## Verification per sub-step
- Tests: `cmake -S native/tests -B native/tests/build -G Ninja && cmake --build native/tests/build`, then **from `native/tests`** run `./build/effect-fixtures`, `./build/engine-tests` and `./build/state-tests`. TSan / ASan for engine changes (steps 3, 6.2, 7.3, 8.2).
- Standalone build: `cmake --build native/build --target SoundEffects_Standalone`.
- UI: capture the running standalone at 1× and compare with the Figma node (method in `docs/0.4.0/PLAN.md`, "How to compare with Figma"); drive-test every interaction in the app.
- Audio: step 3's numbers, then the author's ears.

## Open questions (for the author)
Answered 2026-11-27 and moved into Decisions: new canvas (empty), multi-selection look
(active title bars), MIDI octave (12–23), alerts (no way back).

1. **Figma text fixes** (to make in Figma whenever the author likes; the app uses the corrected wording) (spotted while reading): alerts "This cannot be **indone**" (open, new, quit), "**Magnetophon'** tape" and its button reads "**New canvas**" in the Quit alert (should be "Quit"); Audio/MIDI Setup "**Intput** channels"; Select Exactly "**Centerd** on"; Send to Canvas button "**Import**" (proposed "Send") and its design shows takes 1–2 checked while the spec says unchecked. The app will use the corrected wording.

## Journal
One line per finished sub-step: date · sub-step · what was verified · commit.

- 2026-11-27 · 0.1 · Read the Specs block `998:20646` ("To-do v.0.6.0"; the 0.5.0 menus block `1078:6637` is gone) and the 19 nodes it links to with `use_figma`; `specs_v0-6-0.md` snapshot; this plan. Decisions agreed with the author: new canvas empty, group look = active title bars, MIDI mapping on notes 12–23, hidden alerts never come back, every other proposed row confirmed. Branch `v0.6.0-dev` from `main` `024c65a`. No code change, so no build or tests. Next is 1. · (this commit)
- 2026-11-27 · 1.1 · Cause: 0.9.2 only uses N to size the range of the recursive pass, which is clamped back to the selection end when the sound may not get longer, so the box did nothing there and N was never the count heard (layout keys were right). `[NATIVE]`: exactly N echoes of the selection (max 80), past-selection ones dropped without extension; SPEC §8 #9, TS reference and C++ changed together; fixtures add N = 1 and 3, with and without extension, over-100 % strength and the cap (214/214); npm 578, engine and state tests pass; Only 1 applied in the standalone. · (this commit)
- 2026-11-27 · 1.2 · Select Audio Source (per Sound) and Send to Canvas open with Clean ticked every time; Cancel leaves the Sound's mode alone, a kind change keeps the tick as set. A Sound recorded without opening the popup now converts Clean too. The Magnetophon's source popup no longer shows the box (it only preset Send, which is no longer remembered); Import has none. Tests pass; both popups, reopen after unticking, drive-tested in the standalone. · (this commit)
- 2026-11-27 · 1.3 · `Engine::clickSolo` (plain toggle; ⌘ on lit clears all; ⌘ on unlit solos it before unsoloing the rest, so no block sees the filter off), used by the Mixer strip and the title-bar box; engine test of each click with levels; tests pass; swap and clear drive-tested on strips and title bars. Sound ▸ Solo unchanged. · (this commit)
- 2026-11-27 · 1.4 · `Engine::setMasterMuted`: Main Level's one-block ramp pulled to zero, the same ramps applied to what is added after the fader (input monitors, tape playback and scrub, which stepped per block before); it silences captures from Main as Main Level does; not in the blob and any canvas load unmutes. `M` at (8,17), label at x 28, per `1083:14659`. Engine and state tests (ramp, no jump, transport keeps playing, not saved); all tests pass; lit box and silent Main VU while a Sound plays drive-tested. · (this commit)
- 2026-11-27 · 1.5 · The processor no longer seeds a `newSoundInit()` Sound (New Canvas, first launch, plug-in); File ▸ New Sound and the Mixer button still make one with one empty track. Zoom Sound ▸ greys with no Sound. Engine (silence, keys, solo) and state (round trip, opening it empties) tests; all tests pass. Drive-tested empty: menus, Effects greyed (open tray too), Keyboard, Magnetophon source / record / Send, canvas controls, New Sound, a first launch with both settings files moved aside (restored). Plug-in not loaded in a host (shares processor and editor). First launch after an upgrade from the singular `SoundEffect.settings` still imports that canvas. · (this commit)
- 2026-11-27 · 1 · The author tried step 1 in the dev build: all good. Step 1 complete; next is 2. · (this commit)
- 2026-11-27 · 2.1 · Magnetophon pegged to the column's bottom in `PluginEditor::resized`; the slot above it is step 8's (one more `peg` call, no height today). A shaded Mixer still gives the canvas the column, so its bar floats top-left and the Magnetophon bottom-left; `[NATIVE]` the View palette and Keyboard then move right of the floating Magnetophon while the viewport has room (under ~870 px wide the Keyboard overlaps it, in front). Tests pass, standalone builds; all Mixer × Magnetophon states, Effects shaded, side panels hidden and shown, the 640 × 420 minimum and a relaunch with a shaded Mixer drive-tested. Plug-in not loaded in a host. · (this commit)
- 2026-11-27 · 2.1 · The author tried it: the canvas shifted left and right with the Mixer. `CanvasComponent::setBoundsKeepingView` now offsets the pan, so panels opening, closing or hidden move no Sound on screen; tests pass; Mixer, Effects and hide / show side panels drive-tested. · (this commit)
- 2026-11-27 · 2 · The author tried step 2 in the dev build, the canvas fix included: all good. Step 2 complete; next is 3. · (this commit)
- 2026-11-27 · 3.1 · `tests/run_declick.cpp` (in engine-tests): 30 events on a DC offset (0.5) and a full-scale 100 Hz sine, largest sample jump pinned. Today: stop, pause, resume, caret play, once running out, closing a Sound, keyboard strike / release, mute, solo, fader and balance all jump 0.5 / 1.0; loop wraps 0 / 1.0 (DC 0.5 where a track is short or comes in late); Main Mute and Main Level already ramp (≤ 0.013). Found: a key struck again before the audio thread has seen its release stays silent (keyboard and MIDI retrigger). All tests pass; no product code changed. · (this commit)
- 2026-11-27 · 3.2 · `engine/Declick.h` (`se::kDeclickMs`). `se::Transport` is now a live voice that fades in plus up to 8 fading tails: stop, pause, a new play, seek, re-anchor (the direction crossfade, now 5 ms) and the take's excluded tracks all go through it. Transport state is immediate; a pause leaves the playhead where the fade ends, except the Recorder's, which keeps its place so takes stay in step. A play-once ramps out its last 5 ms; a Sound closed while playing fades before it is freed. Events now ≤ 0.013 (the sine's own slope). Mapping tests run with ramps off (`setDeclickMs (0)`). Tests, TSan, ASan pass; standalone builds. · (this commit)
- 2026-11-27 · 3.3 · `se::trackSampleAt`: per track, the last 5 ms of each lap bend to what leads up to the next lap's start (the start carried back one frame along its slope), so the lap keeps its length and attack and a loop that already joins cleanly is unchanged. `[NATIVE]` choice over overlapping tail and head, which would shorten every lap and shift loops' phase; a loop under 10 ms is ramped over half of itself. Also a short track's end inside a loop fades, and a track coming in late from a caret fades in; ping-pong needs nothing. Ramp time is in output time at any pitch. Wrap scenarios ≤ 0.035 (FF, MPE slide) from 1.0. Tests, TSan, ASan pass; standalone builds. · (this commit)
- 2026-11-27 · 3.4 · Strike fade-in and release fade-out come from the transport. Fixed: a key struck again before the audio thread had applied its release took its own stopping Sound for a rolling one and stayed silent; `Engine::isRolling` now counts a posted Play / Stop, so mouse and MIDI retrigger and cross over the old fade. A new note while one is held still only changes speed (no jump), MPE slides too (≤ 0.035). Tests, TSan, ASan pass; standalone builds. · (this commit)
- 2026-11-27 · 3.5 · `se::Ramp`: mute, solo, fader, balance, Main Level and Main Mute travel over 5 ms at any block size (Main used one block); capture taps follow the same fader ramps. Every declick scenario now has to stay under 0.04 (worst 0.035, MPE slide; before 1.0), plus hammering tests for transport and gain. Not covered: the tape's own play / stop, effects applied while playing, per-track L / R toggles. Tests, TSan, ASan pass; standalone builds. The author has not listened yet. · (this commit)
- 2026-11-27 · 3.1–3.5 · The author tried them in the dev build: "sounds really good". Asked for the same on the Magnetophon and the per-track L / R toggles: 3.6 and 3.7 added. · (this commit)
- 2026-11-27 · 3.6 · The tape's playback fades in; stop, jump, take change, record-over and a grab by the scrub copy the next 5 ms aside as a fade-out (`Engine::fadeOutTape`), so nothing reads the tape afterwards; the end of the tape ramps out. Clear no longer shortens the store at once: it is freed when the audio thread has let go, so a playback or scrub fades. Seven tape scenarios ≤ 0.013 and a hammering test; tests, TSan, ASan pass; standalone builds. Not heard by the author yet. For 6.2: deleting audio under a playing tape must fade the same way. · (this commit)
- 2026-11-27 · 3.7 · A track's L / R share is eased over 5 ms in the transport's mix (live voice and fading ones alike); with nothing sounding it applies at once, so a toggle made while stopped is in force from the first sample. Toggle scenarios ≤ 0.013 and a hammering test; tests, TSan, ASan pass; standalone builds. Not heard by the author yet. · (this commit)
- 2026-11-27 · 3 · The author tried 3.6 and 3.7 in the dev build: all good. Step 3 complete; next is 4. · (this commit)
- 2026-11-27 · 4.1 · The 1-click tray is the title, then one row: Auto-Apply left, Cancel / Apply right (78 px high, matching `543:9781` in an `--effects-snapshot` capture); the "Apply X." sentence is gone. `[NATIVE]` Weird Tempo keeps its warning between title and buttons (the design draws none). Tests pass; box (in step with the group's), Apply, Cancel, Return and the warning tray drive-tested in the standalone. · (this commit)
- 2026-11-27 · 4.2 · Wave is a dropdown (Triangle, Sawtooth, Sine, Square; 98 px, `507:5908` / `1447:9097`) that the panel opens on Triangle. `[NATIVE]` `kTriangle = 4`, in phase with the sine; 1–3 unchanged, so saved setups load as before; `defaults()` stays Sine as in the TS reference, changed with the C++. New fixture (215/215), npm 578, typecheck, engine and state tests pass; menu, all four waves applied at 5 Hz, and saving and recalling a setup drive-tested in the standalone. · (this commit)
- 2026-11-27 · 4 · The author tried step 4 in the dev build: all good. Step 4 complete; next is 5. · (this commit)
