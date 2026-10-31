# Sound Effects 0.4.0 — Bugs and Fixes

Local snapshot of the Figma Specs block "Sound Effects 0.4.0 - Bugs and Fixes"
(file `QmG2Chvizn1QXGPwQVBgnn`, node `883:21119`), taken 2026-10-31. Figma stays
the source of truth; the plan for these items is step 12 in [PLAN.md](PLAN.md).

## General

- Buggy app-menus display, inconsistent with design system (elements are bleeding out of menus' boundaries, sub-menu arrow and the theme colors sub-menu are displaying very badly)
- Buggy and random behaviors of all text fields in effects overlay and sound windows' selection info: weird multiple text higlights, weird mixes of selected text in disabled fields that is not in the design system, cursor sitting at the left of the text for no reason, text in fields staying white while field is deselected. Check for yourself and fix (revised Figma component: Text field `327:4482`)
- Panel collapse/expand button (window-button `203:4347`, variant `hide-panel`)
- New Show/Hide Panels shortcut — written ⌘, in Figma; **the author corrected it to ⌘. (period) on 2026-10-31, and ⇥ goes away**
- Panning canvas with mouse right-click + drag

## Sound Window

- Improve playhead visibility over selected areas + visual feedback on waveforms when recording is engaged (Placeholder-waveform `196:4395`)
- Transport button's icons adjusted for precise pixel alignment + larger loop icon (Transport-button `761:17286`, Transport-controls `589:8301`)
- In grid view, display dynamic drop-targets when moving windows
- New 'Active-track-drag' and 'New-track-drag' icons to use when moving a track, trashing a track, or adding a track in the sound window (icons `208:5690`)

## Solo and Mute

- Adjusted design of mute and solo buttons (window-button `203:4347`)
- Solo: visual feedback on sound windows too (like in the Mixer)
- ⌘ + click on any pressed Solo or Mute button to unlock all active Solos or Mutes

## Mixer

- Refined mixer channel strips design for more precise alignments (Mixer-channel-strip `870:16438`)
- Display temporary indicator when using the balance slider (50L - C - 50R) - same behavior as when using channel volume
- Display dynamic drop-targets when moving channel strip
- Channel strips re-order automatically when closing a sound (no empty slots)

## Playback and Looping

- When a selection is released (de-selection), playhead(s) outside the selection should be synced / aligned with playhead(s) formerly inside. **Evaluate implications thoroughly and take into consideration all possible edge cases before jumping to code**.

## Effects

- Replace 'Reset' button by 'Cancel' button in all effect trays. Clicking Cancel closes the tray.
- Consequently, the 'Reset' function is achieved by choosing the 'Custom' item in setup menus.
- Changes in 'Waveform Generator' effect (tray `507:5908`, dropdown-menus `339:11140`)
  - Default length: 1000 ms OR current selection length
  - Default amplitude: 50 %
  - Simpler Octave menu displaying only the octave number (from 0 to 8)
  - If the user enters a custom kHz value, then the Notes and Octave menus both show the value "-"
