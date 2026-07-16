/**
 * The resource dumps, checked against what the SPEC independently claims.
 *
 * These tests are worth more than they look. `tools/uidump.py` is a hand-written
 * parser for six binary formats, and a parser that is subtly wrong produces
 * *plausible* rects rather than a crash — the step-6 lesson (a ratio a hair under
 * 2 is a different algorithm, not a rounding error) applies to resource parsing
 * exactly as it did to resamplers. §3 and §4 were transcribed from these
 * resources by a human before this parser existed, so they are a genuinely
 * independent reading. Where the two agree, the parse is right. Where they
 * disagree, that is a finding, and the disagreements are pinned here too.
 */

import { describe, expect, it } from "vitest";
import {
  ALRT,
  CNTL,
  DITL,
  MENU,
  Res,
  STR_LIST,
  WIND,
  commandKey,
  getCntl,
  getIndString,
  getMbar,
  getMenu,
  getWind,
  hierarchicalSubmenuID,
  rectHeight,
  rectWidth,
} from "../src/ui/resources.js";
import { chromeFor, WindowProc } from "../src/ui/window.js";
import { EditVerb, editMenuLabel, errorString, fill, lines } from "../src/ui/strings.js";
import { menuTitle } from "../src/ui/menubar.js";

describe("WIND templates match SPEC §3", () => {
  // §3: "initial frame 454×252 px at (20, 50)".
  it("the document window is 454×252 at (20, 50)", () => {
    const w = getWind(Res.wind.document);
    expect(rectWidth(w.rect)).toBe(454);
    expect(rectHeight(w.rect)).toBe(252);
    expect(w.rect.left).toBe(20);
    expect(w.rect.top).toBe(50);
  });

  // §3's "Other windows" table, verbatim.
  it.each([
    ["level", Res.wind.level, 79, 168],
    ["record", Res.wind.record, 288, 186],
    ["mixer", Res.wind.mixer, 480, 270],
    ["spectrum", Res.wind.spectrum, 480, 270],
    ["chunkList", Res.wind.chunkList, 170, 100],
    ["playClipboard", Res.wind.playClipboard, 220, 61],
    ["progress", Res.wind.progress, 328, 58],
  ])("%s is %dx%d", (_name, id, w, h) => {
    const t = getWind(id);
    expect([rectWidth(t.rect), rectHeight(t.rect)]).toEqual([w, h]);
  });

  it("every WIND template parses to a non-empty, positive rect", () => {
    for (const [id, t] of Object.entries(WIND)) {
      expect(rectWidth(t.rect), `WIND ${id} width`).toBeGreaterThan(0);
      expect(rectHeight(t.rect), `WIND ${id} height`).toBeGreaterThan(0);
    }
  });

  /**
   * The conflict of `DOCUMENT_WINDOW_IS_ZOOM_NO_GROW`. §3 calls the document
   * window "zoomable/resizable" on the 0.9.1 Read Me's authority; the 0.9.2
   * resource says `zoomNoGrow` — zoom box, no grow box. Pinned so that if a
   * re-extraction ever says otherwise, this fails loudly rather than quietly
   * changing what the shell believes.
   */
  it("the document window is zoomNoGrow, contradicting §3's 'resizable'", () => {
    const w = getWind(Res.wind.document);
    expect(w.procID).toBe(WindowProc.zoomNoGrow);
    expect(chromeFor(w.procID)).toEqual({ titled: true, zoomBox: true, growBox: false });
  });

  // §3 explicitly grants the Chunk List a grow box, and the resource agrees.
  it("the Chunk List has a grow box, per §3", () => {
    const w = getWind(Res.wind.chunkList);
    expect(w.procID).toBe(WindowProc.zoomDocProc);
    expect(chromeFor(w.procID).growBox).toBe(true);
  });

  it("the close box comes from goAway, not from the procID", () => {
    // Both are movableDBoxProc; only the document window and Chunk List close.
    expect(getWind(Res.wind.document).goAway).toBe(true);
    expect(getWind(Res.wind.chunkList).goAway).toBe(true);
    expect(getWind(Res.wind.progress).goAway).toBe(false);
    expect(getWind(Res.wind.mixer).goAway).toBe(false);
  });
});

