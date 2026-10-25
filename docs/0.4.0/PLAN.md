# SoundEffects 0.4.0 — working plan

This file is the durable memory of the 0.4.0 work. **Every session starts by
reading it**, and every sub-step that lands updates it in the same commit.

- What to build: [`specs_v0-4-0.md`](../../specs_v0-4-0.md). It is a local
  snapshot of the Figma Specs block (file `QmG2Chvizn1QXGPwQVBgnn`, node `752:9475`),
  and Figma stays the source of truth. Re-sync the snapshot whenever Figma changes.
- What it must look like: the Figma design system (see the node index below).
- Branch: `v0.4.0-dev`. `main` stays shippable 0.3.x and is merged at release.
- Rule: **one sub-step = one green commit**. That means `native/tests` pass and the
  standalone builds, then the commit is pushed to `origin/v0.4.0-dev` and the
  journal below gets one line.
- The plugin build stays frozen. Plugin-shared code may change, but no VST3/AU is
  built or installed.

## Decisions (settled — don't re-ask)

| Topic | Decision |
|---|---|
| Rust | Stay in C++/JUCE. Rewriting 15.6 k LOC plus 171 golden vectors, with no mature GUI and no AU in nih-plug, would eat the release. |
| Window chrome (standalone) | Hide the native title bar and seat the real traffic lights inside the app's top bar. The macOS menu bar keeps only the SoundEffects menu (About, Quit). |
| Menu | In-app `AppMenuBar` with File / Edit / Selection / Transport / Window, used in the standalone and the plugin. |
| Side panels | The Mixer (left) and Effects (right) panels are 270 px wide. Each has a collapse/expand button in its title bar; collapsed shows the title bar only. The button design is provisional. |
| Font | `~/Library/Fonts/ClassicChicago.ttf` is embedded via BinaryData. The author owns the rights. |
| Themes | 24 named `Window-themes/*` colours in one name+hex table, in index order. Old index N maps to new colour N. The theme fills the Window background, its two speaker buttons and **its mixer strip** (node `636:11566`). The waveform bed stays white. |
| Mixer strip tint | The strip is filled with the theme colour, and the sliders and VU trough use a darker shade of it. A muted strip is a washed-out pale version with grey text. |
| Mixer slots | A new Sound takes the lowest free slot; Close frees it. Empty slots between used ones show as empty numbered rows. Keys 1–0 focus slots 1–10. Dragging a strip to another slot leaves the canvas untouched. |
| Balance | Linear balance: unity at centre, the opposite side is attenuated. |
| Shortcuts | ⌘W closes, ⌘← / ⌘→ extend to beginning / end. ⌘␣ and ⌃␣ are taken by macOS (Spotlight, input source), so the 3 direction items stay in the menu, a single-key **D** cycles Forward→Backward→Ping-pong, and ⌥␣ = Ping-pong. Single-key shortcuts (R F L M S K G D ⇥ 1–0) work only when no text field has focus. |
| 1-click effects | Auto-apply is on by default and persisted app-wide. When it is off, a tray reads "Apply [name]." with Activate Auto-Apply, Cancel and Apply. Weird Tempo's warning shows in that tray. |
| Apply feedback | The button shows `Done!` for about 0.5 s (one constant), independent of compute time. |
| Bandpass IIR | Has its own tray in the design (`357:13366`). |
| Audio source naming | "Canvas" (not "Mixer"). When recording the main mix into a Window, that Window is excluded from what it records. |
| Recording conversion | Raw/Clean checkbox in Select Audio Source, Raw by default. Raw reuses `engine/Resample` (ZOH, truncation); Clean is band-limited with dither. |
| Recording | Forces Forward and disables the direction menu while recording. With no selection, new tracks start at 0. |
| Project format | A visible folder `<Name>/` holding `<Name>.sefx` (v2) and `Assets/`, with one mono WAV per Track (Tracks have independent lengths). The exact non-integer rate is stored in the .sefx. v1 files still open; saving a v1 file goes through Save As. |
| Export Sound | One self-contained `.sefx` with embedded audio and one Sound. Import accepts both kinds. |
| Out of scope 0.4.0 | The Next, Previous and Send-to-canvas transport icons. |
| Undo | Menu item always disabled ("that's the joke"). |

