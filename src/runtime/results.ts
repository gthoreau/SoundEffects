/**
 * Module result codes and the error voice — SPEC §7.7, `[SOURCE: Glue.h]`.
 *
 * The **sign** of the return value tells the host whether the buffer was
 * touched, which is the whole contract for an app with no undo: the host cannot
 * roll anything back, so all it can do is tell the truth about what happened.
 */

/** noErr — success, data modified. */
export const kModNoError = 0;

/**
 * userCanceledErr. "Return this when the module didn't do anything."
 *
 * A special sentinel, not part of the sign rule below: it is negative but means
 * *nothing happened*. Gain.c initialises `result = kModCancel` and only promotes
 * it to `kModNoError` once a channel actually had samples in range.
 */
export const kModCancel = -128;

// Positive = error, data left UNTOUCHED.
export const kModGenericError = 1;
export const kModNotEnoughMemory = 2;
export const kModCouldntLoadMyRes = 23;
export const kModUnknownMessage = 30;

// Negative = error, but data WAS changed. The "Tell" prefix is the point: the
// host must tell the user their sound is already half-mangled.
export const kTellGenericError = -1;
export const kTellNotEnoughMemory = -2;
export const kTellCouldntLoadMyRes = -23;

/** Returned by `settings` — "Sorry, man. I haven't got any options." */
export const kModHasNoSettings = 1;

export type ModResult = number;

/** Did the module leave the audio untouched? */
export function dataUntouched(result: ModResult): boolean {
  return result === kModNoError ? false : result === kModCancel ? true : result > 0;
}

/** Did this run change audio? Drives whether the host must warn on error. */
export function dataWasChanged(result: ModResult): boolean {
  if (result === kModNoError) return true;
  if (result === kModCancel) return false;
  return result < 0;
}

export function isError(result: ModResult): boolean {
  return result !== kModNoError && result !== kModCancel;
}

/**
 * "If the returned code's **absolute value** indexes a string in the module's own
 * `STR# 1000`, the host shows that string — how modules speak in their own
 * voice." (§7.7)
 *
 * STR# indices are 1-based, so `strings[abs(result) - 1]`. Returns null when the
 * module has nothing to say, and the host falls back to its own wording.
 */
export function errorVoice(result: ModResult, strings: readonly string[] = []): string | null {
  if (!isError(result)) return null;
  const idx = Math.abs(result) - 1;
  return strings[idx] ?? null;
}