describe("MENU templates match SPEC §4", () => {
  it("MBAR 128 lists the eight menus of §4, in order", () => {
    expect(getMbar(Res.mbar)).toEqual([
      Res.menu.apple,
      Res.menu.file,
      Res.menu.edit,
      Res.menu.selection,
      Res.menu.sound,
      Res.menu.effects,
      Res.menu.special,
      Res.menu.windows,
    ]);
    expect(getMbar(Res.mbar).map((id) => getMenu(id).title)).toEqual([
      "\x14",
      "File",
      "Edit",
      "Selection",
      "Sound",
      "Effects",
      "Special",
      "Windows",
    ]);
  });

  it("the Apple menu's title is Chicago's 0x14, rendered as ", () => {
    expect(getMenu(Res.menu.apple).title).toBe("\x14");
    expect(menuTitle(getMenu(Res.menu.apple))).toBe("");
  });

  // §4's File menu: New ⌘N · Open… ⌘O · Close ⌘W · Save ⌘S · Quit ⌘Q.
  it.each([
    [Res.menu.file, "New", "N"],
    [Res.menu.file, "Open…", "O"],
    [Res.menu.file, "Close", "W"],
    [Res.menu.file, "Save", "S"],
    [Res.menu.file, "Quit", "Q"],
    [Res.menu.edit, "Undo", "Z"],
    [Res.menu.edit, "Cut", "X"],
    [Res.menu.edit, "Copy", "C"],
    [Res.menu.edit, "Paste", "V"],
    [Res.menu.selection, "Select All", "A"],
    [Res.menu.selection, "Select None", "D"],
    [Res.menu.selection, "Select Exactly…", "E"],
    [Res.menu.selection, "Select Peak…", "K"],
    [Res.menu.selection, "Set Loop", "L"],
    [Res.menu.selection, "Selection Info…", "I"],
    [Res.menu.effects, "Redo Effect", "/"],
    [Res.menu.special, "Set Chunk", "U"],
  ])("MENU %d's %s has ⌘%s", (menuID, text, key) => {
    const item = getMenu(menuID).items.find((i) => i.text === text);
    expect(item, `${text} in MENU ${menuID}`).toBeDefined();
    expect(commandKey(item!)).toBe(key);
  });

  /**
   * §1.2's no-undo religion, as data. The resource ships Undo disabled: bit 1 of
   * MENU 130's `enableFlags` is clear. The shell does not need to special-case
   * it — it only needs to not "fix" it.
   */
  it("Undo ships disabled in the resource itself", () => {
    const undo = getMenu(Res.menu.edit).items[0]!;
    expect(undo.text).toBe("Undo");
    expect(undo.enabled).toBe(false);
    expect(getMenu(Res.menu.edit).enableFlags & 0b10).toBe(0);
  });

  it("the three hierarchical items point at MENU 140/141/142", () => {
    const hier = (menuID: number, text: string) =>
      hierarchicalSubmenuID(getMenu(menuID).items.find((i) => i.text === text)!);

    expect(hier(Res.menu.file, "Preferences")).toBe(Res.menu.prefsPages);
    expect(hier(Res.menu.edit, "Zoom")).toBe(Res.menu.zoom);
    expect(hier(Res.menu.sound, "Volume")).toBe(Res.menu.volume);
  });

  // §3: "Zoom: Zoom In (⌘=), Zoom Out (⌘-), Fit in Window, Fit Selection".
  it("the Zoom submenu is §3's four items", () => {
    expect(getMenu(Res.menu.zoom).items.map((i) => i.text)).toEqual([
      "Zoom In",
      "Zoom Out",
      "Fit in Window",
      "Fit Selection",
    ]);
  });

  // §3/§6: the eight-step volume ladder — Off, 1–7.
  it("the Volume submenu is the 8-step ladder", () => {
    const items = getMenu(Res.menu.volume).items;
    expect(items).toHaveLength(8);
    expect(items.map((i) => i.text.trim())).toEqual(["Off", "1", "2", "3", "4", "5", "6", "7"]);
  });

  // §4: "populated at launch by scanning the Effects Folder".
  it("the Effects menu ships with only Redo Effect and a separator", () => {
    expect(getMenu(Res.menu.effects).items.map((i) => i.text)).toEqual(["Redo Effect", "-"]);
  });

  /**
   * **A spec correction.** §4 annotates Mixer… with "*no ⌘M — the resource
   * assigns no shortcut; corrected by disassembly*". The resource assigns ⌘M:
   * MENU 134's bytes for that item are `06 "Mixer…" 00 4D 00 00` — icon 0,
   * **key 0x4D = 'M'**, mark 0, style 0.
   *
   * Whether the *app* honours it is a separate question the disassembly may well
   * have settled (a shortcut on an unfinished feature is exactly the kind of
   * thing you'd disable at runtime). But §4's claim is about the resource, and
   * about the resource it is wrong. Pinned so the correction is not lost.
   */
  it("Mixer… DOES carry ⌘M in the resource, contradicting §4", () => {
    const mixer = getMenu(Res.menu.special).items.find((i) => i.text === "Mixer…")!;
    expect(commandKey(mixer)).toBe("M");
  });

  /**
   * **A second, smaller one.** §4 lists File's Preferences as a plain item. It is
   * hierarchical (→ MENU 140) and its submenu is the same five pages §9 lists as
   * the dialog's popup — so the menu offers direct page access *and* the dialog
   * has the popup. MENU 140 and MENU 150 (the popup's own menu, per CNTL 128)
   * are item-for-item identical, which is the corroboration.
   */
  it("Preferences is hierarchical, and its pages are §9's five", () => {
    const pages = ["General", "Window Setup", "Recording - Sound Input Device",
      "Recording - Miscellaneous", "Effects"];
    expect(getMenu(Res.menu.prefsPages).items.map((i) => i.text)).toEqual(pages);
    expect(getMenu(150).items.map((i) => i.text)).toEqual(pages);
  });

  // §7.5's unit codes: 0 samples · 1 bytes · 2 seconds · 3 ms · 4 meters · 5 feet.
  it("MENU 157 is §7.5's 6-unit popup, in unit-code order", () => {
    expect(getMenu(Res.menu.units).items.map((i) => i.text)).toEqual([
      "Samples", "Bytes", "Seconds", "Milliseconds", "Meters", "Feet",
    ]);
  });

  // §9.1: "Default selection units popup (samples/bytes/seconds/ms)" — no
  // meters/feet, because a *selection* default in meters would be nonsense.
  it("MENU 154 is §9's 4-unit prefs popup — no meters or feet", () => {
    expect(getMenu(154).items.map((i) => i.text)).toEqual([
      "Samples", "Bytes", "Seconds", "Milliseconds",
    ]);
  });

  it("the setups popup is §7.5's Save setup as… / Delete setup…", () => {
    expect(getMenu(Res.menu.setups).items.map((i) => i.text)).toEqual([
      "-", "Save setup as…", "Delete setup…",
    ]);
  });
});

