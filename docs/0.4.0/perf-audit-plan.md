# SoundEffects 0.4.0: performance and robustness audit

Read-only audit of `native/` on `v0.4.0-dev` at `fb94b21` (2026-10-31). Nothing was
changed in the code. Every item below is sized to be **one commit** and follows the
usual rule: `native/tests` green, standalone builds, TSan on engine changes.

Sizes: **S** < 50 lines, **M** 50–200, **L** more than that, or touches the golden vectors.

## What was measured

| Measure | Result |
|---|---|
| Unit tests (`engine-tests`, `state-tests`, `effect-fixtures`) | all green (209 fixtures) |
| `engine-tests` under ThreadSanitizer (scratch build) | clean |
| Standalone, author's canvas (~10 Sounds), idle | 3.7–4 % CPU, 87 MB footprint (peak 121 MB) |
| `leaks` on the idle standalone | 288 leaks / 14 KB, all in system XPC (`LNDaemonApplicationInterface`), **none in our code** |
| Engine render bench (scratch, `-O3`, 48 kHz, 512-frame blocks, every Window looping) | 1 Window × 1 track: 13 µs/block · 8 × 4: 183 µs (1.7 % of a core) · 16 × 8: 610 µs (5.7 %) · 32 × 16: 2.36 ms (22 %, worst 3.1 ms). About **9 ns per track-frame**. |
| Same bench, `std::pow` and the empty `std::set` lookups taken out of `mixFrameAt` | only −5 %: the cost is the per-sample `floor` + 64-bit modulo in `readFor` |
| Full waveform-peak rebuild (`ensurePeaks`, stereo 44.1 kHz/16, 500 px) | 0.5 min 4.6 ms · 2 min 24 ms · 5 min 41 ms · **10 min 66 ms** |
| Compiler warnings, Release, our sources (`-fsyntax-only` over `compile_commands.json`) | 14 distinct (see H1) |
| Syntax-only time per UI translation unit | 2.7–3.3 s each, mostly JUCE headers |

**Not measured:** CPU while playing in the running app. Background clicks don't reach
JUCE components, and taking the screen from you just for that didn't seem worth it.
Item G1 below says what to look at with Instruments (Time Profiler) during playback.
The settings files were backed up and restored byte-identical (sha1 checked).

## Verdict

The audio thread is in good shape. The SPSC/RCU design holds, no locks or logging run on
the audio thread, `ScopedNoDenormals` is set and TSan is clean. The real costs are on
the **message thread**:

1. Waveform peaks are rebuilt from every sample. This happens 30 times a second while recording, and on every zoom or pan step.
2. Every commit deep-copies the whole Sound, even when only the selection changed.
3. A playing Window repaints all of itself, transport buttons included, 30 times a second.

---

## Before the 0.4.0 release

Ordered by value over risk. P1 to P3 are user-visible. P4 to P6 are hygiene, safe to slip.

### P1. Waveform peaks from a summary cache, not from every sample (M)
- **Where:** `ui/WindowComponent.cpp:383-436` (`ensurePeaks`), triggered by `PluginEditor.cpp:101-104` (`recorder().onChanged` → `invalidateWaveform()` every 30 Hz tick).
- **What:** any change of view (zoom, pan, width) or of any track's length throws away every column and re-reads every sample in view through `getSample`. While recording into new tracks the length changes every tick, so the whole take is re-read 30×/s.
- **Cost:** measured above: 66 ms per rebuild for a 10-minute stereo take, i.e. ~2× the message thread's budget at 30 Hz. From about 3 minutes of recording on, the UI stutters (playheads, meters, menus), and the Recorder's drain shares that thread. The same rebuild runs on every zoom-bar drag step on a long Sound.
- **Fix:** keep a per-track min/max summary per block of 256 frames (a 1/256 copy of the data, in `WindowComponent`), extended incrementally when a track only grew (the recording case), rebuilt for a track when its content changed (commit). Columns wider than a block read the summary, and narrower ones read samples as today. Optionally add a second level (×64) for very long Sounds.
- **Risk:** low. Pure UI. Compare a few Windows against `--app-snapshot` before/after: they must be pixel-identical when zoomed out.

