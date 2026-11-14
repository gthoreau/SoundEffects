#include "AppCommands.h"

namespace cmd {

namespace {

using KP = juce::KeyPress;
using Mods = juce::ModifierKeys;

constexpr int kCmd   = Mods::commandModifier;
constexpr int kShift = Mods::shiftModifier;
constexpr int kAlt   = Mods::altModifier;

KP key (int code, int mods = 0) { return KP (code, Mods (mods), 0); }

} // namespace

const std::vector<Spec>& table()
{
    // "..." as three full stops: that is how the design writes it, and in Classic
    // Chicago it is wider than the single ellipsis character.
    static const std::vector<Spec> rows = {
        // --- File --------------------------------------------------------------------
        { newSound,      "New Sound",            "File", { key ('N', kCmd) } },
        { openSound,     "Open Sound",           "File", { key ('O', kCmd) } },
        { closeSound,    "Close Sound",          "File", { key ('W', kCmd) } },
        { importSound,   "Import Sound...", "File", {} },
        { exportSound,   "Export Sound...", "File", {} },
        { newCanvas,     "New Canvas",           "File", { key ('N', kCmd | kShift) } },
        { openCanvas,    "Open Canvas",          "File", { key ('O', kCmd | kShift) } },
        { saveCanvas,    "Save Canvas",          "File", { key ('S', kCmd) } },
        { saveCanvasAs,  "Save Canvas as...", "File", { key ('S', kCmd | kShift) } },
        { revertCanvas,  "Revert Canvas",        "File", {} },
        { audioSettings, "Audio/Midi Setup",     "File", { key (',', kCmd) } },

        // --- Edit --------------------------------------------------------------------
        { undo,          "Undo",                 "Edit", { key ('Z', kCmd) } },
        { cut,           "Cut",                  "Edit", { key ('X', kCmd) } },
        { copy,          "Copy",                 "Edit", { key ('C', kCmd) } },
        { paste,         "Paste",                "Edit", { key ('V', kCmd) } },
        { duplicate,     "Duplicate",            "Edit", { key ('D', kCmd) } },
        { leaveSilence,  "Leave Silence...", "Edit", {} },
        { shiftSound,    "Shift Sound...",   "Edit", {} },
        { clear,         "Clear",                "Edit", { key (KP::backspaceKey), key (KP::deleteKey) } },

        // --- Selection -----------------------------------------------------------------
        { selectAll,         "Select All",           "Selection", { key ('A', kCmd) } },
        { extendToBeginning, "Extend to Beginning",  "Selection", { key (KP::leftKey,  kCmd) } },
        { extendToEnd,       "Extend to End",        "Selection", { key (KP::rightKey, kCmd) } },
        { extendAbove,       "Extend Above",         "Selection", { key (KP::upKey,    kCmd) } },
        { extendBelow,       "Extend Below",         "Selection", { key (KP::downKey,  kCmd) } },
        { insertSilence,     "Insert Silence",       "Selection", { key (KP::backspaceKey, kShift), key (KP::deleteKey, kShift) } },
        { crop,              "Crop",                 "Selection", { key (KP::backspaceKey, kAlt),   key (KP::deleteKey, kAlt) } },
        { setLoop,           "Set Loop",             "Selection", { key ('L', kCmd) } },
        { unsetLoop,         "Unset Loop",           "Selection", { key ('L', kCmd | kShift) } },
        { selectLoop,        "Select Set Loop",      "Selection", { key ('A', kCmd | kShift) } },
        { selectExactly,     "Select Exactly...", "Selection", {} },
        { selectPeak,        "Select Peak...",    "Selection", {} },
        { selectSilence,     "Select Silence",       "Selection", { key (KP::backspaceKey, kCmd | kShift) } },

        // --- Sound ---------------------------------------------------------------------
        // Color's row is a submenu and shows no shortcut (as the design draws it);
        // ⌘T drops the same list down at the selected Window.
        { soundColour,       "Color",                "Sound", { key ('T', kCmd) } },
        { rename,            "Rename...",         "Sound", { key ('R', kCmd) } },
        { armRecord,         "Arm Record",           "Sound", { key ('R') } },
        { directRecording,   "Direct Recording",     "Sound", { key ('R', kShift) } },
        { selectAudioSource, "Select Audio Source...", "Sound", { key ('R', kCmd | kShift) } },
        { stop,              "Stop",                 "Sound", { key (KP::escapeKey) } },
        { play,              "Play",                 "Sound", { key (KP::spaceKey) } },
        { pause,             "Pause",                "Sound", { key (KP::spaceKey) } },
        { flipDirection,     "Flip Direction",       "Sound", { key ('D') } },
        { fastForward,       "Fast Forward",         "Sound", { key ('F') } },
        { loop,              "Loop",                 "Sound", { key ('L') } },
        { mute,              "Mute",                 "Sound", { key ('M') } },
        { solo,              "Solo",                 "Sound", { key ('S') } },
        { playForward,       "Play Forward",         "Sound", {} },
        { playBackward,      "Play Backward",        "Sound", {} },
        { playPingPong,      "Play Ping-pong",       "Sound", { key (KP::spaceKey, kAlt) } },

        // --- Canvas --------------------------------------------------------------------
        { toggleColourTheme, "Hide Color Theme",     "Canvas", {} },
        { toggleGrid,        "Free Canvas",          "Canvas", { key ('G') } },
        { toggleKeyboard,    "Hide Keyboard",        "Canvas", { key ('K') } },
        { toggleSidePanels,  "Hide Side Panels",     "Canvas", { key ('.', kCmd) } },
        { zoom100,           "Zoom 100%",            "Canvas", { key ('0', kCmd) } },
        { zoomToFit,         "Zoom to Fit",          "Canvas", { key ('1', kCmd) } },

        // The sound list's shortcut column: the menu shows each sound's own name
        // on these rows, not the names here.
        { focusSlot1,        "Sound in Slot 1",      "Canvas", { key ('1') } },
        { focusSlot1 + 1,    "Sound in Slot 2",      "Canvas", { key ('2') } },
        { focusSlot1 + 2,    "Sound in Slot 3",      "Canvas", { key ('3') } },
        { focusSlot1 + 3,    "Sound in Slot 4",      "Canvas", { key ('4') } },
        { focusSlot1 + 4,    "Sound in Slot 5",      "Canvas", { key ('5') } },
        { focusSlot1 + 5,    "Sound in Slot 6",      "Canvas", { key ('6') } },
        { focusSlot1 + 6,    "Sound in Slot 7",      "Canvas", { key ('7') } },
        { focusSlot1 + 7,    "Sound in Slot 8",      "Canvas", { key ('8') } },
        { focusSlot1 + 8,    "Sound in Slot 9",      "Canvas", { key ('9') } },
        { focusSlot10,       "Sound in Slot 10",     "Canvas", { key ('0') } },
    };
    return rows;
}

const Spec* find (juce::CommandID id)
{
    for (const auto& s : table())
        if (s.id == id) return &s;
    return nullptr;
}

void fill (juce::ApplicationCommandInfo& info, juce::CommandID id)
{
    const auto* spec = find (id);
    if (spec == nullptr) return;
    info.setInfo (juce::String::fromUTF8 (spec->name), {}, spec->category, 0);
    for (const auto& k : spec->keys)
        info.addDefaultKeypress (k.getKeyCode(), k.getModifiers());
}

juce::String describe (const juce::KeyPress& k)
{
    juce::String s;
    const auto m = k.getModifiers();
    if (m.isCommandDown()) s << juce::String::fromUTF8 ("\xe2\x8c\x98");   // ⌘
    if (m.isShiftDown())   s << juce::String::fromUTF8 ("\xe2\x87\xa7");   // ⇧
    if (m.isAltDown())     s << juce::String::fromUTF8 ("\xe2\x8c\xa5");   // ⌥
    if (m.isCtrlDown())    s << "^";                                       // as the design writes ⌃

    const int code = k.getKeyCode();
    if      (code == KP::spaceKey)     s << juce::String::fromUTF8 ("\xe2\x90\xa3");   // ␣
    else if (code == KP::backspaceKey || code == KP::deleteKey)
                                       s << juce::String::fromUTF8 ("\xe2\x8c\xab");   // ⌫
    else if (code == KP::escapeKey)    s << "Esc";
    else if (code == KP::tabKey)       s << juce::String::fromUTF8 ("\xe2\x87\xa5");   // ⇥
    else if (code == KP::leftKey)      s << juce::String::fromUTF8 ("\xe2\x86\x90");
    else if (code == KP::rightKey)     s << juce::String::fromUTF8 ("\xe2\x86\x92");
    else if (code == KP::upKey)        s << juce::String::fromUTF8 ("\xe2\x86\x91");
    else if (code == KP::downKey)      s << juce::String::fromUTF8 ("\xe2\x86\x93");
    else                               s << juce::String::charToString ((juce::juce_wchar) juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) code));
    return s;
}

} // namespace cmd
