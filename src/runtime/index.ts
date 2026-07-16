/**
 * The effects runtime — SPEC §7, build-order step 3.
 *
 * The host⇄module contract, shaped so the same effect code can run in a browser
 * now and on a Daisy later (§12b): messages and dispatch (`module.ts`), the
 * `GlueRec` host surface (`host.ts`), the declarative dialog schema
 * (`schema.ts`), the shared `effect()` skeleton (`skeleton.ts`), the host
 * resamplers (`resample.ts`), and the two distinct RNGs (`rng.ts`).
 */

export * from "./results.js";
export * from "./schema.js";
export * from "./rng.js";
export * from "./resample.js";
export * from "./host.js";
export * from "./module.js";
export * from "./skeleton.js";
