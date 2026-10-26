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
- [x] 1.4 Text field (3 sizes × 4 states) with ↑/↓ ±step, ⇧ ×10 and unit-aware steps. Dropdown and mini-dropdown.
- [x] 1.5 Sliders (Mixer-volume with VU, Mixer-pan, Amplify), Icon-button, Canvas-toggle
- [x] 1.6 Icon set as vector drawings, plus Transport-button, Combo-play-button and Dir-menu
- [x] 1.7 `AppPrefs` over ApplicationProperties (auto-apply, setups, time unit, panel visibility/collapse)

### 2. App shell and menus
- [x] 2.1 Standalone window: hidden native title bar, traffic lights in the app bar (ObjC++ NSWindow tweak), `[canvas-name]` label
- [x] 2.2 `AppMenuBar` component, with menu look per design (item states, checks, submenu arrow, shortcut column)
- [x] 2.3 Single `ApplicationCommandManager` and shortcut table. Migrate the existing keyPressed handlers (PluginEditor, WindowComponent, CanvasComponent) and wire File, plus the Edit/Selection items that already exist.
- [x] 2.4 Side panels: 270 px, window-bar title, collapse/expand button, Show/Hide Side Panels (⇥)

### 3. Sound Window
- [x] 3.1 Title bar: close, stripes, name, M/S buttons, resize; Default/Hover/Active × Mute states (mute greys the waveform)
- [x] 3.2 Sound-info header (slot number, kHz, bits, tracks, length s) and selection-info fields (start/length/end + s/ms/samples dropdown; start with length=end=0 sets the caret)
- [x] 3.3 Track: L/R speaker toggles (4 combos), track-icon drag to move, drop on trash to delete, ⌥-drag to duplicate, new-track drag with ghost target
- [x] 3.4 Bottom bar (zoom −/+/reset, arrows, blue scrollbar) and Transport bar (Source, Record, Stop, Combo-play, Pause, FF, Loop) with theme fill
- [x] 3.5 Pixel-perfect pass against `135:4722` and `589:*`

### 4. Mixer
- [x] 4.1 Engine: atomic per-window balance plus a stereo VU feed, with tests in `run_engine.cpp`
- [x] 4.2 Strip per design: tinted, balance, fader+VU, M/S, states. Numbered static slots, drag between slots, persisted slot in .sefx.
- [x] 4.3 Keys 1–0 focus a slot and its Window. Pixel-perfect pass.

### 5. Effects panel
- [x] 5.1 Panel frame: Sound-info header, 6 groups per `527:5980`, tray inserted under its group
- [x] 5.2 Per-effect tray layouts (23 variants), pixel-perfect. Schema keeps the logic, the layout follows the design. This fixes Replicate's fields.
- [x] 5.3 1-click tray, persisted Auto-apply, Apply `Done!` state, Reset button
- [x] 5.4 Keyboard palette and view palette visual refresh

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
- Standalone build: `cmake --build native/build --target SoundEffects_Standalone`.
- UI: capture the running standalone at 1× and compare it with the Figma node capture. Drive-test the feature in the app.

## Open questions
- **"Set Loop" (spec) or "Loop Selection" (Figma menu, 569:13068)?** The menus use the spec's wording for now. Same question for "Audio/Midi Settings" (spec) and "Audio/Midi Setup" (Figma): the menu uses Figma's.
- **The top bar's black outline and 18 px top corners** (700:14311) are not drawn: in the standalone the real window supplies its own border and corners, and in a plugin the host window does. Say so if the outline is wanted anyway.

## How to compare with Figma, pixel by pixel
Use `use_figma` to export a node at 1x: `figma.base64Encode(await node.exportAsync({format:'PNG', constraint:{type:'SCALE', value:1}}))`. Decode it into the scratchpad, then diff it against `--gallery-snapshot` with a small PIL script that composites over white and prints one hex digit per pixel side by side. Figma rasterises text at fractional positions, so mask text out of the mean, or compare rows only.

Rules learned this way, all in `ui/Os8.cpp`:
- **Baseline:** offset ≈ 0.875 em inside a line box of round(0.98 em), then rounded to a whole pixel.
- **Frame inner shadows** are painted after the children and before the stroke.
- **Inner-shadow order:** the first-listed effect is painted first.
- **Blur:** Figma blur r ≈ `juce::DropShadow` radius r.
- **An OUTSIDE stroke grows the shape that inner shadows use**: the transport faces' bevels cover their grey outline.
- **Read `effect.spread` too:** a (0,0) shadow with spread is a ring, and an inner shadow with spread is a band (the field halo and the Highlighted white margin).

