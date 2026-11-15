<!--
  Local snapshot of two Figma Specs blocks (file QmG2Chvizn1QXGPwQVBgnn):
    - "To-do v.0.5.0"                 node 998:20646
    - "Application Menus — v.0.5.0"   node 1078:6637
  Read with use_figma: figma.spec.getContent({ specNodeId: '<node>' }).
  Figma is the source of truth; re-sync this file whenever the Figma spec changes.
  Last sync: 2026-11-14 (both blocks re-read for step 9). Decisions that resolve contradictions in this text live in
  docs/0.5.0/PLAN.md ("Decisions").
-->

# To-do v.0.5.0

## Bugs and UI fixes

1. BUG - The text in the little track number indicator is not well centered in the square. See updated design here: [sound-info](https://www.figma.com/design/QmG2Chvizn1QXGPwQVBgnn/SoundEffects?node-id=526-4845)
2. BUG - Playback: audio outside the selection is played back when Loop is disabled
   - Opportunity to revive « Play from insertion point » that should be **implemented carefully to avoid any regression from the current state of the audio playback**.
3. FIX - "Set Loop" feature improvements:
   1. New "Unset Loop" item in the selection menu (see doc [node 0-1](https://www.figma.com/design/QmG2Chvizn1QXGPwQVBgnn/SoundEffects?node-id=0-1))
   2. Improved visual feedback for Set Loops (node [196-4395](https://www.figma.com/design/QmG2Chvizn1QXGPwQVBgnn/SoundEffects?node-id=196-4395))
- FIX - Audio assets management: give a unique ID# to individual assets, instead of the channel number, so that the files' name don't change when moving channel strips in the mixer
- FIX - Mixer panel:
  - New 'main-level' component - Self explanatory
  - Make the header (component 'new-open') and the footer (component 'main-level') static
  - Node: [636-10485](https://www.figma.com/design/QmG2Chvizn1QXGPwQVBgnn/SoundEffects?node-id=636-10485)
- FIX - Effects panel:
  - Make the header static
  - smart auto-scroll to put effects tray in focus when deployed
  - In all "Mike's modules", change the values and the labels in the 'Curvature' menus. Node: [499-4926](https://www.figma.com/design/QmG2Chvizn1QXGPwQVBgnn/SoundEffects?node-id=499-4926)

## Application Menu

(see details and notes: [1078-6637](https://www.figma.com/design/QmG2Chvizn1QXGPwQVBgnn/SoundEffects?node-id=1078-6637), transcribed below under "Application Menus — v.0.5.0")

## Transport buttons

See in the table below a detailed description of the UX and functionalities

Note: In the table I call 'initial state' the state of transport buttons where all the transport buttons are set to 'off', except Loop which is 'on' at the onset. This initial state appear on newly created sounds, and on newly opened sounds.
I understand it does not follow strictly the behaviors described below, but it will look cleaner/less perplexing to users.

| **Button** | **Shortcut** | **Action type** | **Operation** | **Effect** |
|---|---|---|---|---|
| Record | R or ⇧R | Push-on, Push-off | - Click or 'R' to **Arm Record**<br>- Click again or 'R' to **disarm record**<br>- '⇧R' to start **direct recording**<br>- Initial state: off | **Arm Record**:<br>- Alternate Record button state between on and off (blink)<br>- Direction menu, FF and Loop are disabled<br>- User must click Play or 'Space' to start recording<br>- IF Play was already on then record button's state is set to 'on', and recording starts right away.<br>**Disarm Record**:<br>- Button goes back to off state<br>- Direction menu, FF and Loop revert to previous states<br>- IF Play was 'on': stop recording and continue normal playback without jitter or glitch<br>**Direct Recording:**<br>- Latch Record and Play buttons to 'on' state<br>- Latch Pause button to 'off' state<br>- Set direction menu, FF and Loop states to 'disabled'<br>- Start recording |
| Stop | Esc | Push-on | - Click or 'Esc'<br>- Initial state: off | - Latch to 'on' state<br>- Stop playback and recording<br>- Reset playhead(s)<br>- Set the states of Record, Play and **Pause** to 'off' |
| Direction | D | Menu | Same as v.04.1 | Same as v.04.1 |
| Play | Space | Push-on | - Click or 'Space'<br>- Initial state: off | - Latch to 'on' state<br>- Set Pause to 'off' state<br>- Set Record to 'on' state IF Record is armed |
| Pause | Space | Push-on | - Click or 'Space'<br>- Initial state: off | - Latch to 'on' state<br>- Set Play to 'off' state<br>- Set Record to armed state (blink) if Record is armed |
| FF | F | Push, momentary | - Click and hold, or hold 'F'<br>- initial state: off | **IF Play is 'on':**<br>- Set FF to 'on' state' while holding click of 'F'<br>- Set FF back to 'off' state when releasing click or 'F'<br>**IF Pause is 'on':**<br>- While holding click or 'F': Set FF and Play to 'on' state + Set Pause to 'off' state<br>- When releasing click or 'F': Set FF and Play back to 'off' state + Set Pause to 'on' state<br>**IF Stop is 'on':**<br>- While holding click or 'F': Set FF and Play to 'on' state + Set Stop to 'off' state<br>- When releasing click or 'F': Set FF and Play back to 'off' state + Set Stop to 'on' state |
| Loop | L | Push-on, Push-off | - Same as v.04.1<br>- Initial state: off | Same as v.04.1 |

## Improvement of the 'selection-info' component

### The 'selection-start', 'selection-length' and 'selection-end' icons are now clickable:

- selection-start: click to set start-value = 0
  - IF there's no selection → set the caret at 0
- selection length: click to expand current selection to whole track(s) i.e. from 0 to length of the longest track in the selection
  - IF there's no selection → select all the tracks where there's a caret
- selection-end: click to set end-value = end of the longest audio in the selection
  - IF there's no selection → set the caret at the end of the audio

Implement with care to cover all possible edge cases.

### ISSUE: Using critical shortcuts (Space in particular) when editing text fields

After editing a value in selection-start, length or end fields, I tend to want to hit the space bar to hear the result, but allas it just replaces the value in the still-selected text field :(
Would it be possible to "shield" the text fields in the selection-info component from keystrokes like Space and letters?

## NEW - Magnetophon

Node: [1199-16274](https://www.figma.com/design/QmG2Chvizn1QXGPwQVBgnn/SoundEffects?node-id=1199-16274)

This new feature allows to record directly from the Mixer's output into a new audio file.

Features: the Magnetophon behaves like a basic reel-to-reel tape recorder

- Click Record to start recording a new 'Take' (a take is basically a time marker)
- Click Stop to end the take (thus, by hitting Record again, the magnetophon will create a new take marker
- Click "Next take" and "Previous take" to jump to the begining of the different takes
- Click Play to play the take (if the magnetophon is stopped in the middle of a take, and the user hits Record, the magnetophon will create a new take marker there and record over the previously recorderd audio)
- Click "send to canvas" to send the whole audio or a selection of takes (1 take per sound window) to the canvas (requires a pop-up to choose what to send)

IMPORTANT NOTE: my design is poor in details so far, so I ask you to implement the Magnetophon feature with both care and a bit of imagination. We'll tweak along the way.

## NEW - MIDI implementation

The goal is to control the keyboard palette with an external MIDI source.

- Support regular MIDI keyboard like a KeyStep 37
- Support continuous pitch slide via MPE from an Ableton Push3 or a LinnStrument, for instance
- In the Plug-in version, support incoming MIDI from the DAW

---

# Application Menus — v.0.5.0

Menu contents transcribed from the selected Application-menu design. Labels, order, shortcuts, separators, and displayed states are preserved. This is a reference for the current design, not a replacement for the existing version-comparison specifications.

[Source design](https://www.figma.com/design/QmG2Chvizn1QXGPwQVBgnn/SoundEffects?node-id=543-6437)

## Reading the tables

- "—" means no shortcut or indicator is displayed.
- Notes provide complementary information about behavior/feature.
- ⌘ = Command, ⇧ = Shift, ⌥ = Option, ⌫ = Delete, ␣ = Space.
- Bracketed labels are dynamic placeholders. In the Canvas menu, \[ch.#\] is a channel-number display in the right-hand column, not a literal keyboard shortcut. The five sound entries are preserved as shown.
- Submenu arrows indicate a submenu; the Color submenu contents are not included in the selected design.

## File menu

[Menu design](https://www.figma.com/design/QmG2Chvizn1QXGPwQVBgnn/SoundEffects?node-id=569-13307)

| **Item** | **Shortcut / right-hand text** | **Indicators** | **Note** |
|---|---|---|---|
| New Sound | ⌘N | — | — |
| Open Sound | ⌘O | — | — |
| Close Sound | ⌘W | — | — |
| *Separator* | — | — | — |
| Import Sound... | — | — | — |
| Export Sound... | — | — | — |
| *Separator* | — | — | — |
| New Canvas | ⌘⇧N | — | — |
| Open Canvas | ⌘⇧O | — | — |
| Save Canvas | ⌘S | — | — |
| Save Canvas as... | ⌘⇧S | — | — |
| Revert Canvas | — | — | — |
| *Separator* | — | — | — |
| Audio/Midi Setup | ⌘, | — | — |

## Edit menu

[Menu design](https://www.figma.com/design/QmG2Chvizn1QXGPwQVBgnn/SoundEffects?node-id=569-12670)

| **Item** | **Shortcut / right-hand text** | **Indicators** | **Note** |
|---|---|---|---|
| Undo | ⌘Z | — | Disabled |
| *Separator* | — | — | — |
| Cut | ⌘X | — | — |
| Copy | ⌘C | — | — |
| Paste | ⌘V | — | — |
| Duplicate | ⌘D | — | — |
| *Separator* | — | — | — |
| Leave Silence... | — | — | — |
| Shift Sound... | — | — | — |
| *Separator* | — | — | — |
| Clear | ⌫ | — | — |

## Selection menu

[Menu design](https://www.figma.com/design/QmG2Chvizn1QXGPwQVBgnn/SoundEffects?node-id=569-13068)

| **Item** | **Shortcut / right-hand text** | **Indicators** | **Note** |
|---|---|---|---|
| Select All | ⌘A | — | — |
| *Separator* | — | — | — |
| Extend to Beginning | ⌘→ | — | — |
| Extend to End | ⌘← | — | — |
| Extend Above | ⌘↑ | — | — |
| Extend Below | ⌘↓ | — | — |
| *Separator* | — | — | — |
| Insert Silence | ⇧⌫ | — | — |
| Crop | ⌥⌫ | — | — |
| *Separator* | — | — | — |
| Set Loop | ⌘L | — | See notes (1) |
| Unset Loop | ⌘⇧L | — | NEW feature - see notes (1) |
| Select Set Loop | ⌘⇧A | — | New shortcut |
| *Separator* | — | — | — |
| Select Exactly... | — | — | — |
| Select Peak... | — | — | — |
| Select Silence | ⌘⌥A | — | — |

Notes (1): Set / Unset Loop

- Set Loop works as it does currently, except there is now visual feedback that shows audio outside the set loop as muted (see [196-4395](https://www.figma.com/design/QmG2Chvizn1QXGPwQVBgnn/SoundEffects?node-id=196-4395) for visual reference)
- At any time user can select another portion of the audio et set the loop (⌘L) there, as it's the case.
- Unset Loop (⌘⇧L) allows to leave the set loop mode easily (currently it is achieved by clicking anywhere in the sound and choosing Ser Loop (⌘L)

## Sound menu

[Menu design](https://www.figma.com/design/QmG2Chvizn1QXGPwQVBgnn/SoundEffects?node-id=569-13467)

| **Item** | **Shortcut / right-hand text** | **Indicators** | **Note** |
|---|---|---|---|
| Color: \[color-name\] | ⌘T | Icon; Submenu arrow | New location - Disabled when Color Theme is hidden (see Canvas menu) |
| Rename... | ⌘R | — | New location - new shortcut |
| *Separator* | — | — | — |
| Arm Record | R | — | Toggle Arm/Disarm Record - See Notes (1) below |
| Direct Recording | ⇧R | — | See Notes (1) below |
| Select Audio Source... | ⌘⇧R | — | — |
| *Separator* | — | — | — |
| Stop | Esc | Icon | — |
| Play | ␣ | Icon | — |
| Pause | ␣ | Icon | — |
| Flip Direction | D | — | New menu item (existing feature) |
| Fast Forward | F | Icon | — |
| Loop | L | Icon | — |
| *Separator* | — | — | — |
| Mute | M | — | — |
| Solo | S | — | — |
| *Separator* | — | — | — |
| Zoom Sound | — | Submenu arrow | NEW |

Notes (1): To-do v.0.5.0 (node 998-20646, transcribed above)

## Zoom Sound submenu

Parent: Sound → Zoom Sound.

[Menu design](https://www.figma.com/design/QmG2Chvizn1QXGPwQVBgnn/SoundEffects?node-id=997-13518)

| **Item** | **Shortcut / right-hand text** | **Indicators** | **Displayed state** |
|---|---|---|---|
| Zoom In | ⌘+ | Icon | NEW menu item |
| Zoom Out | ⌘- | Icon | NEW menu item |
| Fit Window | ⌘= | Icon | NEW menu item |
| Fit Selection | ⌘⇧= | — | NEW feature |

## Canvas menu

[Menu design](https://www.figma.com/design/QmG2Chvizn1QXGPwQVBgnn/SoundEffects?node-id=569-13665)

| **Item** | **Shortcut / right-hand text** | **Indicators** | **Note** |
|---|---|---|---|
| \[Show\]/\[Hide\] Color Theme | — | — | NEW feature. See note (1) |
| *Separator* | — | — | — |
| \[sound-name\] | \[ch.#\] | — | — |
| \[selected-sound\] | \[ch.#\] | Checkmark | — |
| *Separator* | — | — | — |
| \[Free\]/\[Grid\] Canvas | G | — | — |
| \[Show\]/\[Hide\] Keyboard | K | — | — |
| \[Show\]/\[Hide\] Side Panels | ⌘. | — | — |
| *Separator* | — | — | — |
| Zoom Canvas | — | Submenu arrow | NEW |

Note (1): "\[Show\]/\[Hide\] Color Theme" allows to toggle between the current colorful theme and a new sober MacOS8-styled theme. The theme color preferences are kept when toggling back and forth. When Color Theme is hidden:

- all themed elements in the UI switch to 'macos8-grey'
- canvas background color is set to 'macos8-lavender'

When Color Theme is visible:

- all themed elements revert to their saved theme color
- NEW - canvas background color is set to the selected sound's theme color. If no sound is selected, the canvas background color is set to white

## Zoom Canvas submenu

Parent: Canvas → Zoom Canvas.

[Menu design](https://www.figma.com/design/QmG2Chvizn1QXGPwQVBgnn/SoundEffects?node-id=998-13725)

| **Item** | **Shortcut / right-hand text** | **Indicators** | **Note** |
|---|---|---|---|
| Zoom In | + | — | NEW menu item (existing feature) |
| Zoom Out | - | — | NEW menu item (existing feature) |
| Zoom 100% | ⌘1 | — | — |
| Zoom to Fit | ⌘0 | — | — |
