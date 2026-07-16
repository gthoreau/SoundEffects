/**
 * The UI shell's public surface — SPEC §15 step 7.
 *
 * Kept out of `src/index.ts` on purpose. That entry point is the **headless**
 * core, and steps 2–6 earned that: the model, the effects and the DSP have no
 * idea a screen exists, which is what lets §12b's firmware target link the same
 * code. Everything under `src/ui/` needs a DOM, so importing it from the core
 * would quietly make the core un-embeddable. Import this module instead.
 */

export * from "./resources.js";
export * from "./strings.js";
export * from "./window.js";
export * from "./menubar.js";
export * from "./waveform.js";
export * from "./info-strip.js";
export * from "./transport-strip.js";
export * from "./document-window.js";
export * from "./alert.js";
export * from "./app.js";