### P2. Repaint only what moves while playing (S)
- **Where:** `ui/WindowComponent.cpp:123-148` (`refresh()` ends in a full `repaint()`).
- **What:** every tick a playing Window repaints its whole bounds. JUCE then repaints every child inside them (8 transport buttons, 3 text fields, the unit dropdown, the zoom bar, all built from path-clipped `innerShadow` bevels), plus the canvas underneath, because no component is opaque.
- **Cost:** several Windows playing = several full Window paints at 30 Hz. This is the likely bulk of the playing-time CPU (to confirm with G1).
- **Fix:** repaint `tracksArea()` (and the navigator block) instead of the Window. Better: the union of the old and new playhead columns per track, plus the loop block if `uiPlaySt/End` moved. Mute/solo changes keep the full repaint.
- **Risk:** low. Watch the playhead over the selection (12.4) and the recording states.

### P3. Selection, loop and routing changes without cloning the samples (M)
- **Where:** `ui/WindowComponent.h:173` (`commit()`), `engine/Engine.cpp:182-191` (`commitEdit` → `SoundDocument::clone()` at `engine/SoundDocument.cpp:97`, a deep copy of every track).
- **What:** a selection mouse-up (`WindowComponent.cpp:1315`, `:1372`), a speaker toggle (`:1141`), Set Loop, the Extend commands and Select Loop all clone the whole Sound to publish a few integers.
- **Cost:** a 10-minute stereo 16-bit Sound is 106 MB: each click allocates and copies 106 MB on the message thread (measured after the fix: 3.8 ms, not the "tens of ms" first guessed; the copy is fast, the 106 MB are the cost) and briefly holds **three** copies (ui, audio, the outgoing one until `collectRetired`).
- **Fix:** a `SetMeta` engine command carrying `selSt/selEnd/firstSelTrack/lastSelTrack/loopSt/loopEnd` and the per-track `outLeft/outRight/volume` (bitmasks plus a small fixed array). It is applied in place to `audioDoc` in `drainCommands()` (the audio thread owns it, so no allocation is needed), and `commit()` uses it when the sample data didn't change. Edits that change samples keep `commitEdit`.
- **Risk:** medium-low. It is an engine change: add an engine test (play-loop re-aims after a SetMeta exactly as after a SwapDoc) and run TSan.

### P4. Compiler warnings to zero (S)
- **H1, the 14 distinct warnings:** `PluginEditor.cpp:727` shadow · `engine/Edit.cpp:163,167` float-equal (an exact rate compare is intended there: say so with a small `sameRate()` helper rather than an epsilon) · `engine/Engine.cpp:128` sign-conversion · `engine/Project.cpp:68` sign-conversion · `engine/Wav.cpp:17,18` sign-conversion · `ui/EffectsPanel.cpp:1221` captured structured bindings (C++20 ext; copy to locals) · `ui/EffectsPanel.cpp:417` unused `this` capture · `ui/EffectsPanel.cpp:814` float-equal · `ui/EffectsPanel.cpp:920` unused `doc` · `ui/Os8Gallery.cpp:228` shadow · `ui/WindowComponent.cpp:683` shadows a field.
- **Risk:** none. One commit.

### P5. Remove the audio-thread allocation trap in `Transport::play` (S)
- **Where:** `engine/Transport.cpp:223`, `engine/Mixer.h:29-33`.
- **What:** `monitor.soloed.clear(); monitor.soloed.insert(...)` is a `std::set` (heap) mutation inside `play()`, which runs on the audio thread. It is dead today: `PlayOptions::soloTrack` is never set (the Keyboard deliberately drops it, `KeyboardPalette.cpp:221`). But it is the one allocation path on the audio thread, and `mixFrameAt` pays two `std::set` lookups per track per frame for it.
- **Fix:** turn `TrackMonitor` into two `uint64_t` masks (the same as `excludeMask`), or delete it with `soloTrack`. The same commit can drop the other never-written knobs: `Transport::masterVolume`, `supportsStereo`, `playFrom` / `PlayFrom::insertionPoint` (grep finds no writer). If any of these is a planned feature, keep it as a mask or a constant.
- **Risk:** low. Engine tests plus golden fixtures.