## Notes for later steps
- **Step 3 left these to their own steps:** Record is shown greyed until recording exists (9). Source opens the audio settings until the Select Audio Source popup exists (9). The direction menu on Play changes the icon, but playback stays forward until step 8 wires `Os8ComboPlay::onDirectionChange` into the engine.
- **Window sizes:** the default is 533 × (125 + 64 per track) with a minimum width of 330; the chrome is 125 px including the 5 px ring. Old canvases re-fit their lanes from their stored height.
- **Step 7.5:** the sound list is done (4.3): slot order, rows for slots 1–10 are the `cmd::focusSlot1..focusSlot10` commands (keys 1..9, 0; ticked = selected), sounds past slot 10 are plain rows. Zoom to Fit is in the table and greyed (the canvas has no fit yet). Window ▸ Change Name, Color, the sound list, Hide/Show Keyboard, Grid/Free and Zoom 100 % already work (done early in step 2).
- **Steps 7–9:** each new feature only needs `getCommandInfo` (active/ticked) and `perform` in `PluginEditor.cpp`. The command, its menu row and its key are already in the table.
- **Driving the app:** NEVER put a filler `element_index` click in an app_batch. On 2026-10-25 one landed on the window's close box and quit the app (state saved, nothing lost).
- **Where the step-1 vocabulary lives:** `ui/Os8.{h,cpp}` holds the tokens and painters, `ui/Os8Widgets.{h,cpp}` the JUCE components (Os8Button, Os8Toggle, Os8TextField, Os8Dropdown, Os8Slider, Os8IconButton, Os8CanvasToggle, Os8TransportButton, Os8ComboPlay, Os8LookAndFeel), `ui/Os8Icons` all 33 icons, and `ui/AppPrefs` the preferences. `--gallery` shows every piece live. Nothing in the app's panels uses them yet: steps 2–5 swap Sys7 out panel by panel, then Sys7 is deleted.
- **Os8ComboPlay's direction menu** is a modal child of the top-level component. Picking works by click, not by press-drag-release (a drag stays with the combo). That is fine for the spec, but revisit it if press-and-drag is wanted.
- **Step 4, tinted mixer strip** (`636:11566`): the strip fill is the theme colour. **Solved in 1.5:** the slider trough is a #c0c0c0 MULTIPLY layer, and that is the "darker shade". Pass the strip's theme colour to `Os8Slider::setUnder()`. A muted strip adds a `#ffffff@0.80` "content" overlay and greys its text to #9d9d9d. A selected (Active) strip shows the 4 drag-handle bars at `#000000@0.50`.
- **Mixer (4.2):** slots live in the engine (`Window::slot`, `Engine::lowestFreeSlot/windowInSlot/highestSlot/moveToSlot/normaliseSlots`); `applyEngineState` normalises them, so a v1 canvas comes up seated in creation order. The Active strip is 2 px taller, as in Figma, so the rows below it move down when the selection changes. A strip silenced by somebody else's solo wears the muted look with its M box unlit (Figma has no such state). `--mixer-snapshot <dir>` renders every strip state, the five tinted example strips and the panel for the pixel diff.
- **The state blob is already v2** (4.1): `kEngineStateVersion` = 2 appends each Window's `balance` and mixer `slot`; v1 blobs parse with balance 0 and no slot. Step 10's project folder must therefore write **v3**, not "v2".
- **The tokens live in `ui/Os8.h`, not in a separate `ui/Theme`**: the System colours, `kThemes[24]`, `themeColour()` and `themeName()`. `sys7::tintFromPalette` and `kNumTints` forward there until Sys7 is gone.

## Journal
One line per finished sub-step: date · sub-step · what was verified · commit.

