/**
 * The application — SPEC §15 step 7's vertical slice.
 *
 * What this wires up: the menu bar (from MBAR 128), the desktop, document
 * windows, and the File/Edit/Selection/Sound commands that the core already
 * implements. Effects dialogs, Preferences and the utility windows are the
 * second pass; the commands that are not built yet are *disabled*, not faked —
 * a menu item that silently does nothing would be worse than a grey one.
 *
 * The behavioural contract from §1 and §12, enforced here rather than assumed:
 *
 * - **Undo is greyed and stays greyed.** It comes out of the resource disabled
 *   and `commandEnabled` never turns it on. ⌘Z is swallowed so the browser's own
 *   undo cannot stand in for it.
 * - **Edits are destructive and in place.** Cut/Clear call straight into
 *   `src/edit.ts`. Nothing is snapshotted anywhere. There is no undo stack to
 *   forget to write to, which is the point.
 * - **A caret is an insertion point, not "everything".** §2.4's "empty selection
 *   means everything" is an *effects* rule (`Gain.c`); menu edits must not wipe a
 *   document because nothing was selected. The core already draws that line
 *   (step 2's note); the shell just doesn't undo it.
 */

import { SoundDocument } from "../document.js";
import { clear, copy, crop, cut, paste } from "../edit.js";
import { setLoop, setSelection } from "../selection.js";
import { el } from "./dom.js";
import { DocumentWindow } from "./document-window.js";
import { MenuBar, type MenuRef, type Modifiers } from "./menubar.js";
import { type MenuItem, Res } from "./resources.js";
import { DrawMode, type DrawModeName } from "./waveform.js";
import { EditVerb, editMenuLabel } from "./strings.js";
import { WindowManager } from "./window.js";

/** Menu item indices, by the ids the resources give them. See §4. */
const File = { new: 1, open: 2, close: 4, save: 5, saveAs: 6, revert: 7, quit: 11 } as const;
const Edit = {
  undo: 1,
  cut: 3,
  copy: 4,
  paste: 5,
  clear: 6,
  leaveSilence: 8,
  shiftSound: 9,
  playClipboard: 13,
} as const;
const Selection = {
  selectAll: 1,
  selectNone: 2,
  extendToBeginning: 4,
  extendToEnd: 5,
  selectExactly: 7,
  selectPeak: 8,
  selectSilence: 9,
  setLoop: 11,
  selectLoop: 12,
  crop: 14,
} as const;
const Zoom = { zoomIn: 1, zoomOut: 2, fitInWindow: 3, fitSelection: 4 } as const;

/**
 * Index space for items appended at runtime. Well above any resource's item
 * count (a MENU's `enableFlags` only addresses 31 items, so nothing resourced
 * comes near this), which keeps generated items from colliding with real ones.
 */
const DYNAMIC_ITEM_BASE = 1000;

export class App {
  private manager: WindowManager;
  private menubar: MenuBar;
  private docWindows: DocumentWindow[] = [];
  private clipboard: SoundDocument | null = null;
  private untitledCount = 0;
  private drawMode: DrawModeName = DrawMode.dots;

  constructor(root: HTMLElement) {
    const desktop = el("div", { id: "sfx-desktop" });
    this.manager = new WindowManager(desktop);

    this.menubar = new MenuBar({
      stateFor: (ref, item) => this.stateFor(ref, item),
      onCommand: (ref, item, mods) => this.command(ref, item, mods),
      dynamicItems: (menuID) => this.dynamicItems(menuID),
    });

    root.append(this.menubar.element, desktop);
    this.manager.onChange(() => this.menubar.close());
  }

  private get front(): DocumentWindow | null {
    const active = this.manager.activeWindow;
    return this.docWindows.find((d) => d.win === active) ?? this.docWindows[0] ?? null;
  }

