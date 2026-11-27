<!--
  Local snapshot of one Figma Specs block (file QmG2Chvizn1QXGPwQVBgnn):
    - "To-do v.0.6.0"   node 998:20646  (the same block that held the 0.5.0 to-do)
  The 0.5.0 "Application Menus" block (1078:6637) no longer exists in the file.
  Read with use_figma: figma.spec.getContent({ specNodeId: '998:20646' }).
  Figma is the source of truth; re-sync this file whenever the Figma spec changes.
  Last sync: 2026-11-27, re-read at 9.1 (unchanged). Decisions that resolve gaps or contradictions in this text
  live in docs/0.6.0/PLAN.md ("Decisions").
  Embedded links are kept as node ids: open them as
  https://www.figma.com/design/QmG2Chvizn1QXGPwQVBgnn/SoundEffects?node-id=<id with "-">
-->

# To-do v.0.6.0

### NEW - redesign of popups dialogs [636:21366]

- Alerts: [1340:8550]
- Effects' Setups [1447:9215]
  - Embedded frame that slides down then up
  - cover the effect's settings from the top, with a black veil at 25% opacity over the rest of the tray (see example [1458:12238])
  - In the Setup dropdown, 'Delete Setup...' is replaced by 'Manage Setup...' [543:10113] where users can:
    - Rename the setup and save
    - or delete the setup
- All the other popups are detailed here [1447:9163]
  - New design. Popus are moveable, but they continue to appear at appropriate locations like in v.0.5.0
  - Large text labels (Style 'Headlines')
  - Various UI and feature tweaks:
    - "Clean audio" option is always checked by default wherever it is available
    - The incoming audio level fader in the 'Select Audio Source' popup should display a live stereo VU meter
    - 'Export Sound' now has 3 options: '.SEFX file', '.WAV stereo mixdown' and '.WAV mon stems'. The 2 last ones are new features to be implemented [1447:9002]

### Effects

  - BUG: in 'Echo' the "Only # echoes" parameter does nothing
  - UI tweak for '1-click effects' trays: [543:9781]
  - NEW Shape selector in "Waveform Generator" [507:5908], plus new shape option "Triangle" (default) [1447:9097]

### Sounds

- NEW - Multi-selection of Sounds
  - SHIFT+click on a Sound (or its corresponding channel strip) to multi-select. User can multi-select as many sounds as they like on the canvas
  - SHIFT+click again on a multi-selected Sound (or channel strip) to remove it from the multi-selection
  - Multi-selected Sounds share the following functions:
    - Mute, Solo, Stop, Play, Pause, Direction, FF, Loop and keyboard (including incoming MIDI)
    - Activating any of these functions (or the corresponding shortcut / Sound menu item) on any of the multi-selected sounds propagate the function's state to all the other multi-selected sounds
    - When a sound is added to the multi-selection it does not automatically inherit the function's state of the others, it will only start to receive or send function states changes from this moment on.
    - Selecting audio, applying effect, seting loop, setting volume and balance, or any other user action, except the ones listed above, on any multi-selected sound is still possible and WILL NOT affect the other multi-selected sounds, nor exit the multi-selection.
    - Changing multi-selected sounds' color or name is disabled
  - Exit the multi-selection ONLY by:
    - Selecting a Sound (or corresponding channel strip) outside the multi-selected group
    - Clicking anywhere on the canvas
- NEW - Solo swap: command-click the solo button on a un-soloed channel:
  - 1. deactivate all currently active soloed channels
  - 2. solo the channel

### NEW - MIDI Mapping [1451:10974]

With the multi-selection feature we can now remote control Sounds. Hence the idea to go further and to allow to remote control Sounds via MIDI with an external sequencer (best use-case is plug-in mode)

How it works:

- MIDI mapping registers only Midi notes from C0 to B0 (to avoid interferences with the Keyboard, the Keyboard will register only MIDI notes from C1 onward -- which is the current range displayed in the keyboard palette)
- For each of the 12 notes between C0 and B0, users are able to assign a Sound by its channel number (if channels are reordered, the sound will change)
- To assign a sound to a note, users click on the corresponding note-pad [1451:10840]
There they can:
  - Assign a sound from the dropdown
  - Set what happens when note starts and when note end
  - Note that user can set the sound to STOP ou PAUSE at note start: if they select either of these options, then the action at note end is set to PLAY
  - The same sound can be assigned to several notes with different conditions at note start and end, which can result in interesting glitches musically, but also possible conflicts that must be safeguarded.
- This panel is pegged on top of the Magnetophon, itself pegged at the bottom of the right side panel (eg when the mixer is collapsed, it remains on top of the magneto)

### Mixer

- NEW - Mute button on the 'Main-output' [1083:14659] (no shortcut, mutes everything, no visual feedback on the sound windows. Just a useful little commodity)
- FIX - when collapsing the Mixer panel, the bar stays at the top

### Magnetophon

- Refined visual appearance [1307:6817]
  - Bigger reels [1307:6790] and other tweaks:
    - the animation should be limited to the rotation of the reels, no need to simulate the accumulation of the tape on the right reel, just keep the two reels' design as they are.
    - the representation of the tape, the pitch rollers and the playhead must be pixel perfect so that the whole thing looks exactly like in the Figma.
  - Center the time counter with the playhead [1307:6711]
- NEW - Delete a take [1307:6706]: small trash icon appears on hovering a take's area
  - Click to delete - no confirmation required
  - Delete the corresponding audio, no undo, no alert
- NEW: Solo button in the Magnetophon's transport bar [1307:6717]
  - Follows the Solo rules
- Send to canvas:
    - Updated design [1447:9158]
    - Individual takes are unchecked by default
- FIX - The Magnetophon panel is pegged to the bottom of the right-side panel (eg when the Mixer panel is collapsed, the magneto stays at the bottom)

### Audio

The original SoundEffects was very clicky, but I think this version can be less so.
Here are the main occurence of clicks I have observed:

- STOP and PAUSE
- Keyboard action with the mouse
- Keyboard action via MIDI is **very clicky** at the start and even more so at the end of a note
- Looped sounds are often clicky, but the "Zero crosser" effect helps making cleaner loops

Find a global solution at the audio engine level to remedy this.

### General behavior

- Don't create an empty track on a new canvas
