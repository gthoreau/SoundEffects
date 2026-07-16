/**
 * Alerts — SPEC §11, §12 ("first-person alerts" is a non-negotiable).
 *
 * The text is never composed here. Callers pass a string that came out of
 * STR# 128 via `strings.ts`, and this module only draws the box. That split is
 * deliberate: the moment an alert takes a template literal, someone writes
 * "Error: could not open file" and the app stops being itself.
 *
 * Modal in the System 7 sense — the whole application waits. A promise is the
 * honest modern shape for that, since the browser will not block.
 */

import { el } from "./dom.js";
import { lines } from "./strings.js";

export interface AlertButton {
  text: string;
  /** The Return key's button, drawn with the default ring. */
  isDefault?: boolean;
  /** Escape and ⌘. map here. */
  isCancel?: boolean;
}

export interface AlertOptions {
  /** Already resolved from the fork and filled — see `strings.ts`. */
  message: string;
  buttons?: AlertButton[];
  /** The stop/note/caution icon. `stop` is the default an error gets. */
  icon?: "stop" | "note" | "caution";
}

const ICONS: Record<string, string> = {
  stop: "🛑",
  note: "💬",
  caution: "⚠️",
};

/** Resolves to the index of the button pressed. */
export function showAlert(opts: AlertOptions): Promise<number> {
  const buttons = opts.buttons ?? [{ text: "OK", isDefault: true }];

  return new Promise((resolve) => {
    const layer = el("div", { class: "sfx-modal-layer" });
    const box = el("div", { class: "sfx-alert" });

    const body = el("div", { class: "sfx-alert-body" });
    body.append(el("div", { class: "sfx-alert-icon", text: ICONS[opts.icon ?? "stop"] ?? "" }));

    // The corpus breaks lines with \r, and those breaks are authored — "There is
    // not enough memory." and the advice that follows are two paragraphs.
    const text = el("div", { class: "sfx-alert-text" });
    for (const line of lines(opts.message)) text.append(el("div", { text: line }));
    body.append(text);
    box.append(body);

    const row = el("div", { class: "sfx-alert-buttons" });
    const finish = (i: number) => {
      window.removeEventListener("keydown", onKey, true);
      layer.remove();
      resolve(i);
    };

    // System 7 order: the default button sits rightmost.
    buttons.forEach((b, i) => {
      const node = el("button", {
        class: "sfx-button",
        text: b.text,
        "data-default": b.isDefault ? "true" : null,
      });
      node.addEventListener("click", () => finish(i));
      row.append(node);
    });
    box.append(row);
    layer.append(box);
    document.body.append(layer);

    const onKey = (e: KeyboardEvent) => {
      if (e.key === "Enter") {
        const i = buttons.findIndex((b) => b.isDefault);
        if (i >= 0) {
          e.preventDefault();
          finish(i);
        }
      } else if (e.key === "Escape") {
        const i = buttons.findIndex((b) => b.isCancel);
        if (i >= 0) {
          e.preventDefault();
          finish(i);
        }
      }
      // Every other key is swallowed: the app is modal.
      e.stopPropagation();
    };
    window.addEventListener("keydown", onKey, true);

    (box.querySelector("[data-default]") as HTMLElement | null)?.focus();
  });
}

/**
 * The Save-changes alert of §11 — Save / Cancel / Don't Save, in that order.
 * STR# 130 holds the two words it interpolates: "closing" and "quitting".
 */
export const SaveChangesButtons: AlertButton[] = [
  { text: "Save", isDefault: true },
  { text: "Cancel", isCancel: true },
  { text: "Don't Save" },
];
