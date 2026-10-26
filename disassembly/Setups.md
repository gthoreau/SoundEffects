# Effect setups — the named-presets mechanism (68k disassembly)

**Sources:** the app's `CODE 8` ("Effects" segment, the host side of `Glue.h`), its `MENU 160`,
`STR 140`, `DLOG/DITL 155`, `ALRT/DITL 154`, `DITL 500` and `CNTL 133`; every module's `EFCT`
code and `'Setp'` resources in `Effects Folder/` (read from the xattr resource forks with
`tools/rez.py`, disassembled with capstone); the Developer's Kit headers `Glue.h`,
`ModSetups.h` and `ModUnits.h`.

**Verdict:** a *setup* is a named snapshot of an effect's settings dialog. The host owns
everything: the module only passes `withSetups = true` to `ModDoSettingsDialog`, and the host
adds a "Setup:" popup at the top of the dialog, serialises the saveable items into a `'Setp'`
resource **in the module's own file**, and lists, recalls, saves and deletes them. The setups
are therefore global to the application, shared by every document, and survive quitting.
The factory setups are simply `'Setp'` resources the author saved into three of the shipped
modules with the same mechanism.

## Which modules have setups

`ModDoSettingsDialog` is `GlueRec` offset `0x1E`. Each module calls it once, as
`movea.l $1e(aN),aM; jsr (aM)`. The fourth argument pushed (Pascal order) is the
`withSetups` byte: `move.b #$1,-(a7)` or `clr.b -(a7)`.

| Module | `withSetups` | Factory `'Setp'` |
|---|:-:|---|
| Delay | **1** | — |
| Echo | **1** | — |
| Flanger | **1** | 9 |
| Granular Synthesis | **1** | — |
| High Pass IIR | **1** | 3 |
| Iterative Crossfade | **1** | — |
| Low Pass IIR | **1** | 3 |
| Sample Hose | **1** | — |
| Shelving | **1** | — |
| Waveform Generator | **1** | — |
| Amplify, Difference, Fade In, Fade Out, Quick Mix, Replicate, Resample Rate, Silence, Upsample Bits, Zero Crosser | 0 | — |
| Dither, Downsample Bits | (own dialog, no `ModDoSettingsDialog`) | — |
| the 1-click modules, the FIRs, Keyboard | (no settings dialog) | — |

These ten are **exactly** the Figma trays that have a "Setup:" row: Delay, Echo, the three
IIR filters, Flanger, Granular, Iterative Crossfade, Sample Hose and Waveform Generator.
Bandpass IIR, which the design dropped, had no module in 0.9.2.

Sample Hose is the only module that calls the setup glue itself (`ModGetSetup` at
`EFCT+0x2aee`, `ModSetupSwitchCustom` at `+0x2b3e`). Its custom item handlers bring "Custom"
back when one of its own controls changes.

## The host routines (CODE 8)

`Glue.h` lists them in this order, and their bodies sit together at `0x4a2a–0x4f44`. All of
them bracket their work with `CurResFile` / `UseResFile(modInfo->refNum)` (`ModParamsRec+2`)
/ `UseResFile(saved)`. So the module's file is the one they read and write.

| Glue call | Offset | What it does |
|---|---|---|
| `ModGetSetupsNumber` | `0x4a2a` | `Count1Resources('Setp')` |
| `ModGetSetup(num)` | `0x4a6e` | `Get1IndResource('Setp', num)`, `DetachResource`. 1-based, in **resource-map order**. |
| `ModGetSetupName(num)` | `0x4ae4` | `SetResLoad(false)`, `Get1IndResource`, `GetResInfo` → the resource **name** is the setup's name |
| `ModSetSetup(num, h, name)` | `0x4b70` | `num > count`: `Unique1ID('Setp')`, `AddResource(h, 'Setp', id, name)`, `WriteResource`, `DetachResource`. Otherwise it overwrites setup `num` in place (`SetHandleSize`, `BlockMove`, `ChangedResource`, `WriteResource`). The name is not touched. It always returns 0. |
| `ModDeleteSetup(num)` | `0x4c60` | `Get1IndResource`, `RmveResource`, `DisposHandle`, `DelMenuItem(popup, num)`, `SetCtlMax(count + 3)` |
| `ModSetupCreateMenu` | `0x4d2e` | Each setup's name is inserted into `MENU 160` before its "-" (`InsMenuItem` + `SetItem`, so a name starting with a meta-character is not parsed). Then `SetCtlMax(count + 3)`. |
| `ModSetupSwitchCustom(add)` | `0x4dfe` | See below. |