### P6. Delete Sys7 (S)
- **Where:** `ui/Sys7.{h,cpp}` (357 lines). `Sys7Button`, `Sys7AlertDialog` and `Sys7LookAndFeel` have no users. Only `sys7::tintFromPalette` / `kNumTints` are still reached, through `CanvasComponent.h:35,60` and `CanvasComponent.cpp:28`, and both forward to `os8::themeColour` / `os8::kNumThemes`. The comment at `ViewPalette.h:75` is stale.
- **Fix:** point `CanvasComponent` at `os8::` directly, remove the files from `native/CMakeLists.txt:83` and fix the comments (`Os8.h:5`). This closes the Step 1 note "then Sys7 is deleted".
- **Risk:** none.

---

## After 0.4.0

### Audio engine

**E1. Block-wise per-track rendering (L).** `engine/Mixer.cpp:133-173` runs `trackRead` → `readFor` (a `floor`, an int64 division and a modulo) for every track at every frame, twice during a crossfade. Bench: about 9 ns per track-frame, i.e. 22 % of a core for 32 Windows × 16 tracks. Fix: per track and per block, compute the read position once, then step it with a counter and wrap only at span edges (the phase math stays as it is). Hoist the bit-depth divisor out of the frame loop (`Mixer.cpp:22-26`, a `std::pow` per frame). Expected gain is 3–5×. Risk: medium, because the golden vectors and the 12.8 deselect cases must stay identical. Do it with the bench from this audit as a test.

**E2. A block bigger than `prepare()`'s is truncated (S).** `engine/Engine.cpp:373`: `cap_ = min(frames, scratch size)`. Each transport renders and advances only `cap_` frames, the rest of the output stays silent, and the inputs are dropped (`PluginProcessor.cpp:131,140`). The standalone never sends larger blocks, but some hosts do (offline bounce). Fix: loop `render` in chunks of the scratch size. It matters once the plugin build is unfrozen.

**E3. `Engine::prepare` walks the message-thread registry (S).** `engine/Engine.cpp:38-39`. In a plugin, `prepareToPlay` can come from a non-message thread and race `addWindow/removeWindow`. Fix: store the device rate in an atomic and pass it to `Transport::render` (or post a `SetRate` command). Also keep transports created before `prepare` consistent.

**E4. A device rate change during a take (S).** `engine/Recorder.cpp:79`: the converter is built at the start rate, so a mid-take rate change records the rest at the wrong pitch. Fix: stop (or settle) the take when `prepare` sees a new rate.

**E5. Ignored retire-queue pushes (S).** `engine/Engine.cpp:320,329,339,343`: `retiredWins/retiredDocs.push` results are ignored, so a full queue leaks silently. It is unreachable in practice (1023 deep, drained at 30 Hz), but count it, like the capture ring's `dropped`, so a test can assert it.

**E6. Copy-on-write sample buffers (L).** It finishes what P3 starts. Share each track's bytes through an immutable shared buffer, so a commit copies headers rather than samples, and a Sound costs about 1× its size instead of 2–3×. The audio thread must never drop the last reference, so retire buffers through the existing queue. This touches `SoundDocument`, `Edit.cpp` and the effects host.

### CPU and graphics (message thread)

**G1. Profile playback once (S, measurement).** Run Instruments' Time Profiler on the standalone with 3–4 looping Windows, before and after P2, to confirm where the remaining paint time goes (`innerShadow` path clips, `GlyphArrangement` text, VU sliders).

**G1 result (13.7, 2026-11-01).** Done: the remaining paint time while playing was `paintStatus` re-shaping text after a sub-pixel pan, now fixed. A per-lane waveform image cache was tried and dropped: since P2 the clipped wave stroke is ~15 % of paint time and the cache measured no gain. Worth revisiting only if full-Window repaints (hover, selection drags) show up in a profile.