- 2026-10-25 · 5.x (author's Figma revisions) · **Granular** re-laid out to the corrected 409:1813 (Grain Length, its Curvature, Variance, Fade Level, then Density and its own Curvature — so `densityCurvature` has a field again). **Curvature popup** (499:4926): Custom (greyed, shown when the value is none of the three) / rule / Expo - 0 % / Linear - 100 % / Log - 200 %, on Granular and Sample Hose (`Os8Dropdown::setDisabledItem/setSeparatorAfter`). **Bandpass IIR removed** (button and layout; the IIR group is Hi-Pass, Low-Pass, Shelving). **Header** takes the selected Window's theme colour. **Prescribed defaults**: every field, popup, box and radio of the Effect-settings variants is the tray's opening value (`EffectLayouts::applyPresets`, on top of — not replacing — the modules' own `defaults()`, so the golden vectors are untouched; durations are written in ms and converted at the target's rate, and a value within one sample of a round number reads round: "5000", not 4999.97). They win over the old scope prefill (Fade-In opens at 1000 ms, not the selection's length); Draw Line, which has no design, keeps the prefill. Reset restores them. **Active state**: the group button whose tray is open shows Active (`Os8Button::setLatched`). Note Sample Hose's prescribed Pan is 100/100 (hard right; the module's own default is 50) — followed as written, probably a copy-paste slip. **Live**: Granular opens with the prescribed values, its Active button, the curvature popup as designed, header in the Window's colour. Tests green; diffs now 1.2–7.4/255.
- 2026-10-25 · 5.x (author's answers) · Fade In / Fade Out keep pre-filling the selection's length (only their units popup is preset); Sample Hose's Pan opens at 50 (centre), not Figma's 100.
- 2026-10-25 · 5.x (Sample Hose Pan) · The author is right, and Figma's 100 was too: the design shows Pan as a percentage of the sample's own position (0 % left, 100 % unchanged, 200 % right), and the stored 0–100 scale with 50 at the centre (factory default `$44/$48` = 50, kernel clamp 0..100) is exactly half of that. The panel now shows Pan's value and variance fields at twice the stored number (flag `twice`, clamped 0..200), so the default reads 100 % = no change and the module, fixtures and saved settings are untouched.
- 2026-10-25 · 5.4 · Both palettes wear the Canvas-palette frame (`os8::paintPaletteFrame`: #eeeeee, 8 px corners, d0d0d0 / fffdfd bevels, black inside stroke, grey shadow 2 px below). **ViewPalette** is the design's fixed 343 × 38 (so no more narrowing): `Os8` canvas toggle, "Zoom" label, drawn − / +, the pan slider (bar from the 100 % tick to the cap, detent kept), a 100 % button greyed while at 100 %, the keyboard icon button pressed while its palette is up. **KeyboardPalette** is the 575 × 60 frame: the same keys and base-note dot (now 4 px), the 39 × 39 note box (the target Window's theme colour, 14 pt note over 9 pt offset, bevelled), and a primary Apply (53 × 25). Behaviour untouched. **Pixel diff** vs Figma 1x (incl. shadow): keyboard 3.5/255, view 7.9 (thumb style and the greyed 100 % differ slightly). **Live:** keyboard toggled from the view palette, key click → "A3 / -3st", Apply enabled, + zoom step lit the 100 % button. Tests green. **STEP 5 COMPLETE** — open for the author: Granular's density curvature (no field in the design), Bandpass IIR (no module), the "Setup:" popup (step 6), the scope line in the header's empty right half (not in the design).
- 2026-10-25 · 5.3 · Parameterless effects: Auto-apply on (default, `AppPrefs`, the group's Auto Apply switch and every tray's "Activate Auto-Apply" follow one another through the pref listener) → the click applies and the button itself shows `Done!` for `Os8Button::kAppliedMs`; off → the generic tray (543:9781): "Apply [name].", Activate Auto-Apply, Cancel, Apply. Weird Tempo's STR# warning is wrapped into that tray, so it is read before the click. Apply (tray and keyboard Return) flashes `Done!`; Reset restores the module's defaults (and the scope-prefilled spans); an inapplicable effect's button is `setGreyed` (still clickable, and says why in the header). The header's right half shows the scope ("Whole sound · 2 tracks"), then the outcome. **Live** (standalone, `Canvas-test`): Amplify tray opens under Misc, typed 50 + Apply → "Amplify applied", button `Done!`, slider followed; Auto Apply off → Invert opens its tray, Apply → "Invert applied" + `Done!`; ticking Activate Auto-Apply ticked the group switch; Reverse then applied straight from its button (`Done!` on the button). Settings backed up and restored. Tests green. (The click handlers were missing in the first build — a click did nothing; found by the drive-test.)
- 2026-10-25 · 5.2 · The 23 trays (+ Draw Line, which the design omits: laid out like the fades) built from `EffectLayouts` — every cell's x/y/width transcribed from the Figma variants — over the schema's keys: text fields (duration fields convert bytes↔unit through the row's own units key, ↑/↓ steps, ⇧ ×10), unit and choice dropdowns, checkboxes, radios, Amplify's slider (0–200 %, tick at 100), the title and Setup rules, captions dimmed with their gate, and a hand-drawn `±` (the face's glyph is not the design's). **Replicate fixed**: its four gap radios and fields are now laid out as designed and it always extends (the design has no "longer" box for it). **Fixes the design forced**: Resample Rate's field now shows Hz (it stores 16.16 Fixed and used to show 2890137600) with a presets popup; Waveform Generator's frequency is a kHz field plus note/octave popups (A=440 equal temperament); Flanger's three durations and Depth use their own units keys (the old panel read a key that does not exist for Flanger and always showed ms); Sample Hose's Rate/Gain/Pan/Bite/Period boxes are real: `<group>.on`, absent = on, off = that group rests at its factory value (`readParam`; fixtures unaffected). **Pixel diff** (variant screenshots vs our trays, buttons row excluded): Difference 2.0, Quick Mix 1.7, Zero Crosser 2.0, Dither 3.3, Replicate 4.6, Echo 5.0, Upsample 5.0, Waveform 5.8, Flanger 6.9, Iterative Crossfade 7.8, Sample Hose 7.2, Fade-In 6.7, Resample Rate 4.4; panel trays: Amplify 4.8, Delay 4.6, Downsample 5.4, Granular 5.3 — field/button/rule rows coincide, the rest is text rasterisation. Open: Granular's `densityCurvature` has no place in the design (kept at its default), the "Setup:" popup only lists Custom until step 6, Bandpass IIR is greyed (no module). Tests green (171/171).
- 2026-10-25 · 5.1 · `EffectsPanel` rebuilt on Os8 (the 0.3 list/chips/ParamRow are gone): Sound-info header (636:15319; the empty right half of its name row carries the scope readout, then the last result), six group frames from the design's own table (labels, module and exact Figma button widths; greedy 252 px rows, Auto Apply switch in the 1-Click group bound to `AppPrefs`, FIR Filters sub-heading), the tray frame (103:8688) inserted under its group with its title rule and Reset/Apply row, wheel scrolling only (the frames are 270 wide in a 268 column). `SidePanel::setTitleRule` is not needed (Figma's Effects title bar has no rule). Keyboard row dropped (palette/Window menu only). `--effects-snapshot <dir>` renders header, groups and every tray. **Pixel diff** vs `node.screenshot` 1x: all geometry (frames, bevels, rules, button boxes, tray shadow) lands on the same rows/columns; the remaining mean 6–10/255 is text, which Figma rasterises half a pixel low (blurred rows) where we snap the baseline — the established limitation. Bandpass IIR has no module in 0.9.2 (design adds it): drawn greyed, says so when clicked. Layout data for the 23 trays is in `EffectLayouts` (controls come in 5.2). Tests green.
- 2026-10-25 · 4.3 · `cmd::focusSlot1..focusSlot10` in the one shortcut table (single keys 1..9, 0, standing down while a text field has focus); perform focuses the slot's Window, which makes its strip Active; an empty slot's key is inactive. The Window menu's sound list now runs in slot order with those commands, so the digit sits in the shortcut column as in Figma 569:13665 (`[sound-name] … [ch.#]`); the menu sizer measures a command row that shows its own text. `--app-snapshot` Window menu checked. **Live:** 3 → slot 3's Window selected and its strip Active, 1 → slot 1, 9 (empty) → nothing; canvas restored from backup. Pixel-perfect pass done in 4.2. Tests green. **STEP 4 COMPLETE.**

- 2026-10-25 · 4.2 · `MixerComponent` rebuilt on Mixer-channel 509:9257 / panel 636:10485 / tinted example 636:11566: numbered handle (drag bars when Active), name (ellipsised to 203 px; the dB readout while the fader is held), M/S title-bar boxes (right-aligned: on the Active strip they move down, not across), `Os8Slider` pan (−1..+1 balance, double-click centres) and volume (same 80 %-unity taper, stereo VU from meterLeft/Right ending at the thumb centre for that level, double-click → 0 dB), theme fill with MULTIPLY troughs via `setUnder`, muted = white@80 % veil, #eeeeee troughs, #9d9d9d outlines/VU/text (`paintSlider` muted now covers the trough too), Hover black stroke, Active 46 px with a 2/1/2/1 black frame. New/Open = two primary 122×25 buttons on a #dddddd bar whose outside stroke also rules the title bar's last row (`SidePanel::setTitleRule`). Slots: gaps shown as empty numbered rows, drag by handle/name with a spare row at the end and a drop outline, swap on an occupied slot; the canvas's slot boxes now show the real slot. **Pixel diff** (`--mixer-snapshot` vs `node.screenshot` 1x): Default/Hover 1.32/255, muted 1.29, Active 3.4 (all of it the fader thumb: Figma places it at a fixed x, ours follows the value over a 1 px shorter travel), tinted examples 1.0–3.3 (example 2's rows 0/43 are a Figma override slip: its stroke is #dddddd, the others wear their theme); panel top 0.00 outside the buttons (Figma has them at y 9.5) and the step-2 title. Two fixes the diff found: the bar's listed 2 px bevel shows 1 px (OUTSIDE stroke rule), and Figma knocks drop shadows out under the half-transparent drag bars. **Live:** a v1 canvas came up seated 1–5 with the selected Window's strip Active; dragged slot 1 → 4 (swap, the Windows' slot boxes followed, canvas unmoved), slot 2 → spare row 6 (empty row "2" left behind), M (veil + Window muted), balance drag + double-click centre; quit/relaunch kept the slots; original canvas restored from backup. Tests green.

- 2026-10-25 · 4.1 · `Window::balance` (atomic, −1..+1) applied in `Engine::render` through `Engine::balanceGains` (linear, unity at centre, the opposite side falls to 0 at the stop); `Window::meter` split into `meterLeft`/`meterRight`, each that Window's post-gain post-balance peak with the same hold/decay; `Window::slot` (message-thread int, `kNoSlot` = −1) for 4.2. State blob v2 appends balance + slot, v1 still parses (centred, unseated). New engine checks: balance law + clamp, per-side sums, per-side VU and its decay; state checks: v2 round trip, v1 blob. engine/state green under TSan and ASan, fixtures 171/171, standalone builds.

- 2026-10-25 · step 3 (3.1–3.5) · `WindowComponent` rewritten on Figma's Sound-window geometry. Title bar with close/M/S boxes and stripes only when selected; border 2/3/5 px (rest/hover/selected) growing OUTWARD into a 5 px ring the component reserves, so neighbours never overlap. Status row: slot box + rate/bits/tracks/length (dropped right-to-left when narrow) + start/length/end `Os8TextField`s with icons and a units mini-dropdown (s/ms/samples, an app preference shared by every Window; editing start moves the selection keeping its length, length moves the end, end swaps if before start; ↑/↓ steps). Tracks: L/R speaker toggles (4 combinations), white bed in a 20% frame, selection as a DIFFERENCE inversion; muted = 80% white veil + #c0c0c0 waveform + #666 selection. Drag a track handle to move it, onto the trash to delete, ⌥ to duplicate; drag the new-track handle to insert, with a ghost rule and outline. Zoom bar (out, in, whole, arrows, overview with ticks, loop block and blue view bar) and transport (Source + groove + Os8 transport buttons + Os8ComboPlay), grow box. Pixel-diffed against Figma's Active variant parts: zoom bar 2.65/255 mean, title bar 13 (Figma puts its boxes at y 5.5 and blurs them; ours sit on whole pixels; the bevels are 1 px as measured). Live-tested: speaker toggle, move, add, trash, start field, unit switch (all Windows follow), M (mute visuals), grow box. **Bug found and fixed:** `addMouseListener(this, true)` delivered every click on the Window twice (M and speakers toggled back), so the children now carry their own small listener. `--app-snapshot` now renders a real 2-track Window. Tests green.
- 2026-10-25 · step 2 (2.1–2.4) · **2.1** `ui/MacWindowChrome.mm`: native title bar hidden (full-size content), traffic lights re-placed at the design's x 18/41/64, y 16.5 (KVO + NSWindowDidUpdate, because AppKit re-lays them via constraints), bar drags the window and double-click zooms per the system setting; macOS menu bar keeps only the app menu. **2.2** `ui/AppMenuBar`: `AppTopBar` (canvas name item + JUCE MenuBarComponent) and `Os8AppMenuLookAndFeel` (diagonal bevel, 18 px rows, border 3, icons, shortcut column in #666); File menu frame = Figma's 175 × 214 exactly. **2.3** `ui/AppCommands`: ONE table of all 50 spec commands (name, menu, keys, design-ordered shortcut text ⌘⇧⌥⌃); the editor owns the ApplicationCommandManager and the menus (standalone + plugin), and the standalone MainWindow is the next target for canvas/device commands; WindowComponent's own key handling is replaced by `perform(Action)`; single-key shortcuts stand down while a TextEditor has focus; features not built yet are listed but greyed. **2.4** `ui/SidePanel`: 270 px Mixer/Effects frames with a striped title bar and a provisional collapse (window-shade) box, which floats the shaded bar over the canvas and is persisted; ⇥ hides/shows both. Live-tested: menus open over the app, states follow the selected Window, Play from the menu, Space play/pause, ⇥, collapse box, Window ▸ Color list, canvas survives quit/relaunch. `--app-snapshot <png>` renders the editor + every menu off-screen. Tests green.
- 2026-10-25 · 1.7 + step 1 drive-test · `ui/AppPrefs` (own file `~/Library/Application Support/SoundEffects/Preferences.settings`, shared by standalone + plugin, Listener API, XML slots for setups); live in the app: Done! flash 0.5 s then back, radios exclusive, ↑↑⇧↑ took 75 → 87 with the field Highlighted + halo, units popup opens/hovers/selects, direction menu opens over Play / hovers / picking ◀ changes Play's icon, Auto-apply untick survived quit + relaunch (restored to on afterwards); existing Windows already wear the 24 themes; all tests green. **STEP 1 COMPLETE.**
- 2026-10-25 · 1.6 · `paintTransportButton` (normal/active/disabled), `paintPlayDirectionButton`, `paintDirectionMenu`, shared `paintMenuFrame`; widgets `Os8TransportButton` and `Os8ComboPlay` (Play + direction strip + modal direction menu, async close); disabled transport stop = 0.00 vs Figma, normal 3.5, active 5.0 (icons at Figma's half pixels); **design slip resolved:** Figma's disabled ping-pong icon stays red, ours is #9d9d9d like every other disabled icon
- 2026-10-25 · 1.5 · `os8::paintSlider` (volume + stereo VU + 0 dB tick, pan + deviation bar, amplify + 100 % tick), thumb, `Os8Slider` (juce::Slider), `Os8IconButton`, `Os8CanvasToggle`, `os8::multiply()` for Figma MULTIPLY layers; `Os8Icons` now holds ALL 33 Figma icons as verbatim SVG (Drawable, multi-colour) + `outline()` for recolouring; statics moved to DeletedAtShutdown singletons (no leak asserts); diffs: volume 0.17, pan 0.38, icon button 0.58, canvas toggle 12.6 (Figma's icons sit at y 3.5 → blurred; ours snap)
- 2026-10-25 · 1.4 · `Os8TextField` (3 sizes × 4 states, select-all on focus = Highlighted, ↑/↓ ±step, ⇧ ×10, Return/blur commit, Esc revert, `stepFor/decimalsFor(Unit)`), `Os8Dropdown` standard + mini, `Os8LookAndFeel` popup menus, `Os8Icons` (pixel SVG paths); diffs vs Figma: large field 1.65, small active 3.3 (text AA only), dropdown text ½ px (Figma sits it between rows); editor baseline = painted baseline (measured); open menu not yet seen live
- 2026-10-25 · 1.3 · `os8::paintButton/paintCheckbox/paintRadio` + `Os8Button` (Done! flash, 500 ms) and `Os8Toggle` (checkbox/radio + label); pixel-diffed against Figma 1x exports: secondary button mean 1.4/255 off text, primary 6.3 (soft ring blur), text rows identical (Figma snaps baselines to whole px; Figma paints a frame's inner shadows OVER its children); live interaction not yet drive-tested (end of step 1)
- 2026-10-25 · 1.2 · the 24 named themes replace the 12 HSL tints (index N → colour N, -1 → Placeholder-gray-100); the tint pool now spans 24; state-tests round-trip index 23; gallery swatches match the Theme board
- 2026-10-25 · 1.1 · ClassicChicago.ttf embedded (BinaryData), `ui/Os8` text styles + Figma baseline rule (0.865 em) + `drawText`/`widthOf`; JUCE advance widths ceil'd = Figma widths on 15/15 strings; `--gallery` / `--gallery-snapshot` workbench; tests green
- 2026-10-25 · setup · plan approved, spec snapshot re-synced from Figma, branch `v0.4.0-dev` created
