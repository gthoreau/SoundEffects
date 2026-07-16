/**
 * Browser entry point.
 *
 * §9 page 1 has an "Open a new window on startup" preference, default on, which
 * is why the app comes up with `untitled sound #1` rather than an empty desktop.
 *
 * The Welcome alert of §9 ("This is a preliminary release… It still lacks some
 * features, like Undo") belongs here too — it is the founding document of the
 * no-undo religion. It waits for the Preferences store, since its whole point is
 * that it shows once and then never again.
 */

import "./system7.css";
import { App } from "./app.js";

const root = document.getElementById("sfx-root");
if (!root) throw new Error("I couldn't find my own resources! (#sfx-root)");

const app = new App(root);
app.newDocument();

// Dev-only handle. There is no File > Open yet and no Effects menu to generate a
// tone with, so without this there is no way to get a real waveform in front of
// the renderer and check it. Stripped from the production bundle by the guard.
if (import.meta.env.DEV) {
  (window as unknown as { sfx: App }).sfx = app;
}
