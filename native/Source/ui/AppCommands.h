// AppCommands — every command the application menus offer, in ONE table: its id,
// its menu text, its shortcut. The menus, the key bindings and the shortcut column
// the menus draw are all generated from it, so a shortcut is decided in one place
// and cannot be shown in one form and bound in another.
//
// The table follows the 0.5.0 spec (specs_v0-5-0.md, "Application Menus — v.0.5.0"),
// with the decisions recorded in docs/0.5.0/PLAN.md (and, before them, the 0.4.0 ones:
// ⌘␣ and ⌃␣ belong to macOS, so D flips the direction and Play Forward / Backward
// have no shortcut).
//
// Commands whose feature has not landed yet are in the table anyway: the menus show
// them greyed, so the menu structure is the final one from the start.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace cmd {

enum : juce::CommandID
{
    // File
    newSound = 0x5e01, openSound, closeSound, importSound, exportSound,
    newCanvas, openCanvas, saveCanvas, saveCanvasAs, revertCanvas, audioSettings,

    // Edit
    undo = 0x5e40, cut, copy, paste, duplicate, leaveSilence, shiftSound, clear,

    // Selection
    selectAll = 0x5e80, extendToBeginning, extendToEnd, extendAbove, extendBelow,
    insertSilence, crop, setLoop, unsetLoop, selectLoop, selectExactly, selectPeak, selectSilence,

    // Sound (the 0.4 Transport menu, plus the selected Sound's Color and name)
    soundColour = 0x5ec0, rename, armRecord, directRecording, selectAudioSource,
    stop, play, pause, flipDirection, fastForward, loop, mute, solo,
    playForward, playBackward, playPingPong,   // bound, not in a menu (D flips instead)

    // Canvas (the 0.4 Window menu)
    toggleColourTheme = 0x5f00, toggleSidePanels, toggleKeyboard, toggleGrid, zoom100, zoomToFit,

    // Canvas: the sound list. Keys 1..9, 0 focus mixer slots 1..10 (and their Window).
    focusSlot1 = 0x5f20, focusSlot10 = focusSlot1 + 9,
};

/** The slot a focusSlot command focuses, 0-based, or -1 for any other command. */
inline int slotOf (juce::CommandID id) { return id >= focusSlot1 && id <= focusSlot10 ? (int) (id - focusSlot1) : -1; }

/** One row of the table. */
struct Spec
{
    juce::CommandID id;
    const char* name;        // menu text, exactly as the design writes it
    const char* category;    // the menu it lives in
    std::vector<juce::KeyPress> keys;   // the first is the one the menu shows
};

/** The whole table, in no particular order (menus pick their own order). */
const std::vector<Spec>& table();

/** One command's row, or nullptr. */
const Spec* find (juce::CommandID);

/** Name, category and default keys from the table into JUCE's command info. The
    caller adds state (active, ticked) and may rename a toggle. */
void fill (juce::ApplicationCommandInfo&, juce::CommandID);

/** A shortcut as the design writes it: modifiers in the order ⌘ ⇧ ⌥ ⌃, then the
    key — "⌘⇧N", "⌥␣", "⇧⌫", "Esc". (macOS itself puts ⇧ first; the design does
    not, and the design wins inside our own menus.) */
juce::String describe (const juce::KeyPress&);

} // namespace cmd
