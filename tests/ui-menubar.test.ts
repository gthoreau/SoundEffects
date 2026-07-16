// @vitest-environment jsdom
/**
 * The menu bar's event wiring.
 *
 * This file exists because of a bug that every other kind of test missed. The
 * dismiss-on-click-outside handler treated the pull-down panel as "outside" —
 * the panel is appended to `<body>`, not to the bar — so `mousedown` tore the
 * panel out of the DOM before the item's `click` could fire. Every command in
 * the application was dead, and *nothing showed it*: the resources still parsed,
 * the items still rendered with the right labels and the right greying, and the
 * ⌘-key path (which does not go through the panel at all) still worked. It took
 * driving the real browser to notice.
 *
 * So these tests do not check that menus *render*. They check that clicking a
 * menu item actually runs its command.
 */

import { beforeEach, describe, expect, it, vi } from "vitest";
import { MenuBar, menuTitle } from "../src/ui/menubar.js";
import { Res, getMenu } from "../src/ui/resources.js";

function openMenu(bar: MenuBar, menuID: number): HTMLElement {
  const title = bar.element.querySelector<HTMLElement>(`[data-menu-id="${menuID}"]`)!;
  title.dispatchEvent(new window.MouseEvent("mousedown", { bubbles: true, cancelable: true }));
  return document.querySelector<HTMLElement>(`.sfx-menu[data-menu-id="${menuID}"]`)!;
}

function itemRow(panel: HTMLElement, index: number): HTMLElement {
  return panel.querySelector<HTMLElement>(`.sfx-menu-item[data-item="${index}"]`)!;
}

/**
 * mousedown (which the dismiss handler sees) then click.
 *
 * Note what this does *not* catch: jsdom dispatches the `click` we ask for even
 * if the node has meanwhile been detached, whereas a browser only synthesises a
 * click when mousedown and mouseup land on an element still in the tree. So with
 * the dismiss bug present these helpers still pass — which is exactly how the
 * bug survived. The guard is the explicit "panel survived mousedown" assertion
 * below, not this helper.
 */
function realClick(node: HTMLElement): void {
  node.dispatchEvent(new window.MouseEvent("mousedown", { bubbles: true, cancelable: true }));
  node.dispatchEvent(new window.MouseEvent("click", { bubbles: true, cancelable: true }));
}

describe("clicking a menu item runs its command", () => {
  let onCommand: ReturnType<typeof vi.fn>;
  let bar: MenuBar;

  beforeEach(() => {
    document.body.replaceChildren();
    onCommand = vi.fn();
    bar = new MenuBar({ onCommand, stateFor: () => ({ enabled: true }) });
    document.body.append(bar.element);
  });

  /** The regression. mousedown must not dismiss the panel it landed on. */
  it("mousedown inside the panel does not dismiss it before the click lands", () => {
    const panel = openMenu(bar, Res.menu.file);
    const newItem = itemRow(panel, 1);

    newItem.dispatchEvent(new window.MouseEvent("mousedown", { bubbles: true, cancelable: true }));
    expect(document.querySelector(".sfx-menu"), "panel survived mousedown").not.toBeNull();

    newItem.dispatchEvent(new window.MouseEvent("click", { bubbles: true, cancelable: true }));
    expect(onCommand).toHaveBeenCalledOnce();
    expect(onCommand.mock.calls[0]![0]).toEqual({ menuID: Res.menu.file, item: 1 });
  });

  it("fires the right command for each File item", () => {
    for (const index of [1, 2, 4, 5]) {
      onCommand.mockClear();
      realClick(itemRow(openMenu(bar, Res.menu.file), index));
      expect(onCommand.mock.calls[0]![0], `File item ${index}`).toEqual({
        menuID: Res.menu.file,
        item: index,
      });
    }
  });

  it("closes the menu after a command", () => {
    realClick(itemRow(openMenu(bar, Res.menu.file), 1));
    expect(document.querySelector(".sfx-menu")).toBeNull();
  });

  it("a click on the desktop dismisses without firing anything", () => {
    openMenu(bar, Res.menu.file);
    const desktop = document.createElement("div");
    document.body.append(desktop);
    desktop.dispatchEvent(new window.MouseEvent("mousedown", { bubbles: true, cancelable: true }));

    expect(document.querySelector(".sfx-menu")).toBeNull();
    expect(onCommand).not.toHaveBeenCalled();
  });

  it("a disabled item swallows the click", () => {
    document.body.replaceChildren();
    const cmd = vi.fn();
    const b = new MenuBar({ onCommand: cmd, stateFor: () => ({ enabled: false }) });
    document.body.append(b.element);
    realClick(itemRow(openMenu(b, Res.menu.file), 1));
    expect(cmd).not.toHaveBeenCalled();
  });
});

