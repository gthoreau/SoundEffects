<!--
  Local snapshot of the Figma Specs block "SoundEffects V.0.4.0 - Specifications"
  (file QmG2Chvizn1QXGPwQVBgnn, node 752:9475). Figma is the source of truth;
  re-sync this file whenever the Figma spec changes. Last sync: 2026-10-31
  (compared line by line with Figma: content unchanged since 2026-10-25; the
  differences are this file's plain-text links and table line breaks).
-->

# SoundEffects V.0.4.0 - Specifications

Nomenclature:

- 0.9.2 refers to the original SoundEffects 0.9.2 by Alberto Ricci
- 0.3.0 refers to current version of SoundEffects
- 0.4.0 refers to the next version

## RUST

Make a cost/benefit evaluation of migrate the codebase to RUST for 0.4.0

## User Experience

Across the board redesign of the look and feel:

- Inspired from classic MacOS 8 style
- New theme colors
- Recreated Classic Chicago font
- Accurate redesign of effect settings
- Application menus recreated and improved
- Improved Sound windows and Mixer
- and much more
- New Figma design system: https://www.figma.com/design/QmG2Chvizn1QXGPwQVBgnn/SoundEffects?node-id=0-1

### Effects Panel

Figma node: https://www.figma.com/design/QmG2Chvizn1QXGPwQVBgnn/SoundEffects?node-id=636-14867&m=dev

#### **Notes:**

- The original, weird and dirty character of the Effects (and the fact that they are destructive) must be maintained. The design system provides a faithful layout for each effect's settings panel with some improvements that I deemed necessary. A pixel-perfect implementation is expected.
- 1-click effects opens an effect-settings-tray, like all the other effects in the other effect-groups (no more alert popup), except when the user has checked the "Auto-apply effect" option.
  Node: https://www.figma.com/design/QmG2Chvizn1QXGPwQVBgnn/SoundEffects?node-id=543-9781
- By default "Auto-apply effect" is on. Further user choice should be persitant at the application level.
- Don't forget that the classic "Keyboard" effect from 0.9.2 has been moved in a permanent canvas-palette since 0.3.0 -- It works perfectly as it is, no change is required, except slight visual improvement as per the design system.

#### Some effects support saving and recalling presets (called "setups"). We will:

- **Disassemble the mechanism from the original 0.9.2 code first**
- Implement the mechanism to save, recall and delete setups for effects that support the feature
- **Important**: setups are persistant at the application level
- Setup dialogs are not represented in the design system. I let the agent propose a design coherent wuith the design system's rules, and we'll iterate from that.

#### Apply buttons for effects have an intermediary state called 'Applied':

- when the user clicks 'Apply', the button turns to its 'Done' state for approx .5 seconds (to be fine-tuned) to inform the user the effect has ben applied. Then the button returns to its default state.
- it is not tied to the actual time it takes to apply the effect (which is near-instant), it is just made for visual feedback

### Sound Windows

Embedded canvas: https://www.figma.com/design/QmG2Chvizn1QXGPwQVBgnn/SoundEffects?node-id=759-6709

Figma node: https://www.figma.com/design/QmG2Chvizn1QXGPwQVBgnn/SoundEffects?node-id=636-21151&m=dev

- New look and feel, new color themes. All documented in the design system. **Implementation must be pixel perfect**
- Track audio output: User can route each individual Tracks' audio to the Left or the Right, or both, or none by toggling on/off the 2 speaker icons located at the left side of the waveform.

#### Move, delete, add and duplicate Tracks:

- To move track up-down: click and drag the Track icon (on the righ side of the track component) to the new position
- To delete a track: click and drag the Track icon to the Trash icon
- To add a new track to the sound: click and drag the 'new-track' icon, release where user wants to create an empty track (the UI display a ghost target in places where the user can release the new-track icon)
- To duplicate a track: Option-click and drag the track icon to a new position

#### Sound info section that includes:

- channel slot number (see Mixer section)
- sample rate in kHz
- Bit rate
- Total length in seconds

#### Selection info

- 3 text fields (start, length and end) in milliseconds (default). Dropdown to switch time units (s., ms. and samples)
- with 'length' and 'end' fields at 0, the value in 'start' sets the position of the insertion point
- More selection helper features in the 'Selection' entry in the 'Application Menus' section below

### Mixer Panel

Figma node: https://www.figma.com/design/QmG2Chvizn1QXGPwQVBgnn/SoundEffects?node-id=636-10485&m=dev

1. Mixer Channels features:

- Balance sliders to nudge the stereo output to the right or the left
- Stereo VU meter in the output volume slider
- Mixer channels are placed in static, numbered slots in the mixer panel (marked 1,2, 3, 4 ...): a channel can be dragged from its slot to another slot. User can put the focus on a channel (and its correponding Sound Window) by keying the number on the computer keyboard (see also the 'Windows' menu). Moving a channel from a slot to another does not affect the Sound Window location in the Canvas

### Application Menus

The Application menu is displayed in the application's top bar (instead of the OS menu) — mostly to make it available in both standalone and plug-in versions. Figma node: https://www.figma.com/design/QmG2Chvizn1QXGPwQVBgnn/SoundEffects?node-id=700-14311&m=dev

| **0.9.2** | **0.4.0** | Comment |
|---|---|---|
| File | File |   |
| Edit | Edit |   |
| Selection | Selection |   |
| Sound | Transport | Renamed |
| Effects | -- | Replaced by Effects panel |
| Special | -- | Discarded |
| Windows | Windows |   |

#### File menu

The menu takes some elements of the original 'File' menu in 0.9.2,  elements of 0.3.0 and some new elements. Figma node: https://www.figma.com/design/QmG2Chvizn1QXGPwQVBgnn/SoundEffects?node-id=569-13307&m=dev

| **0.9.2** | **0.4.0** | Shortcut | Comment | UI/UX |
|---|---|---|---|---|
| New | New Sound | ⌘N | Present in 0.3.0 | See note (1) |
| Open... | Open Sound... | ⌘O | Present in 0.3.0 |   |
| Close | Close Sound | ⌘W | Present in 0.3.0 |   |
| -- | Import Sound... |   | NEW - See description in File Management |   |
| -- | Export Sound... |   | NEW - See description in File Management |   |
| -- | New Canvas | ⌘⇧N | Present in 0.3.0 |   |
| -- | Open Canvas... | ⌘⇧O | Present in 0.3.0 |   |
| -- | Save Canvas | ⌘S | Present in 0.3.0 |   |
| -- | Save Canvas as... | ⌘⇧S | Present in 0.3.0 |   |
| -- | Revert Canvas |   | Present in 0.3.0 |   |
| Preferences | Audio/Midi Settings | ⌘, |   |   |
| Quit |   |   | In "soundEffects" menu |   |

Note: (1) New sounds are set to 1 track at 44.1kHz 16bit by default. Theme color is assigned randomly following the same rule implemented in 0.3.0

#### Edit menu

The menu is the same than the original 'Edit' menu in 0.9.2. Cut-copy-paste-clear are already available in 0.3.0 but only as keyboard shortcuts. Figma node: https://www.figma.com/design/QmG2Chvizn1QXGPwQVBgnn/SoundEffects?node-id=569-12670&m=dev

| **0.9.2** | **0.4.0** | Shortcut | Comment | UI/UX |
|---|---|---|---|---|
| Undo | Undo | ⌘Z | Always inactive (that's the joke) |   |
| Cut | Cut | ⌘X |   |   |
| Copy | Copy | ⌘C |   |   |
| Paste | Paste | ⌘V |   |   |
| -- | Duplicate | ⌘D | NEW - Copy and paste immediately after the selection's end |   |
| Leave Silence... | Leave Silence... |   | Requires disassembly of 0.9.2 code before re-introduction | popup (1) |
| Shift Sound... | Shift Sound... |   | Requires disassembly of 0.9.2 code before re-introduction | Popup (2) |
| Clear | Clear | ⌫ |   |   |

Notes:

(1) "Leave Silence..." Figma node: https://www.figma.com/design/QmG2Chvizn1QXGPwQVBgnn/SoundEffects?node-id=543-8056&m=dev

(2) "Shift Sound..." Figma node: https://www.figma.com/design/QmG2Chvizn1QXGPwQVBgnn/SoundEffects?node-id=543-9335&m=dev

#### Selection menu

The menu takes some elements of the original 'Selection' menu in 0.9.2, and manifest some invisible features of 0.3.0

Figma node: https://www.figma.com/design/QmG2Chvizn1QXGPwQVBgnn/SoundEffects?node-id=569-13068&m=dev

| **0.9.2** | **0.4.0** | Shortcut | Comment | UI/UX |
|---|---|---|---|---|
| Select All | Select All | ⌘A | Present in 0.3.0 as a keyboard shortcut only |   |
| Select none | -- |   | Discarded |   |
| Extend to Beginning | Extend to Beginning | ⌘← |   |   |
| Extend to End | Extend to End | ⌘→ |   |   |
| -- | Extend Above | ⌘↑ | New |   |
| -- | Extend Below | ⌘↓ | New |   |
| Select Exactly... | Select Exactly... |   | Requires disassembly of 0.9.2 code before re-introduction | Popup (1) |
| Select Peak... | Select Peak... |   | Requires disassembly of 0.9.2 code before re-introduction | Popup (2) |
| Select Silence | Select Silence | ⌘⇧⌫ | Requires disassembly of 0.9.2 code before re-introduction |   |
| Set Loop | Set Loop | ⌘L | Requires disassembly of 0.9.2 code before re-introduction |   |
| Select Loop | Select Loop | ⌘⇧L | Requires disassembly of 0.9.2 code before re-introduction |   |
| -- | Extend Above | ⌘↑ | New |   |
| -- | Extend Above | ⌘↑ | New |   |
| Crop | Crop | ⌥⌫ | Present in 0.3.0 as a keyboard shortcut only |   |
| -- | Insert Silence | ⇧⌫ | Present in 0.3.0 as a keyboard shortcut only |   |
| Selection info... | -- |   | Discarded |   |

**Notes**

(1) "Select Exactly..." Figma node: https://www.figma.com/design/QmG2Chvizn1QXGPwQVBgnn/SoundEffects?node-id=543-8171&m=dev

(2) "Select Peak..." Figma node: https://www.figma.com/design/QmG2Chvizn1QXGPwQVBgnn/SoundEffects?node-id=543-9266&m=dev

#### Transport menu

Borrows most elements from the "Sound" menu in 0.9.2, adds some new elements.

Figma node: https://www.figma.com/design/QmG2Chvizn1QXGPwQVBgnn/SoundEffects?node-id=569-13467&m=dev

| **0.9.2** | **0.4.0** | **Shortcut** | **Comments** | UI/UX |
|---|---|---|---|---|
| Record... | Record | R |   | Toggle Record button |
| -- | Select Audio Source... | ⌘⇧R | New | Popup (1) |
| Stop | Stop | Esc |   | Toggle corresponding Transport buttons: Disable Stop, Enable Play, Disable Pause |
| Play | Play | ␣ | Same shortcut as Pause | Toggle corresponding Transport buttons: Enable Stop, Disable Play, Enable Pause |
| Pause | Pause | ␣ | Same shortcut as Play | Toggle corresponding Transport buttons: Enable Stop, Enable Play, Disable Pause |
| Fast Forward | Fast Forward | F |   | Toggle Fast-Forward button |
| Loop | Loop | L |   | Toggle Loop button |
| Volume | -- |   | Discarded |   |
| -- | Play Forward | ⌘␣ | New | Toggle playback direction |
| -- | Play Backward | ⌃␣ | New | Toggle playback direction |
| -- | Play Ping-pong | ⌥␣ | New | Toggle playback direction |
| -- | Mute | M | New | Toggle Mute buttons in Mixer Channel and Sound Window |
| -- | Solo | S | New | Toggle Solo buttons in Mixer Channel and Sound Window |

Notes:

(1) "Select Audio Source..." Figma node: https://www.figma.com/design/QmG2Chvizn1QXGPwQVBgnn/SoundEffects?node-id=543-9503&m=dev

#### Windows menu

Borrows some elements from 0.9.2, but mostly is original to 0.4.0. Figma node: https://www.figma.com/design/QmG2Chvizn1QXGPwQVBgnn/SoundEffects?node-id=569-13665&m=dev

| **0.9.2** | **0.4.0** | Shortcut | Comment | UI/UX |
|---|---|---|---|---|
| -- | [current-sound-name] | [channel#] | NEW - Checked and Inactive |   |
| -- | Color: [color-name] |   | NEW | Open theme color picker sub-menu |
| -- | Change Name... |   | NEW | Canvas focus on the Sound Window's title bar in edit mode (feature exists in 0.3.0) |
| [sound-name] | [sound-name] | [channel#] (1) | 1 item per Sound Window in the canvas | Puts the corresponding Sound Window in focus |
| Sound Level Meter | Show/Hide Side Panels | ⇥ | NEW | Toggle visibility of side pannels |
| -- | Show/Hide Keyboard | K | NEW | Toggle visibility of the Keyboard effect canvas-palette |
| -- | Grid/Free Canvas | G | NEW | Toggle canvas modes (grid or Free) in the canvas palette |
| -- | Zoom 100% | ⌘0 | NEW | Zoom canvas at 100% |
| -- | Zoom to Fit | ⌘1 | NEW | Zoom to fit all sound-windows in the visible canvas |

Note:  (1) Sound shortcuts are available only for the first 10 mixer channels, from 1 to 0 (channel 10). No shortcuts for Sounds at channel 11 and beyond

### Text Fields

In all text fields, while selected, use arrow-up to increment, arrow-down-to decrement, SHIFT+arrow-up to increment 10X, SHIFT+arrow-down-to decrement 10x. Propose increment values that are commensurate with the context and/or units.

## Audio

### Audio Routing & Resampling

NEW - 0.4.0 enables sampling capabilities and advanced audio routing.

##### Implement internal audio routing capabilities:

- Accessible from the 'Source' button in the sound window's Transport bar, and from the 'Select Audio Source' item in the 'Transport' menu
- Allows to route Audio from 3 different sources into a sound window in order to record into a Track / Slection in a track

1. Set audio monitoring options

- by default monitoring is OFF (to avoide feedback). User can turn it ON
- user can set input gain

###### Choose Audio Sources:

- 'External': lists the external sources set-up in 'Audio/Midi settings'. User selects one (mono or stereo pair) + Apply
- 'Canvas': lists all the Sounds in the Canvas + the Main mixer output. User selects one (always stereo) + Apply
- 'DAW': available only in the plug-in version. No choice: the audio from the DAW channel is routed into the sound Window + Apply

##### Record audio in new track(s)

- When 'Source' is set up, the record button is active. If there is no current selection, then:
- Click 'Record' to start recording of incoming audio in 1 (incoming mono) or 2 (incoming stereo) new Tracks in the sound window
- Click 'Stop' to exit recording
- Click 'Pause' ('Record' button flashes), and then 'Record' again to append new audio to the previously recorded audio

##### Record Audio into a Selection

- Selection on 1 Track: incoming audio is summed to mono
- Selection over 2 tracks: incoming Left output is recorded on the upper track, incoming right output is recorded on the track below. The attribution is based purely on tracks position in the Window, without consideration to the track output settings (speaker icons)
- selection on 3+ tracks: not allowed, 'Record' button is disabled if selection goes over more than 2 tracks
- If 'Loop' is disengaged, then we get a punch-in/out effect (i.e. the recording starts at the selection entry and stops at the selection' end)
- If 'Loop' is engaged, then we have continuous, looping, recording inside the selection until the user stops or pauses te recording

##### Recording into a sound window with existing running tracks

- 'Record' is also a 'Play' action so the other tracks are played while the audio is recorded.
- Stop, Pause, Loop and Fast Forward have the same behavior.

### Audio playback direction

NEW - 0.4.0 will support Backward and PingPong playback directions, at the Sound Window level, and in supplement to the default Forward playback present in 0.3.0

- each sound can play in different directions
- "Fast Forward" and the Keyboard effect live preview follow the playback directiion at all times
- User can toggle direction easily, at any time, without glitches and pauses
- audio performance should be excellent and fluid at all time, which is key for live performance. Great care must be put into designing the audio engine.
- UX: Direction can be toggled via a direction menu attached to the Play button in the transport bar (node: https://www.figma.com/design/QmG2Chvizn1QXGPwQVBgnn/SoundEffects?node-id=636-21143&m=dev ), and via the Transport menu.

## File management

NEW - In 0.3.0 we can already save and open full canvasses in the .sefx format, in 0.4.0 we will adopt a "project" architecture for save an canvas and its content. Also we will dd "Export Sound" and "import Sound" capabilities.

### Change in the SEFX format

- New "Project" style architecture inspired from what regular DAWs like Ableton Live are doing.
- A Canvas is saved as a folder that contains:
  - The .sefx file itself that describes the saved Canvas: the Sounds, preferences, states, etc.
  - A "Assets" folder that contains all the audio files used inthe sounds (i.e. "stems") in .WAV format.

### Export Sound

- Saves the current Sound into a .sefx files.
- Exports are regular .sefx files i.e. they open in a regular Canvas
- The "Export Sound..." dialog layout is not in the design system yet: improvise and we'll iterate

### Import Sound

Workflow:

1. Works like 'Open Canvas' but instead of replacing the current canvas, the user is presented with a list of the Sounds embedded in the .sefx file.
2. User selects the Sound(s) they want to import
3. Click 'Import'
4. Imported Sounds are loaded into the current canvas
5. The "Import Sound..." dialog layout is not in the design system yet: improvise and we'll iterate
