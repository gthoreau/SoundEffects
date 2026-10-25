// AppCommands — every command the application menus offer, in ONE table: its id,
// its menu text, its shortcut. The menus, the key bindings and the shortcut column
// the menus draw are all generated from it, so a shortcut is decided in one place
// and cannot be shown in one form and bound in another.
//
// The table follows the 0.4.0 spec (specs_v0-4-0.md, "Application Menus"), with the
// decisions recorded in docs/0.4.0/PLAN.md: ⌘␣ and ⌃␣ belong to macOS (Spotlight,
// input source), so Play Forward and Play Backward have no shortcut and D cycles the
// direction instead.
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
    insertSilence, crop, setLoop, selectLoop, selectExactly, selectPeak, selectSilence,

    // Transport
    record = 0x5ec0, selectAudioSource, stop, play, pause, loop, fastForward,
    playForward, playBackward, playPingPong, cycleDirection, mute, solo,

    // Window
    changeName = 0x5f00, toggleSidePanels, toggleKeyboard, toggleGrid, zoom100, zoomToFit,
};

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
