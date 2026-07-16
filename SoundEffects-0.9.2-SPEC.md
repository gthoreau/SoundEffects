# SoundEffects 0.9.2 — Reconstruction Specification

**Target:** faithful re-creation of Alberto Ricci's *SoundEffects 0.9.2* (©1993–95, Torino, Italy; shareware $15) — first as a web prototype, ultimately as a Eurorack destructive sampler module.

**Sources:** this spec is reverse-engineered exclusively from the original artifacts in `soundeffects-092.hqx`:

- The application's resource fork (397 KB): all 22 menus, 45 dialog item lists, 25 alerts, 11 window templates, 23 string lists, 38 control templates — the complete declarative UI.
- The **SoundEffects Developer's Kit**: the author's own C headers and sample code documenting the engine's data model and plug-in ABI (`Glue.h`, `ModSetups.h`, `ModUnits.h`, `ModGetSetSamples.c/.h`, `Gain.c`, the manual *Writing SoundEffects Effects*).
- The resource forks of all 38 effect modules: settings dialogs, parameter defaults, built-in help texts (including filter equations), error strings, submenu groupings.
- The user docs (*Read Me*, *Intro*, *Effect Combinations*, third-party effect read-mes).

**Fidelity tags** used throughout:
- `[SOURCE]` — exact behavior from shipped C source.
- `[DOC]` — equation/algorithm stated verbatim by the author in built-in help or read-mes.
- `[RES]` — parameters, defaults, layout, and strings recovered from resources.
- `[ASM]` — exact math still inside compiled 68k code; behavior described from docs; disassembly needed for bit-exactness. (All `[ASM]` items are collected in §14.)

---

## 1. Design philosophy — the personality contract

These are not bugs to fix. They are the product. Any re-creation that "improves" them has failed.