describe("CNTL templates match SPEC §3, §7.5 and §14", () => {
  /**
   * The popup CDEF's convention, which the fork uses throughout: for a
   * `popupMenuProc` control, **`contrlMin` holds the MENU resource id** and
   * `contrlMax` holds the title width in pixels. Every popup in the app is wired
   * this way, and each wiring corroborates a different SPEC section.
   */
  it.each([
    ["Default selection units:", 132, 154],
    ["Setup:", 133, Res.menu.setups],
    ["File format:", 135, 156],
    ["units", 136, Res.menu.units],
    ["Use:", 137, 159],
    ["Mix to:", 203, 158],
  ])("popup CNTL for %s (id %d) points at MENU %d", (_label, cntlID, menuID) => {
    expect(getCntl(cntlID).min).toBe(menuID);
  });

  // §10: "Save: System 7 Sound File or AIFF (format popup in the save dialog)".
  it("the save-format popup offers exactly System 7 Sound File and AIFF", () => {
    expect(getMenu(156).items.map((i) => i.text)).toEqual(["System 7 Sound File", "AIFF"]);
  });

  // §14 item 26: "The sliders are 0–255 [RES: CNTL 307–313]".
  it.each([[309], [312], [313]])("VOX slider CNTL %d is 0–255", (id) => {
    const c = getCntl(id);
    expect([c.min, c.max]).toEqual([0, 255]);
  });

  // §3: the Level window's "Sound Input Gain slider (0–255)".
  it("the input-gain slider is 0–255", () => {
    const c = getCntl(800);
    expect([c.min, c.max]).toEqual([0, 255]);
  });

  // §3: Mixer is "Mix (CNTL 201) / Cancel (CNTL 202)"; Spectrum "a single OK
  // button (CNTL 500)"; Progress "Pause/Resume + Stop"; Play Clipboard "Stop
  // Playing". Every one of those titles is in the fork.
  it.each([
    [201, "Mix"], [202, "Cancel"], [500, "OK"],
    [600, "Stop Playing"], [700, "Stop"], [701, "Pause"],
  ])("CNTL %d is titled %s", (id, title) => {
    expect(getCntl(id).title).toBe(title);
  });

  it("every CNTL has min <= max", () => {
    for (const [id, c] of Object.entries(CNTL)) {
      // Popups reuse min/max for menu id and title width, so they are exempt.
      if (c.procID >= 1008) continue;
      expect(c.min, `CNTL ${id}`).toBeLessThanOrEqual(c.max);
    }
  });
});

