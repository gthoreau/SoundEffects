/**
 * Audio I/O — SPEC §6, build-order step 5.
 *
 * Playback (`transport.ts`, `mixer.ts`), recording (`recorder.ts`, `vox.ts`), and
 * Keyboard's auditioning half (`keyboard-voice.ts`), all behind a backend
 * boundary (`backend.ts`) so the same engine runs on Web Audio, in a test, or on
 * a Daisy.
 *
 * The fidelity-vs-modernity split is argued in full at the top of `backend.ts`.
 * The short version: the **mechanism** is modern (a pull-based render callback,
 * not Sound Manager traps), the **behaviour** is not (routing, volume steps, loop
 * semantics, VOX, insertion policy, and above all the rule that the declared rate
 * is obeyed even when it is a lie).
 */

export * from "./backend.js";
export * from "./mixer.js";
export * from "./transport.js";
export * from "./vox.js";
export * from "./recorder.js";
export * from "./keyboard-voice.js";