**G2. Text width and layout cache (S).** `ui/Os8.cpp:65` (`widthOf`) and `:70` (`drawText`) shape the string with a fresh `GlyphArrangement` on every call and every paint. The dropdown's ellipsis loop (`Os8.cpp:507-511`) re-measures once per dropped character (O(n²)). Fix: a small `(style, string) → width` cache, plus a binary search for the ellipsis.

**G3. Opaque components (S).** No `setOpaque` anywhere: any repaint walks up to the canvas and the editor beneath it. Mark the opaque surfaces as opaque (the canvas background, side panels, Window body if its corners allow). It is cheap and composes with P2.

**G4. Mixer strip ticks (S).** `ui/MixerComponent.cpp:212` builds a `juce::String` from `uiDoc->name` per strip per tick just to compare (compare the `std::string`). `Os8Slider::setLevels` (`Os8Widgets.cpp:526`) repaints the whole fader for a meter move (repaint only the VU rectangle).

**G5. Display-synced animation (S).** The playhead (30 Hz editor timer, `PluginEditor.cpp:131`) and `reorder::Slider` (60 Hz `juce::Timer`, `ui/Reorder.h:136`) are not synced to the display. `juce::VBlankAttachment` would give smooth motion at the screen's rate (ProMotion included) and nothing when idle. In grid reorders the sliding neighbours are not buffered (only the lifted one is, `Reorder.h:176`): buffer them while they slide.

**G6. Effects off the message thread (M/L).** `EffectRunner` applies effects synchronously, so a long Granular or Flanger on a long Sound freezes the UI. Run on a worker against a clone, show progress, commit on completion. This interacts with E6 (a clone is cheap once buffers are shared).

### Memory and persistence

**M1. The canvas is only saved at quit (M).** `StandaloneApp.cpp:719-725`: the whole canvas, audio included (an 8.9 MB XML properties file for your current canvas), is written only on quit. A crash loses everything since launch, which may relate to the open "canvas came back empty" report. Fix: autosave in the background after edits settle (serialise on the message thread, write the file on a worker, atomically). Longer term, keep audio out of the properties XML (base64) and in the project folder.

**M2. Opening a large file (S/M).** `PluginEditor.cpp:676-707`: the whole file goes into a float `AudioBuffer`, is copied into per-channel vectors, then converted, then cloned twice by `addWindow` (`Engine.cpp:52`), so the peak is about 5× the file's size. Read and convert in chunks straight into the track bytes.

### Hygiene and build

**H2. One bit-depth divisor (S).** `2^(bits-1)` is computed in `engine/Mixer.cpp:22`, `engine/Import.cpp:25` and `ui/WindowComponent.cpp:404`. Move it into `SampleFormat.h` as a constexpr table.

**H3. The same add/remove-reconcile loop twice (S).** `CanvasComponent::refresh` (`:123-160`) and `MixerComponent::refresh` (`:492-525`) reconcile children against `engine.windowIds()` with the same quadratic find. A shared helper (or an engine "generation" counter that skips the pass when nothing changed) avoids running it 30× a second.

**H4. Precompiled JUCE headers (S, build time).** Each UI file spends about 3 s just parsing JUCE. `target_precompile_headers` on the shared-code target should cut clean-build and touch-a-header rebuilds substantially. Measure before and after with `ninja -t clean && time ninja`.

**H5. Oversized files (L, opportunistic).** `ui/EffectsPanel.cpp` (1641 lines) and `ui/WindowComponent.cpp` (1642 lines) mix layout, painting and behaviour. Split them when next touched (e.g. `WindowPaint.cpp`, `EffectTrays.cpp`), not as a standalone refactor.

---

## Proposed order

1. **P4** warnings (warm-up, zero risk)
2. **P6** delete Sys7
3. **P5** TrackMonitor masks / dead transport knobs
4. **P2** playing repaint region
5. **P1** peak summary cache
6. **P3** SetMeta commit
7. then 11.1 / 11.2 (release)

After the release, by value: **M1** autosave → **G1** profile → **E1** block render → **E6** COW buffers (with **G6**) → **E2/E3/E4** before the plugin is unfrozen → the rest as opportunities.