The random-looking factory IDs (134, 7361, 14170…) are `Unique1ID` results. That is the
sign they were saved through `ModSetSetup` and not written in ResEdit.

## The popup

`DITL 500` ("EffSettingsSetup") is spliced on top of every setups-enabled dialog: `CNTL 133`
(a popup, `procID 1008`, title **"Setup:"**, menu 160, 330 × 20) and a 2-pixel rule
(`PICT 150`) under it. `MENU 160` holds three items: `-`, `Save setup as…`, `Delete setup…`
(enable mask `…fd`: the rule is disabled). `STR 140` is **"Custom"**.

The popup has two states:

```
values match no setup            values are setup k
─────────────────────            ──────────────────
✓ Custom                           Plain Flange - Fast
  ─────            (only if n>0)  ✓ Plain Flange - Medium     ← item k
  Plain Flange - Fast              …
  …                                ─────
  ─────                            Save setup as…
  Save setup as…                   Delete setup…              ← deletes setup k
  Delete setup…    (disabled)
```

`ModSetupSwitchCustom(true)` (`0x4dfe`) inserts "Custom" at the top. When there are setups it
also inserts a rule (`STR 136` = `"(-"`) under it, disables **Delete setup…** (item `n + 5`,
or `n + 4` with no setups) and selects Custom. `ModSetupSwitchCustom(false)` removes "Custom"
and its rule and enables Delete again. Delete is therefore only enabled while a saved setup
is selected, and it always deletes **that** setup.

### Opening the dialog
`0x0c18` serialises the dialog's current values and compares them **byte for byte** with each
`'Setp'` in turn. The first one that is identical is selected. If none is, the popup shows
"Custom". A dialog that opens on values you once saved shows that setup's name.

### Choosing a setup
`0x0e18` loads setup `k` (`ModGetSetup`), writes each saveable item back into the dialog
(`0x11b6`) and removes "Custom".

### Editing a field
In `ModDoSettingsDialog`'s item loop (`0x480e–0x495c`), every edit field, numeric, decimal
and string alike, re-reads its text. **Only if the value actually changed** does it call the
switch routine (`jsr $1604` with selector 5), which brings "Custom" back. Radios, checkboxes
and the units popup take the same path. Clicking into a field and leaving it unchanged
keeps the setup selected.

### Save setup as…
`0x0cdc`:
1. It asks for a name with `DLOG 155` ("AskSettingName", 322 × 90): the static text
   **"Save the new setting as…"**, an empty 296 px edit field (`SelIText` 0–32767), and
   **Cancel** (183,60) and **Save** (254,60). Save is the default button, Cancel answers
   ⌘. and Esc (`SetDialogDefaultItem` / `SetDialogCancelItem` / `SetDialogTracksCursor`).
2. On Cancel the popup goes back to the item that was selected before. On Save it calls
   `ModSetSetup(count + 1)`, so it **always appends** a new setup. Nothing checks for an
   empty name or for a name that already exists: two setups may share a name, and a setup
   may have an empty name.
3. The name is inserted at the end of the list (plus a rule if it is the first setup),
   "Custom" goes away and the new setup becomes the selected one.

### Delete setup…
1. `0x15b4`: `ParamText(name)` + `CautionAlert(154)` ("ConfirmDeleteSet", 368 × 84, the
   caution icon): **"Are you sure you want to delete the setup “^0”?"** The buttons are
   **Cancel** (item 1, rightmost, the default) and **Delete** (item 2, to its left). Return
   cancels, a guard against destroying a setup by reflex.
