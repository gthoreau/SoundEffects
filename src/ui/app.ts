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
import { Transport, TransportState } from "../audio/transport.js";
import { canPlay, NOT_ENOUGH_HARDWARE } from "../audio/backend.js";
import type { SoundFormat } from "../formats/index.js";
import { showAlert } from "./alert.js";
import { el } from "./dom.js";
import { DocumentWindow } from "./document-window.js";
import { confirmExport, downloadSound, openSoundFile, pickFile } from "./files.js";
import { MenuBar, type MenuRef, type Modifiers } from "./menubar.js";
import { type MenuItem, Res } from "./resources.js";
import { DrawMode, type DrawModeName } from "./waveform.js";
import { EditVerb, editMenuLabel } from "./strings.js";
import { type TransportButtonName } from "./transport-strip.js";
import { WebAudioBackend } from "./web-audio.js";
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
const Sound = {
  record: 1,
  stop: 2,
  pause: 3,
  play: 4,
  fastForward: 5,
  loop: 6,
} as const;
const Zoom = { zoomIn: 1, zoomOut: 2, fitInWindow: 3, fitSelection: 4 } as const;
/** MENU 142's items are Off, 1–7 — so item index n is volume n−1. */
const VOLUME_ITEM_BASE = 1;

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

  /**
   * One device, shared. Each document gets its own `Transport` (it is
   * constructed around a document), but only one may hold the speaker at a time
   * — which is also what the original did, having exactly one Sound Manager
   * channel to give.
   */
  private backend: WebAudioBackend;
  private transports = new Map<SoundDocument, Transport>();
  private playing: Transport | null = null;
  /** The Loop toggle and the Volume ladder are app-wide, not per document. */
  private loop = false;
  private volume = 7;

  constructor(root: HTMLElement) {
    const desktop = el("div", { id: "sfx-desktop" });
    this.manager = new WindowManager(desktop);
    this.backend = new WebAudioBackend({ onEnded: () => this.onPlaybackEnded() });

    this.menubar = new MenuBar({
      stateFor: (ref, item) => this.stateFor(ref, item),
      onCommand: (ref, item, mods) => this.command(ref, item, mods),
      dynamicItems: (menuID) => this.dynamicItems(menuID),
    });

    root.append(this.menubar.element, desktop);
    this.manager.onChange(() => this.menubar.close());
    window.addEventListener("keydown", (e) => this.onKeyDown(e));
  }

  /**
   * The backend hands back the speaker whenever the source stops giving frames —
   * which happens both at the end of a sound *and* on a pause, since a paused
   * `Transport` renders nothing. Only the transport knows which it was.
   */
  private onPlaybackEnded(): void {
    if (this.playing?.currentState === TransportState.paused) return;
    this.playing = null;
    this.refreshTransportUI();
  }

  private transportFor(doc: SoundDocument): Transport {
    let t = this.transports.get(doc);
    if (!t) {
      // The device's rate has to be known before the Transport exists: it is the
      // rate the document's declared rate gets resampled *to*, and Transport
      // reads it to compute its step.
      this.backend.prepare();
      t = new Transport(doc, { capabilities: this.backend.capabilities, volume: this.volume });
      this.transports.set(doc, t);
    }
    t.volume = this.volume;
    return t;
  }

  private refreshTransportUI(): void {
    for (const dw of this.docWindows) dw.setTransportState(this.stateOfWindow(dw));
  }

  private stateOfWindow(dw: DocumentWindow): string {
    const t = this.transports.get(dw.doc);
    if (!t || t !== this.playing) return TransportState.stopped;
    return t.currentState;
  }

  /** §4's keyboard map: space is the play/pause toggle. */
  private onKeyDown(e: KeyboardEvent): void {
    if (e.metaKey || e.ctrlKey || e.altKey) return;
    const target = e.target as HTMLElement | null;
    if (target && /^(INPUT|TEXTAREA|SELECT)$/.test(target.tagName)) return;

    if (e.code === "Space") {
      e.preventDefault();
      const dw = this.front;
      if (!dw) return;
      // Space toggles: if this document is the one playing, pause/resume it;
      // otherwise start it.
      if (this.playing && this.transports.get(dw.doc) === this.playing) this.pausePlayback();
      else void this.startPlayback(dw, {});
    }
  }

  /**
   * Play. §6 gates on the *document's* bit depth, not the selection's, and its
   * refusal is the app: it does not downsample for you, it tells you to go do it
   * yourself. On modern hardware `supports16Bit` is true so this never fires —
   * but a backend constructed with `EIGHT_BIT_CAPABILITIES` gets the original's
   * voice back verbatim.
   */
  private async startPlayback(dw: DocumentWindow, opts: { useLoopRegion?: boolean; soloChannel?: number }): Promise<void> {
    if (!canPlay(dw.doc, this.backend.capabilities)) {
      await showAlert({ message: NOT_ENOUGH_HARDWARE });
      return;
    }
    if (this.playing) this.playing.stop();
    const t = this.transportFor(dw.doc);
    t.play({ loop: this.loop, useLoopRegion: opts.useLoopRegion, soloChannel: opts.soloChannel });
    this.playing = t;
    this.backend.start(t);
    this.refreshTransportUI();
  }

  private pausePlayback(): void {
    if (!this.playing) return;
    this.playing.pause();
    if (this.playing.currentState === TransportState.playing) this.backend.start(this.playing);
    else this.backend.stop();
    this.refreshTransportUI();
  }

  private stopPlayback(): void {
    this.playing?.stop();
    this.backend.stop();
    this.playing = null;
    this.refreshTransportUI();
  }

  private onTransportButton(dw: DocumentWindow, name: TransportButtonName): void {
    switch (name) {
      case "play":
        void this.startPlayback(dw, {});
        break;
      case "stop":
        this.stopPlayback();
        break;
      case "pause":
        this.pausePlayback();
        break;
      case "fastForward":
        this.transports.get(dw.doc)?.fastForward();
        break;
      case "loop":
        this.setLoopEnabled(!this.loop);
        break;
      case "record":
        // §6's recorder is built and tested; the Record window (WIND 133) that
        // drives it is not. Greying the button would be a lie about the model,
        // so it says what is actually missing.
        void showAlert({
          message: "I can't record yet — my Record window isn't built.",
          icon: "note",
        });
        break;
    }
  }

  private setLoopEnabled(on: boolean): void {
    this.loop = on;
    for (const dw of this.docWindows) dw.setLoop(on);
  }

  private setVolume(v: number): void {
    this.volume = v;
    for (const t of this.transports.values()) t.volume = v;
    for (const dw of this.docWindows) dw.setVolume(v);
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
      if (ref.item === File.new || ref.item === File.open) return { enabled: true };
      if (ref.item === File.close) return { enabled: !!doc };
      if (ref.item === File.saveAs) return { enabled: !!doc };
      return { enabled: BUILT.has(refKeyOf(ref)) };
    }

    if (ref.menuID === Res.menu.sound) {
      const t = doc ? this.transports.get(doc) : null;
      const isPlaying = !!t && t === this.playing && t.currentState === TransportState.playing;
      switch (ref.item) {
        case Sound.play:
        case Sound.pause:
        case Sound.fastForward:
          return { enabled: !!doc };
        case Sound.stop:
          return { enabled: isPlaying || (!!t && t === this.playing) };
        // A checkmark, because Loop is a toggle and MENU 132 gives it no other
        // affordance.
        case Sound.loop:
          return { enabled: !!doc, mark: this.loop ? "✓" : null };
        default:
          return { enabled: BUILT.has(refKeyOf(ref)) };
      }
    }

    // MENU 142: Off, 1–7. The current step is marked, radio-style.
    if (ref.menuID === Res.menu.volume) {
      return { enabled: true, mark: item.index - VOLUME_ITEM_BASE === this.volume ? "✓" : null };
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
        else if (ref.item === File.open) void this.openFile();
        else if (ref.item === File.close) dw?.win.close();
        else if (ref.item === File.saveAs && dw) void this.saveAs(dw.doc);
        return;

      case Res.menu.sound:
        if (!dw) return;
        switch (ref.item) {
          case Sound.play:
            // option-Play loops the Set Loop region instead of the selection (§4).
            void this.startPlayback(dw, { useLoopRegion: mods.option });
            break;
          case Sound.stop:
            this.stopPlayback();
            break;
          case Sound.pause:
            this.pausePlayback();
            break;
          case Sound.fastForward:
            this.transports.get(dw.doc)?.fastForward();
            break;
          case Sound.loop:
            this.setLoopEnabled(!this.loop);
            break;
          case Sound.record:
            this.onTransportButton(dw, "record");
            break;
        }
        return;

      case Res.menu.volume:
        this.setVolume(item.index - VOLUME_ITEM_BASE);
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

  /** File > Open (§10). */
  async openFile(): Promise<DocumentWindow | null> {
    const file = await pickFile();
    if (!file) return null;
    const doc = await openSoundFile(file);
    if (!doc) return null;
    return this.openDocument(doc);
  }

  /**
   * File > Save As (§10). AIFF is canonical; WAV is the convenience.
   *
   * The format choice is `MENU 156`'s ("System 7 Sound File" / "AIFF") in the
   * original, but we no longer write System 7 sound files — they are resource-fork
   * documents no modern filesystem carries (step 6). So the popup's two entries
   * become AIFF and WAV, and `confirmExport` says what each will cost.
   */
  async saveAs(doc: SoundDocument): Promise<void> {
    const answer = await showAlert({
      message: `Save “${doc.name}” as which kind of file?`,
      icon: "note",
      buttons: [
        { text: "AIFF", isDefault: true },
        { text: "WAV" },
        { text: "Cancel", isCancel: true },
      ],
    });
    if (answer !== 0 && answer !== 1) return;
    const format: SoundFormat = answer === 0 ? "aiff" : "wav";
    if (!(await confirmExport(doc, format))) return;
    downloadSound(doc, format);
  }

  openDocument(doc: SoundDocument): DocumentWindow {
    const dw = new DocumentWindow(this.manager, doc, {
      drawMode: this.drawMode,
      loop: this.loop,
      volume: this.volume,
      onTransport: (name) => this.onTransportButton(dw, name),
      onVolume: (v) => this.setVolume(v),
      onClose: () => {
        // A closing document takes its transport with it, and the speaker if it
        // held it. Leaving a Transport pointed at a closed document alive would
        // keep the whole buffer reachable.
        const t = this.transports.get(doc);
        if (t && t === this.playing) this.stopPlayback();
        this.transports.delete(doc);
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
  `${Res.menu.file}:${File.open}`,
  `${Res.menu.file}:${File.close}`,
  `${Res.menu.file}:${File.saveAs}`,
  `${Res.menu.sound}:${Sound.play}`,
  `${Res.menu.sound}:${Sound.stop}`,
  `${Res.menu.sound}:${Sound.pause}`,
  `${Res.menu.sound}:${Sound.fastForward}`,
  `${Res.menu.sound}:${Sound.loop}`,
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