1. **Everything is destructive.** Effects mutate the live sample buffer in place, with only a progress bar between you and the result. There is no preview, no non-destructive chain, no dry/wet slider at the host level.
2. **There is no Undo.** The Edit menu shows a permanently disabled *Undo* item (⌘Z). The infrastructure exists — a temp file named `SFXTemp`, an error string "I couldn't create my temporary file for Undo buffers… You may not be able to Undo.", even a whole CODE segment named "Undo" — but it never worked in 0.9.2. Best of all: if you press *Stop* during an effect, the app warns *"If you stop the effect now you will get a half-processed sound, probably useless. You should therefore immediately Undo."* — advice it is incapable of honoring. The half-processed sound stays. **Preserve this exactly, including the alert text.**
3. **Channels are independent, unequal-length buffers.** A "document" is 1–N channels; each is its own byte buffer with its own length. Channels double as scratch tracks ("you may want to add channels … to use them as a temporary work space" — the Intro). Effects like Granular Synthesis and Sample Hose *append new channels* as their output.
4. **The selection is a rectangle.** A byte range × a *contiguous* channel range. You cannot select non-adjacent channels.
5. **Multiple independent windows.** Every document is a window; Mixer, Spectrum, Level meter, Chunk List, Play Clipboard, Record and per-effect progress are all separate floating windows.
6. **The app talks in the first person.** "I couldn't find any sounds in the document…", "Sorry, man. I haven't got any options.", "I'm sorry, but I can't delete the only channel of a mono sound." Keep this voice.
7. **Units include Meters and Feet.** Durations can be expressed as physical lengths (of sound travelling through air). **Conversion constants recovered by disassembly** `[SOURCE-equivalent — see disassembly/Unit-Conversion.md]`: speed of sound = **344 m/s** and **1128 ft/s**, stored as plain 16-bit integers (not high-precision floats) in the app's two unit converters (`CODE 7`). `meters = seconds·344`, `feet = seconds·1128` (and the inverse). The two are independently rounded, so a meters and a feet reading of the same duration disagree by ~0.05% (implied foot = 0.30496 m) — a real quirk to preserve. Full unit-code table (0=samples,1=bytes,2=seconds,3=ms×1000,4=meters,5=feet) in the disassembly note. Deliberately keep this.
8. **Easter eggs**: the app has a date-triggered string list — "Merry Christmas!" and "Today is Alberto Ricci's birthday!" `[RES]`. **Trigger dates recovered by disassembly** `[SOURCE-equivalent — see disassembly/Easter-Eggs.md]`: a single startup check (`CODE 7 @ 0x086c`, `SecondsToDate`) fires the Christmas greeting on **Dec 25** (month==12 && day==25 → STR# 138 #1) and the birthday greeting on **Sep 17** (month==9 && day==17 → STR# 138 #2), each shown as NoteAlert 146. No year check, no once-per-day suppression — fires every launch on the trigger day, using local date. The About box credits "pizza by: Mommy" and "manual by: No manual, folks!".

---

## 2. Core data model `[SOURCE]`

Translated out of Mac-isms from `Glue.h` / `ModSetups.h` / the SDK manual.

### 2.1 Document (sound)

```
Document
├── name                  (string)
├── rate                  (16.16 fixed-point Hz; e.g. 22254.54 as 0x56EE8BA3-style Fixed)
├── bps                   (bits per sample, 1..32)
├── baseFreq              (base MIDI note, byte; default 60)
├── channels[1..N]        each:
│     ├── data            (raw sample bytes — independent buffer)
│     ├── sizeBytes       (independent length!)
│     ├── outLeft/outRight (per-channel speaker enables)
│     └── volume          (8.8 fixed: 0x0100 = 1.0 full, >1.0 = overdrive allowed, ≤0 = silent)
├── selection
│     ├── selSt, selEnd   (byte offsets, frame-aligned, half-open: [selSt, selEnd) )
│     └── firstSelCh, lastSelCh  (1-based, contiguous range)
├── loop: loopSt, loopEnd (byte offsets; set via Selection ▸ Set Loop)
├── chunks[]              (named regions: Start, Length, Channel, Kind — kinds include "Chunk", "Clipping")
└── units                 (current display unit, see §2.3)
```

### 2.2 Sample format `[SOURCE]`

- A sample occupies `ceil(bps/8)` bytes; **any bps from 1 to 32** is legal (a 12-bit sound uses 2 bytes per sample).
- **8-bit or less** (1 byte/sample): stored offset-binary; convert with `signed = stored XOR 0x80` both ways (`ModGetSetSamples.h`).
- **9–32 bits** (2–4 bytes/sample): stored **big-endian, sign-extended**. Read: load the sample's bytes into the top of a 32-bit word, then arithmetic-shift right by `(4 − nbytes) × 8`. Write: store the top `nbytes` bytes of the 32-bit value. Note the value occupies the *full range of its byte container* — a 12-bit sound's samples are read as 16-bit values.
- Value limits for clipping come from bps (host callback `ModGetSampleValueLimits`) — for byte-aligned depths effectively `±(2^(8·nbytes−1))`. Exact behavior for odd bps (e.g. true ±2^11 for 12-bit vs ±2^15) `[ASM]` — Gain.c clamps against whatever the host returns.
- All arithmetic in shipped effects is **integer math**: `sample = sample * gain / 100`, then hard clip. `[SOURCE]` This integer rounding is part of the sound.

### 2.3 Units `[SOURCE: ModUnits.h]`

`0 samples · 1 bytes · 2 seconds · 3 milliseconds · 4 meters · 5 feet`

Every duration field in the app and in effect dialogs carries a units popup. Values are always *stored* in bytes; conversion is a host service (`ModConvertToUnits` / `ModConvertToBytes`). The app-level "default selection units" preference offers only samples/bytes/seconds/ms; the meters/feet lunacy appears in effect dialogs and the Selection menu contexts. `[RES]`

### 2.4 Selection semantics

- Selection bounds are byte offsets **snapped down to frame boundaries** (`start -= start % frameSize`) `[SOURCE: Gain.c]`.
- Per-channel clamping: if a selected channel is shorter than `selEnd`, the range clamps to that channel's length — each channel processes its own intersection `[SOURCE: Gain.c]`.
- **Empty selection (selSt == selEnd) means "everything"**: effects expand to the whole channel range before processing `[SOURCE: Gain.c]`.
- An insertion point (caret) exists when selSt == selEnd; arrow keys move it like a text editor; shift-arrows extend selection `[DOC: Intro]`.

---

## 3. The document window `[RES + DOC]`

Template: `WIND 128`, initial frame 454×252 px at (20, 50), zoomable/resizable ("Document windows are fully resizable" — Read Me 0.9.1). Default title for new docs: `untitled sound #N`.

Three horizontal zones (Intro):

1. **Info strip (top)** — each field individually toggleable in Preferences page 2: sampling rate and size ("22.050 kHz; 16 bits"), number of channels ("Mono"/"Stereo"/"N channels"), file size in bytes, selected channel's length in samples, in seconds. Optional custom background color for the wave area.
2. **Wave editor (middle)** — one lane per channel:
   - Waveform drawn using **Dots or Lines** (a preference! dots was the original mode).
   - Left of each lane: **two speaker toggle icons** (left/right output enable) — these also drive Quick Mix routing.
   - Right of the lanes: a **channel icon column** plus a **"New Channel" icon**. Drag New Channel icon into the column → append channel. Drag a channel icon onto the Trash → delete channel (refusing to delete the last one: *"I'm sorry, but I can't delete the only channel of a mono sound. Delete the document instead."*). Drag one channel icon onto another → swap positions.
   - A selection-position indicator shows where the current selection sits in the whole sound.
   - Zoom: Zoom In (⌘=), Zoom Out (⌘-), Fit in Window, Fit Selection; option-Zoom-In = max zoom; option-Fit = fit selection.
3. **Transport (bottom)** — tape-recorder buttons: Record, Stop, Pause, Play, Fast Forward, Loop toggle, Volume popup (Off, 1–7).

### Other windows `[RES]`

| Window | Template | Notes |
|---|---|---|
| Level (input meter) | WIND 134, 79×168, palette-style | live input level; **Sound Input Gain slider** (0–255) when hardware supports; click device button opens Sound control panel |
| Record | WIND 133, 288×186 | see §6 |
| Mixer | WIND 132, 480×270 | **UNFINISHED in 0.9.2** (Read Me lists it as a future feature) — **recovered by disassembly** `[SOURCE-equivalent — see disassembly/Spectrum-Mixer.md]`: modal window sized to one 128-px vertical **volume fader** per channel (CNTL 200), plus **Mix** (CNTL 201) / **Cancel** (CNTL 202) buttons and a **"Mix to:" popup** (CNTL 203 → MENU 158 = New Document / New Track). Faders drag and buttons close the window, but the **Mix action is a stub** (`CODE 9 @0x796`: `moveq #0,d0; rts`) — no mixdown is performed. |
| Spectrum | WIND 136, 480×270 | **UNFINISHED in 0.9.2** (Read Me lists it as a future feature) — **recovered by disassembly** `[SOURCE-equivalent — see disassembly/Spectrum-Mixer.md]`: modal window with a single **OK** button (CNTL 500) that draws only a frame/axis + playback cursor. It never reads or plots sample data; there is **no FFT** anywhere in the app (zero Elems68K app-wide, zero FP68K in the Spectrum segment). An empty placeholder. |
| Chunk List | WIND 135, 170×100, grow box | list of chunks; export as text (columns Start / End / Length / Channel / Kind) |
| Play Clipboard | WIND 138, 220×61 | tiny window with Stop Playing button — the clipboard is a first-class playable sound |
| Effect Progress | WIND 130, 328×58 | progress bar + **Pause/Resume** + **Stop**; module may display its own PICT/TEXT ad inside |
| Effect Info | WIND 131 | per-effect about/help box (shift-choose an effect) |

---

## 4. Complete menu reference `[RES]`

**Apple ()**: About SoundEffects… · Registration Form…

**File**: New ⌘N · Open… ⌘O (option = "open any kind of document") · Close ⌘W · Save ⌘S · Save As… · Revert (option = no confirmation) · Preferences · Quit ⌘Q

**Edit**: Undo ⌘Z *(always disabled — see §1.2)* · Cut ⌘X · Copy ⌘C · Paste ⌘V · Clear · Leave Silence… · Shift Sound… · Zoom ▸ (Zoom In ⌘=, Zoom Out ⌘-, Fit in Window, Fit Selection) · Play Clipboard
— Cut/Copy/Paste/Clear dynamically relabel to **Cut Channel / Copy Channel / Paste Channel / Clear Channel** (when whole channels are selected) `[RES: STR# 136]`.

**Selection**: Select All ⌘A (shift = single channel; option = stereo pair) · Select None ⌘D · Extend to Beginning · Extend to End · Select Exactly… ⌘E · Select Peak… ⌘K (option = skip dialog: margin 0, Selection scope — **not** "repeat last settings"; corrected by disassembly) · Select Silence · Set Loop ⌘L · Select Loop · Crop · Selection Info… ⌘I

**Sound**: Record… · Stop · Pause · Play · Fast Forward · Loop · Volume ▸ (Off, 1–7)

**Effects**: Redo Effect ⌘/ · ——— · *(populated at launch by scanning the Effects Folder; see §7. Modifier keys: plain = show settings then run; option = run with last settings, no dialog; shift = show effect's about/help box; option+shift = show settings dialog but don't run.)*

**Special**: Show Spectrum… · Mixer… *(no ⌘M — the resource assigns no shortcut; corrected by disassembly)* · Show Chunk List · Set Chunk ⌘U · Find Clipped Samples… (option = repeat last settings) · Change Sound Info… — *note: Show Spectrum and Mixer are unfinished feature shells in 0.9.2 (see §3 and `disassembly/Spectrum-Mixer.md`).*

**Windows**: Sound Level Meter · ——— · *(open document list, dynamic)*

### Keyboard map `[DOC: Intro]`

space = play/pause toggle · F1–Fn = solo-play channel n · option-Fn = mute channel n · arrows = move insertion point · shift-arrows = extend selection · delete = clear selection · option-Play = loop using the Set Loop region instead of the selection · option-Record = quick record (new doc + record window).

---

## 5. Editing semantics

- **Cut/Copy** put actual sound (with its rate/bps) on the clipboard. **Play Clipboard** plays it in its own window.
- **Paste** into a document with different bps/rate prompts: *"You are trying to paste a sound sampled at ^1 bits into a sound sampled at ^2 bits. Do you want me to resample the sound you are inserting at ^3 bits?"* (same for kHz) — Resample / Cancel `[RES: ALRT 152/166]`.
- **Clear/delete** removes the selected range from the selected channels only — since channels are independent buffers, this shortens *only those channels*, letting tracks drift out of alignment. This is a core weirdness. (Exact whole-channel vs range behavior of shipped code `[ASM]`, model per §2.)
- **Leave Silence…**: "Leave [N units] of silence at the beginning of (• the Sound / • the Selected Channels)" — trims everything else `[RES: DITL 142]`.
- **Shift Sound…**: "Shift {the whole sound / channel N / channels N to M / the selection} by [N units], • Shift Left / • Shift Right" `[RES: DITL 139 + STR# 139]`.
- **Crop**: keep selection, discard the rest.
- **Select Exactly…**: "Select exactly [N units] of sound • Before / • Centered on / • After {the selection | the insertion point}" `[RES: DITL 140]`.
- **Select Peak…**: "Select the peak of • the Sound / • the Selection, leaving [N units] of margin at the left and the right of the peak" `[RES: DITL 141]`. **Algorithm recovered by disassembly** `[SOURCE-equivalent — see disassembly/Select-Silence-Peak.md]`: peak metric = |sum of all selected channels' samples| at each frame position (channels shorter than the position skipped), first max wins on ties; selects exactly one frame at the peak, then widens by the margin (converted to bytes) symmetrically, clamped at 0 and the selected channels' max length. Margin field opens at "0" each time — only the units popup code is remembered (per document). "the Sound" radio pre-selected iff the selection is empty or full-document-all-channels. Option key skips the dialog (margin 0, Selection scope).
- **Select Silence**: no dialog. **Recovered by disassembly** `[SOURCE-equivalent — see disassembly/Select-Silence-Peak.md]`: threshold is **exact digital zero** (8-bit byte == 0x80; otherwise every byte of the frame == 0x00) — no tolerance. Requires an insertion point (silently does nothing on a non-empty selection); scans backward/forward from the caret in each selected channel and **intersects** the per-channel silent ranges (max of starts, min of ends), leaving one frame of silence *unselected* as margin at each silent/non-silent boundary (margin dropped where silence reaches a channel edge). Caret not inside silence → no-op.
- **Set Chunk** marks the selection as a named chunk; **Find Clipped Samples…**: "Find all portions of • the Sound / • the Selection where [N] or more consecutive samples have a level greater than or equal to [X]% of the maximum" — results become "Clipping" chunks, max 800 reported ("There are more than 800 clipping points…") `[RES]`.
- **Change Sound Info…** (Edit Header): directly edit Sampling Rate (Hz, with rates popup), Sample Size (bits), Channels, Base MIDI value — with the warning "you should never set these values directly… or you may end up with undesired effects." **This is a feature: header lies = instant pitch/format mangling.** If the entered size differs, the app asks: *"Do you want me to really resample the N-bit sound to M bits, or just change the bits per sample value?"* — **Resample / Change bps** `[RES: ALRT 151]`. Keep the "just lie about it" path.
- **Memory-pressure behaviors worth keeping as character** (translate to buffer/quota limits on modern platforms): "Not enough memory to proceed. Probably, discarding the clipboard's contents would free enough. Can I throw away its contents?"; partial open: "Only ^1 samples can be opened, out of ^2. Should I open only a part of the sound? (from sample [__] to sample [__])" — partial file loading as a first-class dialog `[RES: DITL 144, 165]`.

---

## 6. Playback & recording

**Playback**: Play from beginning or insertion point (preference). Loop button loops the selection; option-Play loops the Set Loop region. Volume in 8 steps (Off, 1–7). Function keys solo individual channels; option-F mutes. Per-channel L/R speaker enables route output. 16-bit playback requires capable hardware, else: "There is not enough hardware to play the selected sound. You may want to downsample it before playing."

**Recording** `[RES + DOC]`: Record window shows live mini-waveform — **green when monitoring, red while recording**. Buttons: Save (insert into document) / Cancel / Device Settings…; popups: Use (Mono/Stereo), Sampling Rate, Sampling Size, Play-Through Volume (Off,1–7); Auto Gain checkbox.

- Rates offered: 5, 7, 11, 11.1, 22.050, 22.254 ("Standard"), 24 ("Voice Recognition"), 32, 44.1 ("CD"), 48 kHz ("DAT") `[RES: MENU 155]`.
- Active-channels matrix (Preferences): Mono+both = mix L/R inputs; Mono+1 = left only; Mono+2 = right only; Stereo = two separate channels `[DOC: Intro table]`.
- **VOX (voice-activated) recording** `[RES: CNTL 307–313]`: recording start trigger On/Off with threshold slider (0–255), independent stop trigger with threshold and **stop-delay** slider. A destructive weirdo sampler wants this.
- Insertion policy preference: "Insert the whole recorded sound" vs **"Constrain the recorded sound to the selection"** (record *into* a hole).
- Record cap preference: "Record no more than [1000] K (☐ only if virtual memory is active)".
- "Beep when clipping occurs" while recording.

---

## 7. The effects engine (host ⇄ module contract)

Modeled directly on the plug-in ABI — keep this shape so the web prototype and the Eurorack firmware can share the plugin model. **This section is `[SOURCE]`**: the full ABI is present as C headers and reference source in the shipped **SoundEffects Developer's Kit** (`SoundEffects 0.9.2 ƒ/SoundEffects Developer's Kit/Interfaces/*.h`, plus complete `Gain` and `EmptyEffect` example modules). The contract below is transcribed from that source, not inferred — treat it as authoritative and cross-check the disassembled modules against it.

### 7.1 Discovery & menu

At launch, scan the **Effects Folder** (next to the app, or in Preferences). Each module contributes one menu item; a module with a `Grup` string goes into that named submenu. Recovered grouping `[RES]`:

- **Echoes & Reverbs**: Delay, Echo, Flanger
- **FIR/IIR Filters**: Bandpass FIR, Bandstop FIR, High Pass FIR, High Pass IIR, Low Pass FIR, Low Pass IIR, Shelving
- **Resampling**: Downsample Bits, Resample Rate, Upsample Bits
- **Misc**: Difference, Draw Line, Invert, Sum
- **Mike's Modules**: Granular Synthesis, Iterative Crossfade, Sample Hose
- **Selection**: Zero Crosser
- **Top level**: Amplify, Dither, Fade In, Fade Out, Keyboard, Noise, Normalize, Quick Mix, Replicate, Reverse, Robotize, Selection to Channel, Silence, Smooth, Waveform Generator, Weird Tempo

Menu templating: "<Name> Options…", "About <Name>…" `[RES: STR# 140]`.

### 7.2 Module entry point & messages `[SOURCE: Glue.h]`

An effect file is a code resource whose `main` is the single entry point; the host passes it one of five messages. In practice modules link the SDK's `Glue.c`, which decodes the message and calls one of three routines the module author writes (`effect`, `settings`, `about`):

```c
pascal OSErr main(OSType message, ModParamsPtr modInfo, GluePtr glue68k, GluePtr gluePPC);

// messages (OSType four-char codes):
'Mrun'  kMsgRun          → effect(modInfo, glue, &prefs)          // run, no dialog
'Mset'  kMsgSettings     → settings(modInfo, glue, &prefs)        // edit settings only
'Msar'  kMsgSettingsRun  → settings(...) then effect(...)         // edit, then run
'Minf'  kMsgAbout        → about(modAboutInfo)                    // show help/about
'Mhvr'  kMsgHeadersVers  → store kHeadersVersion (=1) at *GluePtr, return noErr

// the three (+1 helper) routines a module supplies:
pascal OSErr effect  (ModParamsPtr modInfo, GluePtr glue, ModSettingsHandle *prefs);
pascal OSErr settings(ModParamsPtr modInfo, GluePtr glue, ModSettingsHandle *prefs);
pascal OSErr about   (ModAboutPtr  modInfo);
       OSErr GetModSettings(ModParamsPtr, GluePtr, ModSettingsHandle *prefs);  // load-or-default
```

- Effect code resources come in three flavors selected by CPU/FPU at load: **`EFCT`** (68k), **`EFCF`** (68881 FPU build), **`EFCP`** (PowerPC). `main` receives both a 68k and a PPC `GluePtr` so a fat module can pick.
- **68k global storage** `[SOURCE: ModA4Globals.h]`: 68k modules reach their globals through **A4**, not A5 (they are foreign code loaded into the host's world). The SDK's `MAIN_SETUP_GLOBALS`/`MAIN_RESTORE_GLOBALS` macros (`SetCurrentA4`/`RememberA4` on CodeWarrior, `SetUpA4` on THINK C) bracket every entry; PPC builds compile them to no-ops. A prototype/firmware port doesn't need A4 but must preserve the *"module state is per-call, set up on entry"* model.
- `prefs` is a module-owned `ModSettingsHandle` (opaque `struct ModSettingsRec` the author declares). The host persists it keyed by the module's **signature** (a Pascal string, e.g. Gain's `"\pSampleEffect"`). `settings` loads-or-defaults, shows the dialog, saves via `SaveEffectSettings`, and **leaves the handle allocated** so an immediately-following `effect` (the `'Msar'` path) can consume it; `effect` disposes it. Run-with-last-settings (option-key / Redo Effect) is just `'Mrun'` reusing the persisted blob.

### 7.3 What the module sees: `ModParamsRec` `[SOURCE: Glue.h]`

The host hands every routine a pointer to the live document/selection state (read-only fields must be changed through callbacks, not directly):

| field | type | meaning |
|---|---|---|
| `version`, `refNum` | short | ABI version; the module's own resource-file refNum (do `UseResFile(refNum)` before `Get1Resource`) |
| `name` | StringPtr | document title |
| `parentWind` | WindowPtr | the document window |
| `hands` | `SEChanHandle` | array of `{ Handle chan; unsigned long size; }` — **one per channel**; `size` is **bytes, not samples**. Access `(*modInfo->hands)[ch].chan` / `.size` |
| `outModes` | ChanAttrsHandle | **read-only** per-channel output attrs (L/R pan routing) — use `ModGet/SetChannelPan` |
| `selSt`, `selEnd` | unsigned long | selection start/end **in bytes** |
| `loopSt`, `loopEnd` | unsigned long | loop region (Set Loop) |
| `rate` | Fixed | sample rate, 16.16 fixed-point Hz |
| `bps` | short | bits per sample (1–32) |
| `numChans` | short | channel count |
| `firstSelCh`, `lastSelCh` | short | selected channel range (**1-based**; loop `ch = firstSelCh-1 … lastSelCh-1`) |
| `baseFreq` | Byte | MIDI base note for playback pitch |
| `doExtend` | short | resolved extension policy for this run (see 7.6) |
| `units` | short | current display-units code (`ModUnits.h`) |

### 7.4 The sample model, exactly `[SOURCE: ModGetSetSamples.c]`

This is *the* reason the originals sound the way they do; reproduce the integer path byte-for-byte. `frameSize = bps/8 + (bps%8 != 0)` bytes per sample; samples are packed big-endian; the effective value is sign-extended and right-aligned into a 32-bit signed register:

```c
// 8-bit: stored as offset-binary, converted to signed by XOR 0x80
#define GetSample8(x)      ((signed char)(*(signed char *)(x) ^ 0x80))
#define SetSample8(x, s)   *(signed char *)(x) = (SignedByte)(s) ^ 0x80

// 9–32-bit: read min(frameSize,4) big-endian bytes into the HIGH bytes of a long,
// then arithmetic-shift down to sign-extend and right-align:
signed long GetSample16(Ptr p, short bps) {
    signed long v; short n = min(frameSize, 4);
    for (byte=0; byte<n; byte++) ((uchar*)&v)[byte] = ((uchar*)p)[byte];  // big-endian, top-justified
    return v >> (sizeof(long)-n)*8;                                       // arithmetic (signed) shift
}
// SetSample16 writes back the top n bytes of the value. Sample value limits per bps
// come from ModGetSampleValueLimits(bps, &min, &max); all effect math clamps to [min,max].
```

All DSP is integer. `s * gain / 100`, `(a+b)/2`, truncating divides, saturating clamps — the rounding scars and aliasing are the point. (This is also why disassembled surprises like **Smooth**'s center-excluding 2-point average or **Flanger**'s `fintrz` truncation matter: they are exactly this integer discipline, verified in the binary.)

### 7.5 Declarative settings dialogs `[SOURCE: ModSetups.h]`

Rather than draw its own dialog, a module hands the host an **`ItemInfoRec[]`** (one entry per DITL item: `{ short type; ProcPtr proc; long param; }`) and the host runs the entire modal dialog — fields, radio groups, unit conversion, and the named-setups popup — via `ModDoSettingsDialog`. The item types (this is the schema to reuse for web forms *and* hardware encoder pages):

| `type` | `param` points to | notes |
|---|---|---|
| `type_ignore` | — | static/unused item |
| `type_button` | ProcPtr action | see return convention below |
| `type_checkBox` / `…NoSave` | `short` (0/1) | |
| `type_radioButton` / `…NoSave` | `short` = active index (**1-based**); pass the *same* pointer to every button in a group | |
| `type_unitsPopup` | `short` = `ModUnits.h` code | required if any unit-dependent field is present |
| `type_editFieldUnitDependent` / `…NoSave` | `unsigned long` **in bytes** | host auto-converts to/from the popup's unit; the variable is **always bytes** |
| `type_editFieldNumerical` / `…NoSave` | `long` | plain integer field |
| `type_editFieldNumDecimal` / `…NoSave` | `double` | decimal field |
| `type_editField` / `…NoSave` | StringPtr | raw text |
| `type_customItem` | `SettCustomItemRec` | author-drawn item with init/hit/close + optional save/load |

`NoSave` variants work identically but are not remembered in a saved setup. Button/custom procs return: **1** = OK-and-continue, **0** = dismiss as if OK, `kModCancel` = dismiss as Cancel, **−1** = "values now differ from the selected setup" (deselects the setups popup), any other value = cancel and report that error. **Named setups** (host-managed): the "Setup:" popup with Save setup as… / Delete setup… is driven by `ModGetSetupsNumber`/`ModGetSetup`/`ModGetSetupName`/`ModSetSetup`/`ModDeleteSetup`/`ModSetupCreateMenu`/`ModSetupSwitchCustom` `[RES: MENU 160, DITL 154/155]`. **Unit codes** `[SOURCE: ModUnits.h]`: `0 samples · 1 bytes · 2 seconds · 3 milliseconds · 4 meters · 5 feet` — exactly the table the app's converters implement (see `disassembly/Unit-Conversion.md`; meters=×344, feet=×1128).

### 7.6 Host services — the `GlueRec` callbacks `[SOURCE: Glue.h]`

The module calls back into the host through a `GlueRec*` (`glueVers` 1). Signatures verbatim, grouped:

- **Progress**: `ModSetupProgress(modInfo,&timeToCallProgress,textStr)` · `ModShowProgress(cur,total,&timeToCallProgress,canCancel) → OSErr` (nonzero return = user cancelled; **half-processed audio stays**, §1.2) · `ModCloseProgress()` · `GetProgressWind()`. Delay before showing, and Excellent/Good/Fair/Poor responsiveness, are host prefs; the `timeToCallProgress` flag tells the module when to actually call.
- **Settings persistence**: `LoadEffectSettings(signature,&hand)` · `SaveEffectSettings(signature,&hand)` · `GetEffectSettingsRefNum()`.
- **Settings dialog + setups** (8): `ModDoSettingsDialog(modInfo,itemInfo,withSetups,custFilter,custItemHandler,initProc)` and the seven setup-management calls listed in 7.5.
- **Environment**: `GetMachineInfo(&MachinePtr)` → a `MachineRec` of pre-resolved Gestalt flags (`hasFPU, hasColorQD, has32BitQD, hasQuickTime, hasSoundManager3, hasSoundInputDevice, hasStereoOutput, hasStereoInput, mixesStereoOutputToMono, hasPlayAndRecord, has16BitSoundIO, hasMultipleChannels`) · `AskDoExtend() → short` · `ModInitRandSeed()`.
- **Dialog utilities**: `ApplWindowsUpdate(w)` · `ApplWindowsActivate(evt)` · `ModConvertToUnits(modInfo,destStr,srcVal,newUnits)` / `ModConvertToBytes(modInfo,srcStr,&destVal,newUnits)` — **these two are the `CODE 7` converters reverse-engineered in `disassembly/Unit-Conversion.md`.**
- **Selection queries**: `GetBytesToProcess(modInfo) → long` · `ModMaxChSize(modData,&size)` (max over all channels) / `ModMaxRelChSize(modData,&size)` (max over *selected* channels) · `ModGetPeak(modData,&peak,&pos)` · `ModGetSampleValueLimits(bps,&min,&max)` · `ModGetOffsToSndHeader(sndH)` / `ModGetOffsToSndData(sndH)`.
- **Utilities / drawing**: `ModToggleInputLevel(on)` · `ModDrawWave(modInfo,&bounds,maxSize,firstByte,zoom,firstCh,lastCh)` · `ModShowError(modInfo,errNum,withCancel)`.
- **Document restructuring**: `ModAddNewChannel(modData,where)` · `ModDelChannel(modData,chan)` · `ModGetChannelPan(chAttrs,channel,leftOne)` / `ModSetChannelPan(chAttrs,channel,leftPan,rightPan)` · `ModSilenceOut(modData,chSt,chEnd,st,end)` · `ModResampleSoundBits(modData,newBps)` / `ModResampleSoundRate(modData,newRate:Fixed)` — the host-side resamplers that **Resample Rate / Up/Downsample Bits delegate to** (still `[ASM]` at app level, the last unrecovered DSP) · `ModNewDocument(&modData)` / `ModChangeNewDocument(modData)`. Modules like Granular Synthesis and Sample Hose use these to append channels and spawn documents. **`ModResampleSoundRate` / `ModResampleSoundBits` are real host DSP** (`CODE 4 @0x42be` / `0x3d44`, behind A5-swapping glue wrappers `CODE 8 @0x39b6` / `0x3672`), **not** delegated to the OS SoundConverter — full recovery in `disassembly/Host-Resamplers.md`: rate = drop-sample/zero-order-hold (truncated float position, no interpolation); bits = plain truncation/zero-extension (no rounding, no dither — the module owns dithering).

### 7.7 Result codes & error voice `[SOURCE: Glue.h]`

The **sign** of the return value tells the host whether the buffer was touched:

- `kModNoError` (0) — success, data modified.
- `kModCancel` (`userCanceledErr`) — module did nothing (no-op).
- **Positive** = error, **data left untouched**: `kModGenericError` (1), `kModNotEnoughMemory` (2), `kModCouldntLoadMyRes` (23), `kModUnknownMessage` (30).
- **Negative** = error, **but data WAS changed**: `kTellGenericError` (−1), `kTellNotEnoughMemory` (−2), `kTellCouldntLoadMyRes` (−23).
- If the returned code's **absolute value** indexes a string in the module's own `STR# 1000`, the host shows that string — how modules speak in their own voice (§8 catalog). `settings` returns `kModHasNoSettings` (1) when the module has no parameters ("Sorry, man. I haven't got any options.").

### 7.8 Reference effect — `Gain.c` = **Amplify** `[SOURCE, complete]`

The SDK's `Gain` example *is* Amplify's algorithm (signature `"\pSampleEffect"`, `struct ModSettingsRec { short gain; }`, default `gain = 200`, clamped ≤ 32767 in `settings`). It is our one piece of real C source for a shipping-equivalent effect and nails the canonical `effect()` skeleton every module follows — worth mirroring exactly in the prototype's plugin base class:

```c
pascal OSErr effect(ModParamsPtr modInfo, GluePtr glue, ModSettingsHandle *prefs) {
    UseResFile(modInfo->refNum);                       // 1. own resource file
    myGain = (**prefs)->gain;  DisposHandle(*prefs); *prefs = 0;   // 2. read & free settings
    progressString = Get1Resource('STR ',128);         // 3. progress text ("Amplifying by ")
    glue->ModSetupProgress(modInfo, &timeToCallProgress, progressString);
    if (modInfo->selSt == modInfo->selEnd)             // 4. empty selection ⇒ whole channels
        { modInfo->selSt = 0; glue->ModMaxRelChSize(&modInfo, &modInfo->selEnd); }
    glue->ModGetSampleValueLimits(modInfo->bps, &min, &max);
    frameSize = modInfo->bps/8 + (modInfo->bps%8 != 0);
    for (channel = modInfo->firstSelCh-1; channel < modInfo->lastSelCh; channel++) {
        start = selSt; end = selEnd;                   // 5. clamp to THIS channel's size…
        if (start > channelSize) start = channelSize;
        if (end   > channelSize) end   = channelSize;
        start -= start%frameSize; end -= end%frameSize;// …and frame-align (unequal channels!)
        HLock(chan);
        for (loopPtr = start; loopPtr < end; loopPtr += frameSize) {
            mySample = (frameSize==1) ? GetSample8(loopPtr) : GetSample16(loopPtr, bps);
            mySample = mySample * myGain / 100;         // 6. integer gain
            if (mySample < min) mySample = min;         // 7. saturating clamp
            else if (mySample > max) mySample = max;
            (frameSize==1) ? SetSample8(loopPtr,mySample) : SetSample16(loopPtr,mySample,bps);
            if (timeToCallProgress &&
                (progressResult = glue->ModShowProgress(processed, total, &timeToCallProgress, false)))
                break;                                  // 8. non-cancellable (canCancel=false)
        }
        HUnlock(chan);
        result = kModNoError;
    }
    glue->ModCloseProgress();
    return result;                                      // kModCancel if nothing was in range
}
```

Every recovered module in §8 is a variation on this skeleton — the differences are the inner kernel (steps 6–7) and whether the pass runs forward/backward, extends the buffer, or appends channels.

---

## 8. Effect catalog — all 38 modules

Format: **Name** (version, author, group) — algorithm. Parameters. Fidelity.

All ©1994 Alberto Ricci v1.1 unless noted. "Units popup" = the 6-unit popup of §2.3. Defaults marked ⌘ are baked into the DITL; other fields start empty on first run and thereafter remember last-used values via Effect Prefs.

1. **Amplify** — `s' = clamp(s × gain / 100)` integer math `[SOURCE — the SDK's Gain.c is this module; full annotated source in §7.8]`. Param: "Amplify by: [__] %". Progress text "Amplifying by ". Empty selection ⇒ whole channels (`ModMaxRelChSize`), per-channel + frame-aligned, saturating clamp to `ModGetSampleValueLimits(bps)`, non-cancellable progress.
2. **Bandpass FIR** — `y(n) = x(n) − x(n−2) − 0.25·y(n−2)`; zeros at z=±1, poles at ±0.5i `[DOC]`. No params. *Note: named "FIR" but has a recursive term — the misnomer ships as-is.*
3. **Bandstop FIR** — `y(n) = [x(n) + x(n−2) + 0.25·y(n−2)]/2`; zeros ±i, poles ±0.5 `[DOC]`. No params.
4. **Delay** (Echoes & Reverbs) — **recovered by disassembly** `[SOURCE-equivalent — see disassembly/Echo-Delay.md]`: exactly one offset copy mixed in, `dst[n] = clamp(dst[n] + trunc(src[n−D]·strength/100))` computed as a single **backward** in-place pass (destination walks from the end down to `start+D`, always reading the untouched original `D` bytes earlier — no feedback, no repeats). `D` = Delay param frame-aligned to the sample size; Strength = mix %. If extension is allowed, the buffer grows by exactly one `D` and the new tail is silenced before mixing (degrades gracefully, no error, if the grow only partially succeeds); if not, the part of the copy that would fall outside the selection is simply dropped. Params: Delay [__ units popup], Strength [__]% `[RES]`. Non-cancellable progress.
5. **Difference** (Misc, v1.1.1) — `ch2' = ch1 − ch2` conceptually; output goes to • New Channel / • New Document, ☐ Normalize the difference `[RES]`. Requires exactly 2 selected channels; >2 asks "subtract the second selected channel from the first one?". Has balloon-help strings `[RES: STR# 1100]`.
6. **Dither** — adds random noise: • [__]% of max amplitude or • [__] points (absolute). "10% ≈ 12 points in 8-bit, 3276 in 16-bit" `[DOC]`. Own OK/Cancel dialog (drawn by module, not host schema).
7. **Downsample Bits** (Resampling) — **recovered by disassembly** `[SOURCE-equivalent — see disassembly/Downsample-Bits.md]`: set bps lower. Like Resample Rate, the actual bit truncation is a host service call (`ModResampleSoundBits(modData, newBps)`) — the module owns only validation and optional pre-processing. When "Expert features" + the noise-adding radio are on, it injects `sample += clamp(Random()·noiseScale/32767)` into **every sample of the whole document** before the host truncates, where `noiseScale ≈ 2^(oldBits−newBits)` (computed via SANE float round-trip) — i.e. dither/shaping amplitude auto-scales to the size of the discarded LSBs, not a fixed constant. The exact radio-button binding (this traced path has no user-value dependency, so it reads as "Noise shaping"; "Dither"'s own user-specified-LSB code path was not located and may be a dead path, mirroring this app's confirmed non-functional Undo precedent — see the doc's caveat section) is the one remaining open sub-question. Params: Downsample to [__] bits; ☐ Expert features: • Dither [__] LSB in amplitude / • Noise shaping `[RES]`. Whole document only — selection triggers "Do you want me to proceed and downsample the whole sound?". Warns if dither ≥ 1 LSB.
8. **Draw Line** (Misc, v1.0) — replaces selection with a straight line from first sample to last `[DOC]`. De-click tool / instant ramp generator. No params.
9. **Echo** (Echoes & Reverbs) — **recovered by disassembly** `[SOURCE-equivalent — see disassembly/Echo-Delay.md]`: the *same* offset-and-scale kernel as Delay, but computed as a single **forward** in-place pass, `dst[n+D] = clamp(dst[n+D] + trunc(src[n]·strength/100))` for `n` from selection start upward — since the destination of an early sample becomes the source read of a later one, this one pass is recursive and produces a whole exponentially-decaying echo train by itself. "Only N echoes" checked: mix range = `end + N·D`, no decay math. Unchecked ("until it decays"): mix range is precomputed by iterating `amp = amp·strength/100` from 100, extending the range by one `D` each step, until `amp ≤ 8` or 80 echoes are reached — this sizing loop never touches audio; the audible decay curve is an emergent property of the recursive pass. Params: Delay ⌘3500 [units popup], Strength ⌘70 %, ☐ Only [__] echoes `[RES]`. Same buffer-grow/silence/clamp/non-cancellable-progress machinery as Delay.
10. **Fade In** — linear fade-in. Param: Fading time [__ units] `[RES]`. Had FPU & PowerPC variants (float-capable builds of same algorithm).
11. **Fade Out** — linear fade-out, same param.
12. **Flanger** (Echoes & Reverbs) — **recovered by disassembly** `[SOURCE-equivalent — see disassembly/Flanger.md]`: modulated-delay comb filter, delayed copy mixed with original, delay = Delay ± Width swept over Period by an LFO. **No interpolation**: the delay position is computed to full SANE-extended precision but truncated toward zero (`fintrz`) to a single integer sample offset before the one-and-only delay-line read — sub-sample precision is computed and then discarded. **Feedback is a sweep-direction flip, not a separate tap**: the whole per-channel DSP routine is compiled twice, selected by the Feedback checkbox; Feedback off walks the buffer backward (every read strictly precedes any write, so it only ever touches pristine history — the same trick documented for Delay), Feedback on walks it forward (so once the sweep outruns the current delay offset it reads back its own already-mixed output — the same trick as Echo), with no separate feedback-gain constant. **LFO**: Sinusoidal uses a real `sin()` call (SANE/68881, not a table), phase = 2π·(samples processed)/Period; Triangular is a linear accumulator (±4·Width/Period per sample) that reflects at Delay±Width; Sawtooth is the same accumulator (+2·Width/Period) that hard-resets to Delay−Width instead of reflecting. "Flange percentage" 0–100 sets the mix: 100% = delayed only, 0% = original only, via plain truncating integer math (`dry·(100−pct)/100 + wet·pct/100`), written back in place. ☐ Feedback (see above). ☐ Envelope Follower: Depth [units] and "Louder means • Longer / • Shorter delays" — delay offset ± `interpolatedPeak · Depth / maxSampleValue`, where `interpolatedPeak` is a **block-peak follower** (`ModGetPeak` over 1024-sample windows, linearly interpolated between block boundaries), not RMS or a one-pole smoother. Non-cancellable progress, no `doExtend` interaction (fixed-length in-place effect). All five duration fields carry unit popups (delay in meters!). Ships 9 factory setups (`Setp` resources, 36-byte on-disk records distinct from the 60-byte runtime settings struct; all decoded to real ms values — Plain Flange family at Delay≈11 ms/Width≈2 ms with Period 800–4750 ms, "Soft Flange" is just another Period slotted into that family, Distorto High/Medium/Low at Delay≈5 ms/Period 300–600 ms with Envelope Follower on; Feedback is off in all 9 factory presets — full table in `disassembly/Flanger.md`).
13. **Granular Synthesis** (Mike's Modules, ©1995 Michael Norris, v1.0) — granulates the selection **into new channels** `[DOC, formulas]`: quasi-Gaussian grains stepped through the source. Params: New Length [units], Initial/Final Grain Length, Variance, Curvature %, Initial/Final Density %, density Curvature %, Fade Level %. Tween formula (shared with Sample Hose): `v(n) = ((n/l)^(c/100))·(vf−vi)+vi` — curvature 100 = linear, 0 = convex, 200 = concave (confirmed: a genuine `pow()`, result truncated with `FTINTX` before use as a byte count). **Grain envelope recovered by disassembly** `[SOURCE-equivalent — see disassembly/Granular-Synthesis.md]` — and here the docs are *accurate* (contrast Smooth): with `x` = offset into grain, `n` = grain length, `f` = FadeLevel/100, the code computes `t = 5x/n − 2.5`, `P = (1 − e^(−t²))³`, **`y = (1 − P) + P·f`** — i.e. a linear blend `y = E(x)·(1−f) + f` between Roads' truncated curve `E(x) = 1 − (1 − e^(−(5x/n − 2.5)²))³` (Fade Level 0%) and unity (Fade Level 100%). Edges sit at `y≈0.0058`, centre exactly 1.0; flatter-topped than a Gaussian (the cubing) — Roads' Gaussian/rectangle compromise. Applied as a **floating-point per-sample gain then clamped** (not the Ricci integer convention — hence the FPU/PPC builds). The published formula was only ever a *picture* in the read-me, but `PICT 1001` carries the original **Expressionist equation source** as text, which independently confirms the disassembly. **Variance RNG = the textbook ANSI-C `rand()`**: `seed = seed·0x41C64E6D + 0x3039; return (seed>>16) & 0x7FFF` — grain jitter is exactly reproducible.
14. **High Pass FIR** (Filters) — `y(n) = [x(n) − x(n−1)]/2` `[DOC]`. No params.
15. **High Pass IIR** (Filters) — `y(n) = a·x(n) − b·y(n−1)`; params: Coefficients a [__], b [__] (decimal fields) `[DOC+RES]`. No range validation mentioned — unstable filters are user's privilege.
16. **Invert** (Misc, v1.0) — negate waveform. No params.
17. **Iterative Crossfade** (Mike's, ©1995 Norris, v1.0.2) — takes 2 selected channels, renders `n` iterations crossfading from A to B into a **new channel**; params: Number of Iterations [__], Silence Length [__ units]. New length = `n·(l+s)−s` `[DOC]`. Zero silence on short samples ⇒ quasi-granular. **Recovered by disassembly** `[SOURCE-equivalent — see disassembly/Sample-Hose-Iterative-Crossfade.md]`: a plain **linear crossfade** `out = A·(1−r) + B·r` with `r = pos/len` sweeping 0→1 (A = first selected channel, B = second), computed in double precision then **truncated** (`FTINTX`) and clamped to the sample limits. **Channels that have run out contribute digital zero** — the crossfade zero-pads rather than skipping (the opposite of Select Peak's skip-short-channels rule), so a short sound fades into/out of silence. Shares none of #13/#28's machinery: no RNG, no transcendentals, no tween — and ships **no FPU build**, since add/sub/mul/div suffice.
18. **Keyboard** — not a buffer effect: opens a **piano keyboard window**; click keys to audition the first selected channel at chromatic pitches (playback-rate scaling); ☐ Use selection for looping; **Change Pitch** commits by *changing the document's playback rate only* — data untouched, header lies (pair with Resample Rate to make it real) `[DOC+RES]`. Only plays first selected channel (with an apologetic alert).
19. **Low Pass FIR** (Filters) — `y(n) = [x(n) + x(n−1)]/2` `[DOC]`.
20. **Low Pass IIR** (Filters) — `y(n) = a·x(n) + b·y(n−1)`; Coefficients a, b `[DOC+RES]`.
21. **Noise** — **recovered by disassembly** `[SOURCE-equivalent — see disassembly/Noise.md]`: "makes some noise. That's all there is to it." `[DOC]` **Replaces** (not adds) the selection with the Toolbox `Random()` trap's output, written 16 bits at a time regardless of the sound's bit depth (odd-boundary bytes at each end get a single-byte fixup) — matches the Airplane recipe's replace-into-silence usage. No params. Progress text: "Makin' noise…". Non-cancellable progress.
22. **Normalize** — amplify selection to maximum without clipping. No params. If only some channels of a multichannel sound are selected: alert "You are about to normalize only a part of this sound's channels…" — **Norm All / Just These / Cancel** (Norm All extends selection to all channels, not in time) `[DOC+RES]`.
23. **Quick Mix** (v1.0) — mixes the selected channels down. Params: Mix to • New Channel / • New Document; Use • Mono / • Stereo; ☐ Use each channel's pan `[RES]`. Stereo mode routes by each channel's L/R speaker buttons `[DOC]`.
24. **Replicate** — clones the selection N times with optional gaps `[DOC, rich]`. Params: Number of copies [__]; gaps: • No Gaps / • Gap = selection divided by [__] / • Gap = selection multiplied by [__] / • Gap = [__ units]. Divide-by-zero → friendly alert, uses gap = selection length. Out-of-memory offers "replicate as many times as possible". The built-in help is a manifesto for tape-loop rhythm building — quote it in the UI.
25. **Resample Rate** (Resampling) — **recovered by disassembly** `[SOURCE-equivalent — see disassembly/Resample-Rate.md]`: real sample-rate conversion of the **whole document**, but the module itself implements none of it — `effect()` validates/forces a whole-document selection (with the confirmation alert if only part is selected) and delegates the actual conversion to the host via `ModResampleSoundRate(modData, newRate)`, a `GlueRec` service. The interpolation method lives in the **app binary**, not this module — and is now **recovered by disassembly** `[SOURCE-equivalent — see disassembly/Host-Resamplers.md]`: it is **drop-sample / zero-order-hold (drop or duplicate whole samples), NOT linear interpolation**. The host worker (`CODE 4 @0x42be`) steps a SANE-extended float position by `oldFrameSize·oldRate/newRate` bytes per output sample, **truncates it toward zero** (`FTINTX`), and copies that one source frame — no blend of neighbours; new buffer size = `round(oldSize·newRate/oldRate)`. Dialog text self-references previous rate ("Resample the sound (previously at N Hz) at [__] Hz" + rate-presets popup). Settings default to 44.1 kHz (Fixed 44100.0) before any prefs are ever saved.
26. **Reverse** — reverse the selection ("You'll finally be able to hear those weird satanic messages hidden in your sounds!"). Low-memory path asks to proceed with possibly-incorrect reversal — keep this: *a reverse that may garble under pressure*.
27. **Robotize** — **recovered by disassembly** `[SOURCE-equivalent — see disassembly/Robotize.md]`: a recursive feedback comb, `y[n] = clamp(x[n] + trunc(y[n−D]·75/100))` with `D = trunc(190 · rate / 22254.5454 Hz)` samples — a constant ≈ 8.54 ms delay at any rate, comb fundamental ≈ **117.13 Hz** (the robot's pitch). Integer feedback truncation gives a fast, dead-stop decay; saturating clamp; first D samples are the untouched seed. Progress is **non-cancellable**; selections ≤ 8.5 ms are a silent no-op. No params.
28. **Sample Hose** (Mike's, ©1995 Norris, v1.0.3) — texture generator inspired by Fractal Design Painter's Image Hose `[DOC, complete]`. Sprays copies of the selection (or random "bites" of it) into new channel(s). 5 parameter types × 5 sub-params (Initial Value, Initial Variance, Final Value, Final Variance, Curvature %): **Rate** (% pitch, 50 = octave down, 200 = octave up), **Gain** (%), **Pan** (0 = L, 50 = C, 100 = R; needs ☐ Stereo), **Bite** (subsection length; 0 = whole sample), **Period** (time between copies). Plus New Length [units], ☐ Stereo. Tween formula as in #13. 27 numeric fields in one dialog — reproduce the wall of fields faithfully. **Recovered by disassembly** `[SOURCE-equivalent — see disassembly/Sample-Hose-Iterative-Crossfade.md]`: **Rate uses real LINEAR INTERPOLATION** — `out = a + frac·(b−a)` in double precision, reading two adjacent source samples (`a = src[trunc(pos)]`, `b = src[trunc(pos)+1]`, `frac = pos − trunc(pos)`), then `× gain`. **This is the only interpolating DSP found anywhere in the app** — every Ricci module and the host resampler truncate the fraction away. The Curvature tween is a genuine `pow()` (C library wrapping SANE `Elems68K` `FXPWRY`, with domain-error handling), confirming the #13 formula; the Variance RNG is the same ANSI-C LCG as #13 (statically linked here). Factory defaults (106-byte settings struct): New Length 222000 bytes (≈10 s at 22254.5454 Hz), Rate/Gain pairs 100%, **Pan 50/50 = centre**, **all five Curvatures 100 = linear**, Period 10000/10000, all Variances and Bite 0, Stereo on.
29. **Selection to Channel** — copies the selection out into new channel(s). No params.
30. **Shelving** (Filters) — `y(n) = x(n) + a·x(n−1) + b·y(n−1)`; Coefficients a, b. Its help text ends mid-sentence: "The response is that of a shelving filter, with." — **ship the truncated sentence.** `[DOC]`
31. **Silence** — replace selection with silence, or with an insertion point: "Insert [__ units] of silence." `[DOC+RES]`
32. **Smooth** — **recovered by disassembly** `[SOURCE-equivalent — see disassembly/Smooth.md]`: despite the About text's "averaging every sample with the preceding and the following one," the actual formula is a **2-point** neighbor average that excludes the center sample entirely, `out[i] = (in[i−1]+in[i+1])/2` — the current sample's own value plays no part in its own replacement. The selection's first and last sample are skipped by the loop and left completely unmodified (no edge clamping/padding); a selection needs at least 3 frames for any sample to actually change. Non-cascading (reads happen one frame ahead of the write cursor, so every average uses only original, pre-smoothing neighbor values). No params.
33. **Sum** (Misc) — `ch2' = ch1 + ch2` for two selected channels `[DOC]`. Error string if run without options: "Sorry, man. I haven't got any options."
34. **Upsample Bits** (Resampling) — raise bps (whole doc only); nags you to use Downsample for reductions. The host service (`ModResampleSoundBits`) grows each channel handle and **shifts samples left / zero-pads the new low bytes** — no interpolation of resolution `[SOURCE-equivalent — see disassembly/Host-Resamplers.md]`.
35. **Waveform Generator** — synthesize into the selection: Length [__ units], Frequency [__] Hz, Amplitude [__]%, Wave type • Sine / • Square / • Sawtooth `[RES]`.
36. **Weird Tempo** — **recovered by disassembly** `[SOURCE-equivalent — see disassembly/Weird-Tempo.md]`: a zero-crossing-aligned **period doubler**. Splits the selection at falling zero-crossings (0 counts as positive) and writes each pseudo-period twice in place — no windowing, no interpolation; trailing partial period plays once, so output is *approximately* 2×. Shows the "short hack" warning (with Cancel) on **every run**; ignores the doExtend preference; silence and unipolar material pass through unchanged; channels are segmented independently (stereo smear); cancel mid-run keeps the partial result. No params.
37. **Zero Crosser** (Selection) — non-audio: • Expand / • Shrink the selection to nearest zero-crossings on • Left / • Right / • Both sides `[DOC+RES]`.
38. *(SDK examples: Gain / EmptyEffect — templates, not shipped in Effects Folder.)*

### Factory recipe book (`Effect Combinations`, ship it as content)

- **Airplane-like effect**: 5 s Silence → Noise → Flanger (50%, no feedback, Delay 8 ms, Width 5 ms, Period 6500 ms, sinusoidal, no envelope follower); reapply Flanger for metal; LowPass FIR ×2 to tame highs.
- **Drunk effect**: record speech → Flanger (100%, no feedback, Delay 8 ms, Width 8 ms, Period 500 ms, sinusoidal); variant with Envelope Follower ON, Depth 2.5 ms, Louder = shorter.
- **Pre-echoing**: Reverse → Echo (any settings) → Reverse.

---

## 9. Preferences `[RES: DITL 1001–1005]`

Five pages (popup: General / Window Setup / Recording - Sound Input Device / Recording - Miscellaneous / Effects):

1. **General**: ☐ Open a new window on startup · ☐ Remember utility window positions · ☐ Add .sys7/.AIFF extension to saved files · Default selection units popup (samples/bytes/seconds/ms) · Draw the sound wave using • Dots / • Lines · Start playing from • Beginning / • Insertion Point.
2. **Window Setup**: ☐ Use custom background color [color well] · info-strip toggles: rate+size, channels, file size, selected channel length (samples), (seconds).
3. **Recording – Sound Input Device**: ☐ Automatic Gain Control · Active channels selector · Use: Mono/Stereo · Sampling Rate popup · Sample Size popup · Play-Through Volume (Off,1–7) · Device Settings… · ☐ Beep when clipping occurs.
4. **Recording – Misc**: when recording • Insert the whole recorded sound / • Constrain to the selection · ☐ Record no more than [1000] K (☐ only if virtual memory is active) · VOX parameters (§6).
5. **Effects**: Show progress window after [__] 60ths of a second · Progress bar and background tasks responsiveness • Excellent / • Good / • Fair / • Poor · ☐ Beep after applying effects if more than 1 min elapsed · When a module may affect the sound beyond the end of the selection: • Extend Effect / • Don't Extend / • Ask Each Time.

First launch shows the **Welcome** alert: "This is a preliminary release of SoundEffects. It still lacks some features, like Undo. However, it is fully functional and tested, and a complete version will be ready soon. … (This dialog will not be shown again.)" — keep verbatim; it's the founding document of the no-undo religion.

---

## 10. File formats

- **Open**: System 7 sound files (`sfil` — an `snd ` resource in a resource fork), AIFF, Sound Designer II (opens as untitled copy). Anything else: "I can only open System 7 sound files or AIFF files… You may want to use SoundExtractor to convert…".
- **Save**: System 7 Sound File or AIFF (format popup in the save dialog). Warning when saving >2 channels as sfil: the Finder can't play it.
- Chunk list exports as tab-separated text; registration form saves as TeachText document.
- For the re-creation: treat AIFF as the canonical interchange (it round-trips arbitrary rates; sample sizes 8/16 naturally; nonstandard bps needs care). Web prototype should also import WAV for convenience — but keep the alert for unknown types, reworded only if unavoidable.

---

## 11. App voice — strings to preserve `[RES]`

Keep the first-person error corpus (STR# 128, 66 strings — full dump in `graphify`-able extraction JSON, see §13). Highlights that must survive verbatim:

- "I couldn't find any sounds in the document '…'."
- "There is not enough memory. Try to close unused windows to free up some memory." (the all-purpose lament)
- "I couldn't find my own resources!"
- "Please save your work and quit as soon as possible." (as a *standalone* error)
- "I'm sorry, but I can't delete the only channel of a mono sound. Delete the document '…' instead."
- "I couldn't create my temporary file for Undo buffers in the system disk. You may not be able to Undo."
- Save-changes: "Save changes to the sound '…' before closing/quitting?" — Save / Cancel / Don't Save.
- Shareware nag on quit-without-registering: "Are you sure you don't want to register your copy of SoundEffects? C'mon, it's really cheap!" (adapt content, keep energy).

---

## 12. Web prototype — mapping notes

Goal: a faithful *behavioral* prototype, not a skin. Suggested architecture:

- **Model**: `Document { rateFixed, bps, baseFreq, channels: Channel[] }`, `Channel { bytes: Uint8Array, sizeBytes, outL, outR, vol8_8 }`. Store samples in the **original packed big-endian format** and implement `getSample/setSample` exactly as §2.2 (including the 8-bit XOR 0x80 and integer shifts). All effect math in integers exactly as the originals. This is what makes it sound *right*, aliasing, rounding scars and all.
- **Effects as plugins**: one module = `{ name, group, help, settingsSchema, effect(doc, sel, host, prefs) }` where `settingsSchema` mirrors §7.2's declarative item list (checkbox/radio/int/decimal/unit-dependent + units popup). The schema drives auto-generated dialogs now and encoder-menu pages on hardware later.
- **Host services**: implement the GlueRec surface as a `host` object (progress with pause/stop, doExtend policy, unit conversion, addChannel/newDocument, prefs store in localStorage/IndexedDB with named setups).
- **UI**: draggable in-page windows (document windows, Mixer, Level, Play Clipboard, Progress with working Pause/Stop). Waveform canvas with Dots/Lines modes. Channel icon column with drag-to-add/trash/swap. System-7 look optional but recommended (Chicago-style font, 1-px bevels); behavior first.
- **Audio**: Web Audio only for *playback/monitoring* (convert packed buffers to Float32 at the output edge; per-channel L/R routing and 8-step volume). Recording via getUserMedia with the VOX state machine of §6.
- **Non-negotiables in the prototype**: no undo (⌘Z stays greyed); destructive in place; Stop mid-effect keeps the mangled half; unequal channel lengths everywhere; ask-each-time extension alerts; first-person alerts.

## 12b. Eurorack notes (for later; keep in view now)

- The plugin/schema model maps to firmware: effects as statically-linked modules with the same `effect(doc, sel, host, prefs)` contract; settings schema → OLED + encoder pages; named setups → preset slots.
- Integer-only DSP is *already the spec* — ideal for MCU (no FPU needed for the Ricci modules; Norris modules had FPU builds, still integer-friendly).
- Channels-as-unequal-buffers = per-voice sample slots in RAM; "New Channel as scratch" = a mult/hold buffer. VOX record start/stop with thresholds and stop-delay is directly patchable (gate in). Function-key channel soloing → per-channel gate outs.
- The destructive/no-undo philosophy is the module's identity: one *Commit* knob, no safety. "Weird Tempo" as a panel button.
- Suggested target: STM32H7/Daisy-class (you already have the STM32 toolchain); 64 MB SDRAM ≈ 12 minutes of 16-bit 44.1 k mono across all channel buffers.

---

## 13. Extraction provenance

- Decoded chain: `soundeffects-092.hqx` → BinHex 4.0 → `soundeffects-092.sit` (StuffIt 1.5) → `SoundEffects 0.9.2 ƒ/` with resource forks + 1995–97 timestamps intact.
- Machine-readable dumps produced during this analysis (regenerate with `rsrc_dump.py` against any file's `..namedfork/rsrc`): per-resource JSON for the app and all 38 modules — DITL items with rects/labels/defaults, CNTL, MENU, STR#, ALRT/DLOG/WIND, vers, Grup, TEXT help.
- App signature: creator `√SFx`, type APPL; effect files type `√Eff`; effect code resources `EFCT` (68k), `EFCF` (FPU), `EFCP` (PowerPC).
- **Developer's Kit (`[SOURCE]`, shipped in the archive):** `SoundEffects 0.9.2 ƒ/SoundEffects Developer's Kit/` contains the complete module ABI as C headers (`Interfaces/Glue.h`, `ModGetSetSamples.{h,c}`, `ModSetups.h`, `ModUnits.h`, `ModUnitConversion.{h,c}`, `ModStringConversion`, `ModTypeConversion`, `ModA4Globals.h`) plus full source for two example modules — **`Gain`** (identical to the shipping Amplify effect) and **`EmptyEffect`** (the do-nothing skeleton) — and Resource Templates + THINK C / CodeWarrior project files. §7 is transcribed from this kit; it is the authoritative host⇄module contract, independent of the disassembly.

## 14. Open items requiring 68k disassembly (ranked by value/effort)

1. ~~**Weird Tempo**~~ — **done** (2026-07-14): zero-crossing-aligned period doubler; full reconstruction in `disassembly/Weird-Tempo.md`.
2. ~~**Robotize**~~ — **done** (2026-07-14): 117 Hz feedback comb (0.75 integer feedback, rate-compensated 190-sample delay); full reconstruction in `disassembly/Robotize.md`.
3. ~~**Echo / Delay**~~ — **done** (2026-07-14): same offset-and-scale kernel run in opposite directions — Echo forward (recursive, self-feeding decay train, sized by an amp≤8%/80-echo precompute) vs Delay backward (non-recursive, exactly one echo); full reconstruction in `disassembly/Echo-Delay.md`.
4. ~~**Noise**~~ — **done** (2026-07-14): replaces (not adds) with the Toolbox `Random()` trap, word-at-a-time regardless of bps; full reconstruction in `disassembly/Noise.md`.
5. ~~**Smooth**~~ — **done** (2026-07-14): 2-point neighbor average `(prev+next)/2` — the center sample is excluded from its own average, and the selection's first/last samples are left untouched entirely (not edge-clamped); full reconstruction in `disassembly/Smooth.md`.
6. ~~**Meters/Feet conversion constant**~~ — **done** (2026-07-14): speed of sound = **344 m/s** and **1128 ft/s**, stored as plain 16-bit integers in the app's two SANE unit converters (`CODE 7`); milliseconds = ×1000. Symmetric multiply (bytes→display) / divide (display→bytes). The two speeds are independently rounded (meters/feet disagree ~0.05%). Recovered the full 6-entry unit-code table too. Full reconstruction in `disassembly/Unit-Conversion.md`.
7. ~~**Select Silence threshold**; **Select Peak margin math**~~ — **done** (2026-07-14): Select Silence threshold is exact digital zero (0x80 byte / all-zero frame), insertion-point-only, per-channel scan intersected across channels with a one-frame interior margin at each boundary; Select Peak metric is |sum across selected channels| (first max wins), one-frame selection ± margin bytes clamped at both ends. Bonus: the menu's "(option = repeat last settings)" annotation was wrong — option skips the dialog with margin 0. Full reconstruction in `disassembly/Select-Silence-Peak.md`.
8. ~~**Resample Rate** (+ host resamplers)~~ — **fully done** (2026-07-14): the module delegates to the host's `ModResampleSoundRate` glue service (module reconstruction in `disassembly/Resample-Rate.md`), and the **host service itself is now recovered** (`disassembly/Host-Resamplers.md`): located the runtime `GlueRec` builder (`CODE 8 @0x0012`), followed the slots to the rate worker (`CODE 4 @0x42be`) and bits worker (`CODE 4 @0x3d44`). **Rate conversion = drop-sample / zero-order-hold** (SANE-float position truncated with `FTINTX`, one source frame copied, no interpolation); **bit conversion = plain truncation/zero-extension** (no rounding, no dither). Bespoke code, not the OS SoundConverter. This was the last unrecovered host DSP.
9. ~~**Flanger**~~ — **done** (2026-07-14): no interpolation (delay position truncated toward zero before the single delay-line read); Feedback is a whole-routine sweep-direction flip (backward=non-recursive, forward=recursive, mirroring Echo/Delay); Sinusoidal LFO uses real `sin()`, Triangular/Sawtooth are linear ramp accumulators; Envelope Follower is a 1024-sample block-peak follower with linear interpolation between blocks; all 9 factory `Setp` presets decoded to real ms values; full reconstruction in `disassembly/Flanger.md`.
10. ~~**Spectrum** window parameters; **Mixer** strip semantics~~ — **done** (2026-07-14): **both are unfinished feature shells in 0.9.2** (the app's own Read Me lists Mixer and Spectrum analyzer as *future* improvements). The menu items are enabled and open real modal windows, but: **Spectrum** (WIND 136, kind 0x87) has only an OK button and plots no data — there is no FFT anywhere in the app (zero Elems68K, zero FP68K in its segment); **Mixer** (WIND 132, kind 0x83) is a live shell with one 128-px volume fader per channel + Mix/Cancel + "Mix to: New Document/New Track" popup (MENU 158), but its **Mix action is a stub** (`CODE 9 @0x796`) that performs no mixdown. Also corrected: "Mixer…" has no ⌘M. Full reconstruction in `disassembly/Spectrum-Mixer.md`.
11. ~~Downsample **noise shaping** filter~~ — **done** (2026-07-14): auto-scaled additive white-noise dither, `noiseScale ≈ 2^(oldBits−newBits)` via SANE; the module delegates actual truncation to `ModResampleSoundBits` (host), mirroring Resample Rate. Dither radio's exact code path unresolved (possibly a dead path, see doc). Full reconstruction in `disassembly/Downsample-Bits.md`.
12. ~~Easter-egg trigger dates~~ — **done** (2026-07-14): single startup date check (`CODE 7 @ 0x086c`, one `SecondsToDate` trap in the whole binary) — Christmas = Dec 25 (STR# 138 #1), Alberto Ricci's birthday = **Sep 17** (STR# 138 #2), each shown as NoteAlert 146; no year check, no once-per-day guard. Full reconstruction in `disassembly/Easter-Eggs.md`. Also produced the reusable app-binary tooling (`CODE 0` jump-table resolver) that items 6, 7, 10 need.

Each is a small, named code resource with a known ABI — good afternoon-sized disassembly targets, best verified against the original in an emulator as oracle.

---

## 15. Recreation status

Where each piece stands as of 2026-07-14. The **investigation phase is complete**: every `[ASM]` question in §14 is closed and the module ABI is transcribed from SDK source. What remains is engineering plus a short effect-verification tail — no open research unknowns.

**Confidence legend:** `SOURCE` = from the shipped Developer's Kit C source · `BINARY` = recovered by 68k disassembly (`disassembly/*.md`) · `DOC/RES` = from the manuals + resource-fork dumps, *not* byte-verified · `UNBUILT` = absent or non-functional in 0.9.2.

| Subsystem | Confidence | Notes |
|---|---|---|
| Personality / behavior contract (§1) | Captured | Destructive-in-place, no Undo, first-person voice, keep-the-mangled-half. The identity; fully specified. |
| Core data model / sample format (§2, §7.4) | SOURCE | Packed big-endian, 8-bit `XOR 0x80`, `GetSample8/16` exact (`ModGetSetSamples.c`). |
| Module ABI — host⇄module (§7) | SOURCE | Full `GlueRec`, messages, `ModParamsRec`, result codes, declarative dialog schema; `Gain.c` = Amplify reference. |
| Host DSP services | BINARY | Unit/Meters-Feet conversion (`Unit-Conversion.md`), Select Silence/Peak (`Select-Silence-Peak.md`), **resamplers** (`Host-Resamplers.md`). All recovered. |
| Effect DSP — 13 of 38 | SOURCE/BINARY | Amplify (source) + Echo, Delay, Flanger, Noise, Smooth, Robotize, Weird Tempo, Downsample Bits, Resample Rate, Granular Synthesis, **Sample Hose**, **Iterative Crossfade** (binary). All three Mike's Modules now done. |
| Effect DSP — ~25 of 38 | DOC/RES | Formulas/params from docs + resources; **not byte-verified** (Smooth proved docs can lie — though Granular/Sample Hose proved they can also be exactly right). All remaining ones are simple/low-risk; see build-order step 1. |
| Editing engine (selection, channels, waveform, zoom, chunks, clipboard) | DOC/RES | Behavior known; SPEC thin on waveform render (Dots/Lines + zoom math), the Chunk system, clipboard-as-sound, channel-column drag/trash/swap. |
| Playback & recording (§6) | DOC/RES | Sound Manager-based; VOX record state machine + monitoring meter not specced to the sample. Faithful feel needs a modern audio backend. |
| File formats (§10) | DOC | System 7 Sound resource, AIFF, Sound Designer II import — described, not byte-specced. Parsers needed. |
| UI chrome (menus, DITLs, alerts, windows, CNTLs, icons) | RES | All declarative data dumpable from the resource fork; rendering/behavior unbuilt. §7.5 schema drives the plugin dialogs. |
| Preferences (§9) | RES | Five pages, all items known from DITL 1001–1005. |
| Easter eggs (§1.8) | BINARY | Dec 25 / Sep 17 startup check (`Easter-Eggs.md`). |
| Mixer, Spectrum, Undo | UNBUILT | Mixer/Spectrum are unfinished shells (`Spectrum-Mixer.md`); Undo never worked. Leave broken for fidelity, or fork to finish. |

### Remaining build order

1. **Verify the remaining risky doc-only effects** (short, targeted disassembly — same tooling). ~~Granular Synthesis grain envelope~~, ~~Sample Hose~~, ~~Iterative Crossfade~~ **all done** — the three Mike's Modules are byte-verified (`Granular-Synthesis.md`, `Sample-Hose-Iterative-Crossfade.md`); the docs were accurate in every case, and Sample Hose turned up the app's only linear interpolation. Still worth a look: **Dither** noise application, **IIR filters / Shelving** coefficient handling. Everything else is simple enough to take from the docs.
2. **Core editing engine + sample model** — the foundation: packed-buffer `getSample/setSample` (§2.2/§7.4), unequal-length channels, selection/loop, destructive edits, clipboard.
3. **Effects runtime** — a plugin base class mirroring the `effect()` skeleton (§7.8) and the declarative dialog schema (§7.5); port the 10 confirmed effects, then the rest.
4. **Audio I/O** — playback first, then recording + the VOX state machine (§6), on a modern backend.
5. **File parsers** — System 7 Sound, AIFF, SD II import (§10).
6. **UI shell** — System-7-look windows/menus/alerts driven by the resource dumps; schema-generated effect dialogs.
7. **Leave Mixer/Spectrum/Undo non-functional** (fidelity) — or build them out as an explicit fork.

Steps 2–6 are ordinary engineering against a now-complete spec; step 1 is a short verification pass. Nothing here requires guessing what the original did.