describe("the menu bar reflects the resources", () => {
  beforeEach(() => {
    document.body.replaceChildren();
  });

  it("renders the eight menus of MBAR 128, with  for the Apple menu", () => {
    const bar = new MenuBar({ onCommand: vi.fn() });
    document.body.append(bar.element);
    const titles = [...bar.element.querySelectorAll(".sfx-menu-title")].map((t) => t.textContent);
    expect(titles).toEqual([
      menuTitle(getMenu(Res.menu.apple)),
      "File", "Edit", "Selection", "Sound", "Effects", "Special", "Windows",
    ]);
    expect(titles[0]).toBe("");
  });

  it("renders separators as separators, not as items titled '-'", () => {
    const bar = new MenuBar({ onCommand: vi.fn(), stateFor: () => ({ enabled: true }) });
    document.body.append(bar.element);
    const panel = openMenu(bar, Res.menu.file);
    expect(panel.querySelectorAll(".sfx-menu-separator").length).toBe(3);
    expect([...panel.querySelectorAll(".sfx-menu-item")].some((r) => r.textContent === "-")).toBe(
      false,
    );
  });

  it("nests a hierarchical item's submenu inside it, not as a sibling panel", () => {
    // The Zoom submenu once rendered as a top-level panel, landing on top of the
    // Edit menu's own Cut/Copy/Paste/Clear and hiding them completely.
    const bar = new MenuBar({ onCommand: vi.fn(), stateFor: () => ({ enabled: true }) });
    document.body.append(bar.element);
    const panel = openMenu(bar, Res.menu.edit);

    const zoomRow = itemRow(panel, 11);
    const sub = zoomRow.querySelector(`.sfx-submenu[data-menu-id="${Res.menu.zoom}"]`);
    expect(sub, "the Zoom submenu is a child of its row").not.toBeNull();
    expect(sub!.querySelectorAll(".sfx-menu-item")).toHaveLength(4);

    // …and the Edit menu's own items are all still present alongside it.
    for (const index of [1, 3, 4, 5, 6]) expect(itemRow(panel, index)).not.toBeNull();
  });

  it("dispatches a submenu item under its own menu id, not its parent's", () => {
    const onCommand = vi.fn();
    const bar = new MenuBar({ onCommand, stateFor: () => ({ enabled: true }) });
    document.body.append(bar.element);
    const panel = openMenu(bar, Res.menu.edit);
    const sub = itemRow(panel, 11).querySelector<HTMLElement>(".sfx-submenu")!;

    realClick(sub.querySelector<HTMLElement>('.sfx-menu-item[data-item="3"]')!);
    // Fit in Window is MENU 141 item 3 — addressed by the submenu, not by Edit.
    expect(onCommand.mock.calls[0]![0]).toEqual({ menuID: Res.menu.zoom, item: 3 });
  });

  it("shows the ⌘ equivalent the resource assigns, and none where it assigns none", () => {
    const bar = new MenuBar({ onCommand: vi.fn(), stateFor: () => ({ enabled: true }) });
    document.body.append(bar.element);
    const panel = openMenu(bar, Res.menu.file);
    expect(itemRow(panel, 1).textContent).toContain("⌘N");
    // Save As… has no shortcut in MENU 129.
    expect(itemRow(panel, 6).textContent).not.toContain("⌘");
  });
});

describe("⌘-keys, and the no-undo contract (§1.2)", () => {
  beforeEach(() => {
    document.body.replaceChildren();
  });

  function press(key: string): KeyboardEvent {
    const e = new window.KeyboardEvent("keydown", {
      key,
      metaKey: true,
      bubbles: true,
      cancelable: true,
    });
    window.dispatchEvent(e);
    return e;
  }

  it("⌘N fires File > New from the resource's own key assignment", () => {
    const onCommand = vi.fn();
    const bar = new MenuBar({ onCommand, stateFor: () => ({ enabled: true }) });
    document.body.append(bar.element);
    press("n");
    expect(onCommand.mock.calls[0]![0]).toEqual({ menuID: Res.menu.file, item: 1 });
  });

  it("⌘-keys reach items inside hierarchical submenus", () => {
    const onCommand = vi.fn();
    const bar = new MenuBar({ onCommand, stateFor: () => ({ enabled: true }) });
    document.body.append(bar.element);
    // ⌘= is Zoom In, and it lives in MENU 141 — reachable only by walking into
    // the submenu when the shortcut index is built.
    press("=");
    expect(onCommand.mock.calls[0]![0]).toEqual({ menuID: Res.menu.zoom, item: 1 });
  });

  /**
   * §1.2. ⌘Z must be *swallowed* even though it does nothing — if it fell
   * through, the browser's own undo would answer for it and the app would appear
   * to have the one feature it must never have.
   */
  it("⌘Z is swallowed and fires nothing", () => {
    const onCommand = vi.fn();
    const bar = new MenuBar({
      onCommand,
      stateFor: (ref) => ({ enabled: !(ref.menuID === Res.menu.edit && ref.item === 1) }),
    });
    document.body.append(bar.element);

    const e = press("z");
    expect(onCommand).not.toHaveBeenCalled();
    expect(e.defaultPrevented, "the browser must never see ⌘Z").toBe(true);
  });

  it("a key with no ⌘ equivalent is left to the page", () => {
    const bar = new MenuBar({ onCommand: vi.fn(), stateFor: () => ({ enabled: true }) });
    document.body.append(bar.element);
    expect(press("j").defaultPrevented).toBe(false);
  });
});