  /**
   * Whether a command can fire right now.
   *
   * Everything not listed is left as the resource has it, so a command this pass
   * has not built stays exactly as enabled as the original shipped it — until
   * {@link BUILT} says otherwise.
   */
  private stateFor(ref: MenuRef, item: MenuItem) {
    const doc = this.front?.doc ?? null;
    const hasSel = !!doc && doc.selEnd > doc.selSt;

    // The Windows menu's document list is generated, not resourced, so it has no
    // entry in BUILT and must not be measured against it — it *is* built.
    if (ref.menuID === Res.menu.windows && item.index >= DYNAMIC_ITEM_BASE) {
      return { enabled: true };
    }

    // Whole channels selected → Cut becomes Cut Channel, etc. (§4, STR# 136).
    const wholeChannels =
      !!doc && doc.selSt === 0 && doc.selEnd >= doc.maxRelChSize() && doc.selEnd > 0;

    if (ref.menuID === Res.menu.edit) {
      switch (ref.item) {
        // §1.2. The resource ships it disabled; we make sure it stays that way.
        case Edit.undo:
          return { enabled: false };
        case Edit.cut:
          return { enabled: hasSel, text: editMenuLabel(EditVerb.cut, wholeChannels) };
        case Edit.copy:
          return { enabled: hasSel, text: editMenuLabel(EditVerb.copy, wholeChannels) };
        case Edit.paste:
          return {
            enabled: !!doc && !!this.clipboard,
            text: editMenuLabel(EditVerb.paste, wholeChannels),
          };
        case Edit.clear:
          return { enabled: hasSel, text: editMenuLabel(EditVerb.clear, wholeChannels) };
        case Edit.playClipboard:
          return { enabled: !!this.clipboard };
        default:
          return { enabled: !!doc && BUILT.has(refKeyOf(ref)) };
      }
    }

    if (ref.menuID === Res.menu.selection || ref.menuID === Res.menu.zoom) {
      return { enabled: !!doc && BUILT.has(refKeyOf(ref)) };
    }

    if (ref.menuID === Res.menu.file) {
      if (ref.item === File.new) return { enabled: true };
      if (ref.item === File.close) return { enabled: !!doc };
      return { enabled: BUILT.has(refKeyOf(ref)) };
    }

    // The Effects menu is populated at launch by scanning the Effects Folder
    // (§7.1). Nothing scans yet, so it has only Redo Effect, and nothing to redo.
    if (ref.menuID === Res.menu.effects) return { enabled: false };

    return { enabled: item.enabled && BUILT.has(refKeyOf(ref)) };
  }

  private dynamicItems(menuID: number): MenuItem[] {
    // §4: the Windows menu carries the open-document list after its separator.
    if (menuID !== Res.menu.windows) return [];
    return this.manager.list().map((w, i) => ({
      index: DYNAMIC_ITEM_BASE + i,
      text: w.title,
      icon: 0,
      key: 0,
      mark: w === this.manager.activeWindow ? "✓" : 0,
      style: 0,
      enabled: true,
    }));
  }

