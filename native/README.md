# SoundEffect — native (JUCE)

The native rebuild of SoundEffects 0.9.2: a **standalone app** and a **DAW plugin**
(VST3 / AU / AAX) from one codebase — the VCV Rack Pro delivery model. Loadable in
a host as either an audio effect or an instrument.

The TypeScript project in the repo root is the **reference spec** (byte-accurate
model, effects, file formats, tests). This `native/` tree ports the engine to C++
and wraps it in JUCE. Where behaviour is subtle, the C++ cites the TS file it came
from.

## Layout

```
native/
  CMakeLists.txt            JUCE via FetchContent → Standalone + VST3 + AU
  Source/
    PluginProcessor.{h,cpp} the ONLY JUCE/host boundary; owns the engine
    PluginEditor.{h,cpp}    hosts the canvas + mixer + toolbar (New/Open/Mosaic)
    ui/                     JUCE UI (message thread); reads engine UI-safe snapshots
      CanvasComponent       the board: hosts Windows, drag-to-move, Mosaic
      WindowComponent       one System-7 sound Window: info strip, track lanes
                            (waveform + L/R indicators), transport wired to engine
      MixerComponent        main mixer: one dB fader + mute strip per Window
    engine/                 host-agnostic core — ZERO JUCE includes
      SampleFormat.h        packed offset-binary / big-endian sample access
      Track.h               one lane: independent buffer + output channel + volume
      SoundDocument.{h,cpp}  the document model; add/move/trash tracks
      Mixer.{h,cpp}         per-frame sum, L/R routing, solo/mute, volume steps
      Transport.{h,cpp}     per-window free-running playhead + loop + Advance(2×)
                            + atomic UI snapshot (uiPlayhead / uiState)
      Import.{h,cpp}        build a document/tracks from decoded float audio
      SpscQueue.h           lock-free single-producer/consumer ring buffer
      Engine.{h,cpp}        multi-window ADDITIVE sum + per-window mixer channel
                            + the lock-free UI↔audio window hand-off
```

## Threading (UI ↔ audio)

The **audio thread** solely owns the *live* Window set. The **message thread**
(UI) never touches it directly — it posts commands (add / remove / transport)
into a lock-free SPSC queue (`SpscQueue`), and reads/writes per-Window mixer
params (`gain`, `muted`) which are atomics. `render()` drains the queue at the
top of each block. Removed Windows are handed BACK to the message thread via a
retirement queue and freed there (`collectRetired()`, driven by a 5 Hz timer in
the processor), so **no allocation or deallocation ever happens on the audio
thread**. All message-thread API must be called from one thread (single
producer); `render()` is the single consumer.

## Vocabulary

Canvas › **Window** › **Tracks** › **Track** › *Audio sample* (with a **Selection
area** and an **Output channel** L/R/L+R) · plus a **Transport** and an
**Audio Output → Mixer channel**. NB the original app and the TS reference call a
lane a "channel"; the rebuild renames that to **Track** and reserves "channel"
for output routing and the mixer.

## The model (what makes it SoundEffect)

- **Independent tracks.** Each track is its own buffer with its own length.
  Nothing pads or aligns them — drifting out of phase is the point.
- **Free-running loops.** No selection → the whole sound loops and tracks phase
  against each other; a selection → only the selected span loops.
- **Global additive playback.** `Engine` sums *every open window* at once. The
  infinite canvas is the spatial view of a bank of unsynchronised loopers.
- **Per-window mixer.** Each `Window` has a `gain` + `mute` — the real "Level"
  fader the original only stubbed out.
- **Destructive edits, declared rate obeyed even when it lies** — carried over
  from the TS core (edits/effects still to be ported).

## Build

Requires CMake ≥ 3.22, a C++17 compiler, and (first configure) network access to
fetch JUCE. On macOS the Command Line Tools are enough (full Xcode NOT required) —
use the Ninja generator, not `-G Xcode`.

```sh
# one-time tools (macOS/Homebrew): brew install cmake ninja
cd native
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target SoundEffect_Standalone   # or omit --target for all formats
open build/SoundEffect_artefacts/Debug/Standalone/SoundEffect.app
```

Artifacts land under `build/SoundEffect_artefacts/` (Standalone `.app`, `.vst3`,
`.component`). Verified building & running on macOS arm64 (AppleClang, CLT-only,
JUCE 8.0.4) 2026-07-18.

## Status

- **Engine core**: done and verified (see the two checks below).
- **UI/UX increment 1** (this pass): canvas hosting draggable System-7 Windows,
  each drawing its tracks' waveforms + a working transport (play/stop/pause/
  advance/loop) wired to the engine; a per-Window main mixer (dB fader + mute);
  File > Open via JUCE decoders; New; Mosaic. **Builds and runs** (Standalone,
  macOS arm64, JUCE 8.0.4) — multiple windows, per-track waveforms, transport, and
  the per-window mixer all render and work. Engine additions it relies on
  (uiPlayhead/uiState atomics, Import, window-id enumeration) are also verified via
  smoke + ThreadSanitizer.
- **UI/UX increment 2**: interactive tracks + selection + clipboard, on an RCU
  editing foundation (the UI mutates a UI-authoritative doc and publishes an
  immutable snapshot to the audio thread — edits apply mid-loop, no glitch, no
  audio-thread alloc). Add/move/trash track, L/R output toggle, selection drag,
  and ⌘X/⌘C/⌘V. Engine layer verified (ASan/UBSan + ThreadSanitizer incl.
  concurrent edits during playback); UI drive-tested (add/trash track, selection,
  cut/paste with the independent-track shortening all confirmed on real audio).
- **Next increments**: canvas pan/zoom; System 7 chrome polish; the effects port;
  recording; byte-accurate AIFF/WAV save.

## Known TODO / not-yet-safe

- **Window lifecycle hand-off: DONE** (lock-free command + retirement queues;
  gain/mute atomics). Verified with ThreadSanitizer (200k-iteration concurrent
  churn, no races) and `leaks` (0 leaks, incl. destructor reclaim).
- **Intra-window edits are NOT yet safe.** The audio thread reads a Window's
  document (track buffers, selection, loop) while playing. Destructive effects,
  selection changes, and add/move/trash *track* while a Window is playing still
  need their own hand-off (same command-queue idea, or a per-Window immutable
  document swap). Editing a stopped Window is fine.
- **`TrackMonitor` uses `std::set`.** Soloing/muting a *track* (not a window)
  mutates a set — allocation. Fine today because it goes through the command
  queue applied on the audio thread only via `play` payloads, but a live
  solo/mute path must avoid `std::set` on the audio thread.
- Plugin format AAX needs the AAX SDK path set; CLAP needs the clap-juce-extension
  wrapper. Only Standalone/VST3/AU are enabled by default.