2. On Delete, `ModDeleteSetup(k)` runs, then the popup switches to "Custom". The dialog's
   values stay as they were. On any error the host beeps (`SysBeep(3)`).

## The `'Setp'` record

A setup is the dialog's **saveable items**, serialised in item-list order. The `…NoSave`
types of `ModSetups.h` and buttons are skipped. A custom item is saved only if its
`saveProc` is set.

| Item type | Stored as |
|---|---|
| checkBox, radioButton, unitsPopup | `short` |
| editFieldUnitDependent | `long` — a **byte count** |
| editFieldNumerical | `long` |
| editFieldNumDecimal | 68k `extended` — 12 bytes: exponent word, pad word, 64-bit mantissa |
| editField | Pascal string |

Two consequences:

- **Durations are bytes.** A setup saved on a 22 kHz 8-bit sound and recalled on a 44.1 kHz
  16-bit one keeps its byte count, so its real duration changes by a factor of 4. The units
  code saved next to it is cosmetic: it only tells the dialog which unit to show.
- The record has no field names and no version: a module whose item list changed would
  have read old setups wrongly.

### Factory setups

**Flanger** (36 bytes; layout in `Flanger.md`). The byte counts are 8-bit samples at the
"Standard" rate 22254.5454 Hz, and the units are all `3` (milliseconds):

| Name (map order) | % | Delay | Width | Period | Env. follower | Depth |
|---|--:|--:|--:|--:|:-:|--:|
| Plain Flange - Fast | 50 | 134 (6.02 ms) | 67 (3.01) | 17804 (800.0) | off | 0 |
| Plain Flange - Medium | 50 | 245 (11.01) | 45 (2.02) | 22255 (1000.0) | off | 0 |
| Plain Flange - Slow | 50 | 245 | 45 | 50073 (2250.0) | off | 0 |
| Plain Flange - Slower | 50 | 245 | 45 | 77891 (3500.0) | off | 0 |
| Plain Flange - Slowest | 50 | 245 | 45 | 105710 (4750.0) | off | 0 |
| Soft Flange | 50 | 245 | 45 | 44510 (2000.0) | off | 0 |
| Distorto - High | 100 | 111 (4.99) | 89 (4.00) | 6676 (300.0) | on | 445 (20.0) |
| Distorto - Medium | 70 | 110 (4.94) | 88 (3.95) | 11127 (500.0) | on | 88 (3.95) |
| Distorto - Low | 70 | 111 (4.99) | 66 (2.97) | 13352 (600.0) | on | 44 (1.98) |

Feedback is off, Modulation is 1 (Sinusoidal) and Louder means is 1 (Longer) in all nine.

**High Pass IIR and Low Pass IIR** (24 bytes = two 12-byte `extended`s, `a` then `b`). The
records in the two files are byte-identical:

| Name (map order) | a | b |
|---|--:|--:|
| Medium | 0.5 | 0.5 |
| Low | 0.4 | 0.6 |
| Lower | 0.2 | 0.7 |

(The pad word of each `extended` repeats the exponent, `3ffe 3ffe …`. It is garbage that
the 68k FPU ignores.)

Shelving and the six other modules ship with no setups.

## What 0.4.0 keeps, and what it changes `[NATIVE]`

| 0.9.2 | 0.4.0 |
|---|---|
| Stored in the module's resource fork | Stored app-wide in `AppPrefs` (`Preferences.settings`), one XML list per module, shared by the standalone and the plugin |
| Durations as byte counts | Durations as **seconds**: recalled at the target's rate and size, a setup means the same time on any sound (as `applyPresets` already does for the author's starting values) |
| Factory setups in three module files | The same 12 setups, seeded the first time a module's list is read; they can be deleted like any other |
| Popup: Custom only while unmatched, Delete only on a selected setup, append-only Save, byte-exact match on open, "Custom" returns only when a value really changed | Kept, with one difference: the match is checked against the live values, so setting a value back to the setup's own brings its name back |
| Fixed item-order record | Keyed by setting name, so a setup survives a layout change |
| "Save the new setting as…" dialog, confirm alert for Delete | Redrawn in the Os8 vocabulary (step 6.3) |