## Figma node index

| What | Node |
|---|---|
| App layout | `700:14314` |
| Example with tinted mixer | `636:11566` |
| UI Elements | `636:21143` (Buttons `87:8`, Checkbox `87:1401`, Radiobutton `292:4290`, Text field `327:4482`, Dropdown `328:5750`, Sliders `354:12896`, icons `208:5690`, Transport-button `761:17286`, Combo-play-button `795:16809`, Dir-menu `795:22732`, Canvas-toggle `527:5564`, Icon-button `527:5725`, VU-segment `534:11786`, Frames `103:8686`) |
| Menus | `636:21145`: main bar `700:14311`; File `569:13307`; Edit `569:12670`; Selection `569:13068`; Transport `569:13467`; Window `569:13665` |
| Sound Windows | `636:21151`: Sound-window states `135:4722`, Track `589:7036`, Transport-controls `589:8301`, zoom-controls `589:8166`, speaker-buttons `264:2474`, window-button `203:4347`, selection-info `526:4846`, sound-info `526:4845` |
| Side panels | `636:21156`: Mixer `636:10485`, Mixer-channel `509:9257`, Effects `636:14867`, Effects-group `527:5980`, Sound-info `636:15319` |
| Effect settings | `329:6501`. The 23 variants are generic-alert `543:9781`, Amplify `341:11421`, Delay `329:7014`, Difference `355:13019`, Dither `355:13155`, Downsample `355:13090`, Echo `329:6502`, Fade-in `356:13256`, Fade-out `356:13332`, BP-IIR `357:13366`, HP-IIR `799:15617`, LP-IIR `357:13539`, Shelving `357:13574`, Flanger `329:6500`, Granular `409:1813`, Iterative-crossfade `409:3758`, Quick-mix `409:3841`, Replicate `409:3930`, Resample-rate `415:4020`, Sample-hose `415:4184`, Upsample-bits `507:5876`, Waveform-generator `507:5908`, Zero-crosser `507:5994` |
| Popups | `636:21366`: Leave Silence `543:8056`, Shift Sound `543:9335`, Select Exactly `543:8171`, Select Peak `543:9266`, Select Audio Source `543:9503` |
| Canvas palettes | `636:21442` |
| Theme board | page `650:5398` |