  private command(ref: MenuRef, item: MenuItem, mods: Modifiers): void {
    const dw = this.front;

    if (ref.menuID === Res.menu.windows && item.index >= DYNAMIC_ITEM_BASE) {
      const win = this.manager.list()[item.index - DYNAMIC_ITEM_BASE];
      if (win) this.manager.activate(win);
      return;
    }

    switch (ref.menuID) {
      case Res.menu.file:
        if (ref.item === File.new) this.newDocument();
        else if (ref.item === File.close) dw?.win.close();
        return;

      case Res.menu.edit:
        if (!dw) return;
        switch (ref.item) {
          case Edit.cut:
            this.clipboard = cut(dw.doc);
            break;
          case Edit.copy:
            this.clipboard = copy(dw.doc);
            break;
          case Edit.paste:
            if (this.clipboard) paste(dw.doc, this.clipboard);
            break;
          case Edit.clear:
            clear(dw.doc);
            break;
          default:
            return;
        }
        dw.refresh();
        return;

      case Res.menu.zoom:
        if (!dw) return;
        // option-Zoom-In = max zoom; option-Fit = fit selection (§3).
        if (ref.item === Zoom.zoomIn) {
          if (mods.option) dw.zoomToMax();
          else dw.zoom(0.5);
        } else if (ref.item === Zoom.zoomOut) {
          dw.zoom(2);
        } else if (ref.item === Zoom.fitInWindow) {
          if (mods.option) dw.zoomToSelection();
          else dw.zoomToFit();
        } else if (ref.item === Zoom.fitSelection) {
          dw.zoomToSelection();
        }
        return;

      case Res.menu.selection:
        if (!dw) return;
        switch (ref.item) {
          // shift = single channel; option = stereo pair (§4).
          case Selection.selectAll:
            setSelection(dw.doc, 0, dw.doc.maxChSize());
            if (!mods.shift) {
              dw.doc.firstSelCh = 1;
              dw.doc.lastSelCh = mods.option ? Math.min(2, dw.doc.numChans) : dw.doc.numChans;
            }
            break;
          case Selection.selectNone:
            setSelection(dw.doc, dw.doc.selSt, dw.doc.selSt);
            break;
          case Selection.extendToBeginning:
            setSelection(dw.doc, 0, dw.doc.selEnd);
            break;
          case Selection.extendToEnd:
            setSelection(dw.doc, dw.doc.selSt, dw.doc.maxChSize());
            break;
          case Selection.setLoop:
            setLoop(dw.doc);
            break;
          case Selection.selectLoop:
            setSelection(dw.doc, dw.doc.loopSt, dw.doc.loopEnd);
            break;
          case Selection.crop:
            crop(dw.doc);
            break;
          default:
            return;
        }
        dw.refresh();
        return;

      default:
        return;
    }
  }

  newDocument(): DocumentWindow {
    // §3: "Default title for new docs: untitled sound #N".
    this.untitledCount++;
    const doc = new SoundDocument({ name: `untitled sound #${this.untitledCount}` });
    return this.openDocument(doc);
  }

  openDocument(doc: SoundDocument): DocumentWindow {
    const dw = new DocumentWindow(this.manager, doc, {
      drawMode: this.drawMode,
      onClose: () => {
        const i = this.docWindows.indexOf(dw);
        if (i >= 0) this.docWindows.splice(i, 1);
      },
    });
    this.docWindows.push(dw);
    return dw;
  }

  setDrawMode(mode: DrawModeName): void {
    this.drawMode = mode;
    for (const d of this.docWindows) d.setDrawMode(mode);
  }
}

function refKeyOf(ref: MenuRef): string {
  return `${ref.menuID}:${ref.item}`;
}

/**
 * The commands this pass actually implements.
 *
 * An explicit allow-list, so an unbuilt command greys out instead of pretending.
 * Growing this set *is* the second pass: add the dialog, add the key.
 */
const BUILT = new Set<string>([
  `${Res.menu.file}:${File.new}`,
  `${Res.menu.file}:${File.close}`,
  `${Res.menu.edit}:${Edit.cut}`,
  `${Res.menu.edit}:${Edit.copy}`,
  `${Res.menu.edit}:${Edit.paste}`,
  `${Res.menu.edit}:${Edit.clear}`,
  `${Res.menu.selection}:${Selection.selectAll}`,
  `${Res.menu.selection}:${Selection.selectNone}`,
  `${Res.menu.selection}:${Selection.extendToBeginning}`,
  `${Res.menu.selection}:${Selection.extendToEnd}`,
  `${Res.menu.selection}:${Selection.setLoop}`,
  `${Res.menu.selection}:${Selection.selectLoop}`,
  `${Res.menu.selection}:${Selection.crop}`,
  `${Res.menu.zoom}:${Zoom.zoomIn}`,
  `${Res.menu.zoom}:${Zoom.zoomOut}`,
  `${Res.menu.zoom}:${Zoom.fitInWindow}`,
  `${Res.menu.zoom}:${Zoom.fitSelection}`,
]);
