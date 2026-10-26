// EffectLayouts — where each effect's controls sit in its settings tray.
//
// The schema (effects/Schema.h) says WHAT an effect asks for; this says WHERE, and it
// is a transcription of the Figma "Effect-settings" variants (node 329:6501): every
// x, y and width below was read off the design, in the tray's own coordinates (the
// 252 px content box, 9 px in from the tray's frame). The schema keeps the logic —
// keys, ranges, units, defaults — and a cell names the key it edits.
//
// A tray is: a 19 px title row, the rows listed here, an 8 px spacer, and the 33 px
// Reset / Apply row. A layout lists only the middle.

#pragma once

#include "effects/Settings.h"

#include <set>
#include <string>
#include <vector>

namespace fxlayout {

enum class Kind
{
    label,        // static text, painted (10 pt unless `small`)
    legendStart,  // the selection-start / selection-end icons of Sample Hose's grid
    legendEnd,
    field,        // a text field bound to `key`
    units,        // the units dropdown bound to `key` (the Unit enum, 0-based)
    choice,       // a dropdown over `items`, bound to `key` (1-based, as radios are)
    setup,        // the Setup: dropdown (step 6 gives it contents)
    check,        // a checkbox bound to `key`; key "@extend" is the panel's own
    radio,        // one radio of the group `key`, standing for `value`
    slider,       // Amplify's slider, bound to `key`
    curvePreset,  // the "Linear - 100%" dropdown beside a curvature field
    notePick,     // Waveform Generator's pitch name
    octavePick,   // ...and octave, both setting `key` (Hz)
    ratePreset    // Resample Rate's presets, setting `key` (16.16 Fixed Hz)
};

enum Flag
{
    fixed16 = 1,   // the stored value is a 16.16 Fixed rate; the field shows Hz
    kilo    = 2,   // the field shows the stored value / 1000
    decimals4 = 4, // shown with 4 places (the IIR coefficients)
    large = 8      // the 26 px field (Flanger's percentage)
};

struct Cell
{
    Kind kind = Kind::label;
    float x = 0, y = 0;
    int w = 0;                   // field / dropdown width; a label with value 1 is right-aligned in [x, x + w]
    std::string text;            // label text, or a check/radio caption
    std::string key;
    std::string unitsKey;        // fields that hold a duration: the units key
    int value = 0;               // radio: what the key holds when this one is on
    int flags = 0;
    bool small = false;          // a 9 pt label
    std::string gate;            // enabled only while this boolean key is true
};

struct Row
{
    int h = 0;
    std::vector<Cell> cells;
};

struct Layout
{
    std::string title;
    std::vector<Row> rows;       // between the title row and the buttons row
    /** Is there a Reset / Apply row? The 1-click tray swaps it for its own. */
    int contentHeight() const;   // title + rows + spacer + buttons
};

/** The layout for the module called `name` ("Fade In"), or null if the design
    draws none (the panel then has nothing to show but Apply). */
const Layout* layoutFor (const std::string& moduleName);

/** The 1-click tray (543:9781) — a title, one sentence, and the buttons row. */
Layout oneClickLayout (const std::string& effectName, int warningLines);

/**
 * The values a tray opens with — the author's own, as the Effect-settings variants in
 * Figma show them. They sit on top of the module's `defaults()` (which stay what the
 * golden vectors and the recovered binary say) and only in the panel.
 *
 * Durations are written in milliseconds and converted to bytes at the target
 * Window's rate when the tray opens, so "250" means 250 ms whatever the sound is.
 */
void applyPresets (const std::string& moduleName, se::Settings&, double rateHz, int bps,
                   std::set<std::string>* touchedKeys = nullptr);

inline constexpr int kTitleH = 19;
inline constexpr int kSpacerH = 8;
inline constexpr int kButtonsH = 33;
inline constexpr int kContentW = 252;

} // namespace fxlayout