Colour styles: `System/*` (selected-text-field #ccccff, selected-menu-item #333399,
transport-button-red #fd0000, white, gray-000 #eeeeee … gray-600 #333333, black) and
24 `Window-themes/*`. Text styles, all Classic Chicago at 98 % line height: Large Label 14,
Headlines 12, App Menu items 11, Small Label 10, X-small 9.

How to read Figma: through the Figma MCP. Use `use_figma` with `node.screenshot()` for
inline captures, and **do not curl-download assets**. Text layers can't be written
remotely because the font is local.

## Steps and sub-steps

Legend: `[ ]` todo · `[~]` in progress · `[x]` done (see the journal).

### 0. Rust evaluation
- [x] 0.1 Decided: stay C++/JUCE (2026-10-25)

### 1. Design-system foundations
- [x] 1.1 Embed ClassicChicago.ttf (`juce_add_binary_data`) and add `ui/Theme` text styles (14/12/11/10/9). Check crisp rendering at native sizes.
- [x] 1.2 `ui/Theme` colour tokens: the System colours plus the 24-theme name+hex table, replacing `kTints`. Index migration, with an engine/state test.
- [x] 1.3 `Sys7` → `Os8` look-and-feel: Buttons (Primary/Secondary × Default/Applied/Active/Disabled), Checkbox, Radiobutton
- [ ] 1.4 Text field (3 sizes × 4 states) with ↑/↓ ±step, ⇧ ×10 and unit-aware steps. Dropdown and mini-dropdown.
- [ ] 1.5 Sliders (Mixer-volume with VU, Mixer-pan, Amplify), Icon-button, Canvas-toggle
- [ ] 1.6 Icon set as vector drawings, plus Transport-button, Combo-play-button and Dir-menu
- [ ] 1.7 `AppPrefs` over ApplicationProperties (auto-apply, setups, time unit, panel visibility/collapse)

### 2. App shell and menus
- [ ] 2.1 Standalone window: hidden native title bar, traffic lights in the app bar (ObjC++ NSWindow tweak), `[canvas-name]` label
- [ ] 2.2 `AppMenuBar` component, with menu look per design (item states, checks, submenu arrow, shortcut column)
- [ ] 2.3 Single `ApplicationCommandManager` and shortcut table. Migrate the existing keyPressed handlers (PluginEditor, WindowComponent, CanvasComponent) and wire File, plus the Edit/Selection items that already exist.
- [ ] 2.4 Side panels: 270 px, window-bar title, collapse/expand button, Show/Hide Side Panels (⇥)

### 3. Sound Window
- [ ] 3.1 Title bar: close, stripes, name, M/S buttons, resize; Default/Hover/Active × Mute states (mute greys the waveform)
- [ ] 3.2 Sound-info header (slot number, kHz, bits, tracks, length s) and selection-info fields (start/length/end + s/ms/samples dropdown; start with length=end=0 sets the caret)
- [ ] 3.3 Track: L/R speaker toggles (4 combos), track-icon drag to move, drop on trash to delete, ⌥-drag to duplicate, new-track drag with ghost target
- [ ] 3.4 Bottom bar (zoom −/+/reset, arrows, blue scrollbar) and Transport bar (Source, Record, Stop, Combo-play, Pause, FF, Loop) with theme fill
- [ ] 3.5 Pixel-perfect pass against `135:4722` and `589:*`

### 4. Mixer
- [ ] 4.1 Engine: atomic per-window balance plus a stereo VU feed, with tests in `run_engine.cpp`
- [ ] 4.2 Strip per design: tinted, balance, fader+VU, M/S, states. Numbered static slots, drag between slots, persisted slot in .sefx.
- [ ] 4.3 Keys 1–0 focus a slot and its Window. Pixel-perfect pass.

### 5. Effects panel
- [ ] 5.1 Panel frame: Sound-info header, 6 groups per `527:5980`, tray inserted under its group
- [ ] 5.2 Per-effect tray layouts (23 variants), pixel-perfect. Schema keeps the logic, the layout follows the design. This fixes Replicate's fields.
- [ ] 5.3 1-click tray, persisted Auto-apply, Apply `Done!` state, Reset button
- [ ] 5.4 Keyboard palette and view palette visual refresh

### 6. Effect setups
- [ ] 6.1 Disassemble the 0.9.2 setups mechanism (menu 160, module forks, factory setups?) → `disassembly/Setups.md`
- [ ] 6.2 Setup dropdown (Custom / list / Save setup as… / Delete setup…), stored app-wide per module
- [ ] 6.3 Proposed "Save setup as" dialog → iterate with the author

### 7. Edit, Selection and Window commands
- [ ] 7.1 Edit: Cut/Copy/Paste/Clear in the menu, **Duplicate** ⌘D
- [ ] 7.2 Leave Silence… and Shift Sound…: disassembly check, port from `src/edit.ts`, golden vectors, popups
- [ ] 7.3 Selection: Select All, Extend to Beginning/End/Above/Below, Set/Select Loop, Crop, Insert Silence
- [ ] 7.4 Select Exactly…, Select Peak…, Select Silence: port from `src/selection.ts`, golden vectors, popups
- [ ] 7.5 Window menu: current sound, Color ▸ (24), Change Name…, sound list with slot shortcuts, Keyboard (K), Grid/Free (G), Zoom 100 % (⌘0), Zoom to Fit (⌘1)

### 8. Playback direction
- [ ] 8.1 Engine: Forward/Backward/PingPong per Window, per-track mapping (own length, own phase), selection-loop aware. Tests.
- [ ] 8.2 Glitch-free switching: re-anchor each track's phase plus a micro-fade. Advance and Keyboard preview follow the direction. TSan.
- [ ] 8.3 UI: Combo-play + Dir-menu, Transport menu items, D cycle

### 9. Audio input, routing and recording
- [ ] 9.1 Engine input path: device input in processBlock, SPSC capture ring, per-window post-fader taps (Canvas source)
- [ ] 9.2 Raw/Clean conversion to the Sound's rate and bits
- [ ] 9.3 Record into new tracks (mono/stereo), Pause/Record append, flashing Record
- [ ] 9.4 Record into a selection: 1 track sums to mono, 2 tracks split L/R, 3+ disabled; punch-in/out vs loop recording
- [ ] 9.5 Select Audio Source popup (External / Canvas / DAW), monitor and gain, Source button, ⌘⇧R and R

### 10. File management
- [ ] 10.1 `.sefx` v2 project folder plus `Assets/*.wav` (writer and reader), v1 reader kept, state-tests round trip
- [ ] 10.2 Export Sound… (self-contained .sefx) plus its dialog
- [ ] 10.3 Import Sound… (pick sounds from a .sefx), lands in the first free slots, plus its dialog
- [ ] 10.4 New Sound defaults: 1 track, 44.1 kHz, 16 bit, random theme

### 11. Release
- [ ] 11.1 SPEC `[NATIVE]` departures, re-sync `specs_v0-4-0.md`, settle the uncommitted deletion of `SoundEffects-0.9.2-SPEC.md` with the author
- [ ] 11.2 Version 0.4.0, merge into main, tag

## Verification per sub-step
- Tests: `cmake -S native/tests -B native/tests/build -G Ninja && cmake --build native/tests/build`, then **from `native/tests`** run `./build/effect-fixtures` (it reads `fixtures/` relative to the cwd and exits 2 elsewhere), `./build/engine-tests` and `./build/state-tests`. Run TSan/ASan for engine changes.
- Standalone build: `cmake --build native/build --target SoundEffect_Standalone`.
- UI: capture the running standalone at 1× and compare it with the Figma node capture. Drive-test the feature in the app.

## Open questions
- (none yet)

## How to compare with Figma, pixel by pixel
Use `use_figma` to export a node at 1x: `figma.base64Encode(await node.exportAsync({format:'PNG', constraint:{type:'SCALE', value:1}}))`. Decode it into the scratchpad, then diff it against `--gallery-snapshot` with a small PIL script that composites over white and prints one hex digit per pixel side by side. Figma rasterises text at fractional positions, so mask text out of the mean, or compare rows only.

Rules learned this way, all in `ui/Os8.cpp`:
- **Baseline:** offset ≈ 0.875 em inside a line box of round(0.98 em), then rounded to a whole pixel.
- **Frame inner shadows** are painted after the children and before the stroke.
- **Inner-shadow order:** the first-listed effect is painted first.
- **Blur:** Figma blur r ≈ `juce::DropShadow` radius r.

## Notes for later steps
- **Step 4, tinted mixer strip** (`636:11566`): the strip fill is the theme colour. The slider and VU troughs keep their grey layers (#c0c0c0, #9d9d9d, #666666, #dddddd), so check their blend mode before calling them a "darker shade". A muted strip adds a `#ffffff@0.80` "content" overlay and greys its text to #9d9d9d. A selected (Active) strip shows the 4 drag-handle bars at `#000000@0.50`.
- **The tokens live in `ui/Os8.h`, not in a separate `ui/Theme`**: the System colours, `kThemes[24]`, `themeColour()` and `themeName()`. `sys7::tintFromPalette` and `kNumTints` forward there until Sys7 is gone.

## Journal
One line per finished sub-step: date · sub-step · what was verified · commit.

- 2026-10-25 · 1.3 · `os8::paintButton/paintCheckbox/paintRadio` + `Os8Button` (Done! flash, 500 ms) and `Os8Toggle` (checkbox/radio + label); pixel-diffed against Figma 1x exports: secondary button mean 1.4/255 off text, primary 6.3 (soft ring blur), text rows identical (Figma snaps baselines to whole px; Figma paints a frame's inner shadows OVER its children); live interaction not yet drive-tested (end of step 1)
- 2026-10-25 · 1.2 · the 24 named themes replace the 12 HSL tints (index N → colour N, -1 → Placeholder-gray-100); the tint pool now spans 24; state-tests round-trip index 23; gallery swatches match the Theme board
- 2026-10-25 · 1.1 · ClassicChicago.ttf embedded (BinaryData), `ui/Os8` text styles + Figma baseline rule (0.865 em) + `drawText`/`widthOf`; JUCE advance widths ceil'd = Figma widths on 15/15 strings; `--gallery` / `--gallery-snapshot` workbench; tests green
- 2026-10-25 · setup · plan approved, spec snapshot re-synced from Figma, branch `v0.4.0-dev` created
