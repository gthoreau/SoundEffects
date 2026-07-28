// Module result codes and the error voice — SPEC §7.7, [SOURCE: Glue.h].
// Ported from the TS reference `src/runtime/results.ts`.
//
// The SIGN of the return value tells the host whether the buffer was touched, and
// that is the whole contract for an app with no undo (§1): the host cannot roll
// anything back, so all it can do is tell the truth about what happened.
//
//   0        success, data modified
//   positive error, data left UNTOUCHED
//   negative error, but data WAS changed ("Tell" — the user's sound is already
//            half-mangled and they need to know)
//   -128     the sentinel: negative, but means NOTHING happened

#pragma once

#include <cstdlib>
#include <string>
#include <vector>

namespace se {

using ModResult = int32_t;

/** noErr — success, data modified. */
inline constexpr ModResult kModNoError = 0;

/**
 * userCanceledErr. "Return this when the module didn't do anything."
 *
 * A special sentinel, outside the sign rule: negative, but it means nothing
 * happened. The Gain.c skeleton initialises `result = kModCancel` and only
 * promotes it to kModNoError once a track actually had samples in range — which
 * is how a selection that misses every track reports honestly.
 */
inline constexpr ModResult kModCancel = -128;

// Positive = error, data left UNTOUCHED.
inline constexpr ModResult kModGenericError = 1;
inline constexpr ModResult kModNotEnoughMemory = 2;
inline constexpr ModResult kModCouldntLoadMyRes = 23;
inline constexpr ModResult kModUnknownMessage = 30;

// Negative = error, but data WAS changed.
inline constexpr ModResult kTellGenericError = -1;
inline constexpr ModResult kTellNotEnoughMemory = -2;
inline constexpr ModResult kTellCouldntLoadMyRes = -23;

/** Returned by `settings` — "Sorry, man. I haven't got any options." */
inline constexpr ModResult kModHasNoSettings = 1;

inline bool isError (ModResult r) noexcept
{
    return r != kModNoError && r != kModCancel;
}

/** Did the module leave the audio untouched? */
inline bool dataUntouched (ModResult r) noexcept
{
    if (r == kModNoError) return false;
    if (r == kModCancel) return true;
    return r > 0;
}

/** Did this run change audio? Drives whether the host must warn on error. */
inline bool dataWasChanged (ModResult r) noexcept
{
    if (r == kModNoError) return true;
    if (r == kModCancel) return false;
    return r < 0;
}

/**
 * "If the returned code's ABSOLUTE VALUE indexes a string in the module's own
 * STR# 1000, the host shows that string — how modules speak in their own voice."
 * (§7.7)
 *
 * STR# indices are 1-based. Empty when the module has nothing to say, and the
 * host falls back to its own wording.
 */
inline std::string errorVoice (ModResult r, const std::vector<std::string>& strings)
{
    if (! isError (r)) return {};
    const size_t idx = static_cast<size_t> (std::abs (r)) - 1;
    return idx < strings.size() ? strings[idx] : std::string{};
}

} // namespace se