describe("the string corpus (SPEC §11)", () => {
  /**
   * **A spec correction.** §11 says "STR# 128, 66 strings". The fork holds
   * **64**. Nothing downstream depended on the count — it is recorded because
   * §11's whole point is that this corpus is preserved exactly, and a preserved
   * corpus you have miscounted is a corpus you have not checked.
   */
  it("STR# 128 holds 64 strings, not §11's 66", () => {
    expect(STR_LIST[String(Res.errors)]).toHaveLength(64);
  });

  // §11's "highlights that must survive verbatim", checked verbatim.
  it("preserves the all-purpose lament", () => {
    expect(errorString(2)).toBe(
      "There is not enough memory.\rTry to close unused windows to free up some memory.",
    );
    expect(lines(errorString(2))).toEqual([
      "There is not enough memory.",
      "Try to close unused windows to free up some memory.",
    ]);
  });

  it("preserves the only-channel refusal of §3, curly quotes and all", () => {
    expect(errorString(24, "untitled sound #1")).toBe(
      "I’m sorry, but I can’t delete the only channel of a mono sound.\r" +
        "Delete the document “untitled sound #1” instead.",
    );
  });

  it("preserves the first-person 'I couldn’t find any sounds'", () => {
    expect(errorString(3, "Beep")).toBe("I couldn’t find any sounds in the document “Beep”.");
  });

  it("fills multiple ∑ placeholders positionally, in order", () => {
    // "I couldn’t save “∑” because the disk “∑” is full…" — document, then disk.
    expect(errorString(12, "Beep", "Macintosh HD")).toContain("“Beep”");
    expect(errorString(12, "Beep", "Macintosh HD")).toContain("“Macintosh HD”");
    expect(fill("∑ then ∑", "a", "b")).toBe("a then b");
  });

  it("leaves an unfilled ∑ standing rather than printing undefined", () => {
    expect(fill("a ∑ b ∑", "x")).toBe("a x b ∑");
  });

  it("STR# 130 holds the two words the Save-changes alert interpolates", () => {
    expect(STR_LIST["130"]).toEqual(["closing", "quitting"]);
  });

  // §4: "Cut/Copy/Paste/Clear dynamically relabel to Cut Channel / …".
  it("STR# 136 relabels the Edit menu by a +4 offset", () => {
    expect(editMenuLabel(EditVerb.cut, false)).toBe("Cut");
    expect(editMenuLabel(EditVerb.cut, true)).toBe("Cut Channel");
    expect(editMenuLabel(EditVerb.clear, false)).toBe("Clear");
    expect(editMenuLabel(EditVerb.clear, true)).toBe("Clear Channel");
  });

  it("STR# indices are 1-based, as GetIndString had them", () => {
    expect(getIndString(Res.errors, 1)).toBe("An unknown error occurred.");
    expect(() => getIndString(Res.errors, 0)).toThrow(/couldn't find my own resources/);
    expect(() => getIndString(Res.errors, 65)).toThrow(/couldn't find my own resources/);
  });
});

describe("DITL and ALRT", () => {
  it("every DITL item has a 1-based id and a sane rect", () => {
    for (const [id, items] of Object.entries(DITL)) {
      items.forEach((item, i) => {
        expect(item.id, `DITL ${id} item ${i}`).toBe(i + 1);
        expect(item.rect.right).toBeGreaterThanOrEqual(item.rect.left);
        expect(item.rect.bottom).toBeGreaterThanOrEqual(item.rect.top);
      });
    }
  });

  // §9: "Five pages, all items known from DITL 1001–1005".
  it.each([[1001], [1002], [1003], [1004], [1005]])("DITL %d (a prefs page) parses", (id) => {
    expect(DITL[String(id)]!.length).toBeGreaterThan(0);
  });

  it("every ALRT points at a DITL that exists", () => {
    for (const [id, a] of Object.entries(ALRT)) {
      expect(DITL[String(a.itemsID)], `ALRT ${id} → DITL ${a.itemsID}`).toBeDefined();
    }
  });

  it("every resControl item points at a CNTL that exists", () => {
    for (const [id, items] of Object.entries(DITL)) {
      for (const item of items) {
        if (item.kind !== "resControl") continue;
        expect(CNTL[String(item.resID)], `DITL ${id} → CNTL ${item.resID}`).toBeDefined();
      }
    }
  });

  it("every hierarchical menu item points at a MENU that exists", () => {
    for (const [id, m] of Object.entries(MENU)) {
      for (const item of m.items) {
        const sub = hierarchicalSubmenuID(item);
        if (sub === null) continue;
        expect(MENU[String(sub)], `MENU ${id} → MENU ${sub}`).toBeDefined();
      }
    }
  });
});
