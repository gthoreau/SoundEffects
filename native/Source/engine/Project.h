// Project — a canvas (or one Sound) as files on disk. JUCE-free, so the round trip
// is tested through a real folder.
//
//   <Name>/                  the project folder: visible in the Finder
//     <Name>.sefx            the canvas: Sounds, placement, mixer, settings (v3)
//     Assets/
//       01 <Sound> - track 1.wav   one mono WAV per Track, byte-exact
//
// The .sefx names its assets by path relative to itself and keeps the exact
// (non-integer) rate, the true bit depth and each Track's exact byte length, so the
// WAVs are plain stems anyone can open and nothing is lost by saving.
//
// An EXPORTED Sound is not a project: one self-contained .sefx with the audio
// embedded (v3, audioMode 0). Anything that reads a .sefx (readSefx) takes every
// kind: v1/v2 single files, exports, projects.

#pragma once

#include "EngineState.h"

#include <filesystem>
#include <string>

namespace se {

/** Write `<dir>/<name>.sefx` and `<dir>/Assets/` WAVs for `state` (all audio taken
    from the Tracks). Creates `dir`. Assets the previous save of this project
    referenced and this one does not are removed; nothing else is touched. The .sefx
    is written last and atomically, so a failure never leaves it pointing at assets
    that are not there. `err` says what went wrong. */
bool writeProject (const std::filesystem::path& dir, const std::string& name,
                   const EngineState& state, std::string& err);

/** One Sound as a self-contained .sefx (embedded audio). Its mixer slot, solo and
    canvas placement are dropped: the importer places it. */
bool writeSoundExport (const std::filesystem::path& file, const WindowState& sound,
                       std::string& err);

/** Read any .sefx. A project's assets are loaded from beside the file. */
bool readSefx (const std::filesystem::path& file, EngineState& out, std::string& err);

/** True iff `file` is a v3 .sefx whose audio lives in Assets/ — i.e. a project that
    Save may rewrite in place. False for older files, exports, and unreadable ones. */
bool isProjectFile (const std::filesystem::path& file);

/** A Sound's name made safe to be part of a file name. */
std::string safeFileName (const std::string& s);

} // namespace se
