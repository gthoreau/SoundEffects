// Os8Icons — the design system's icons ("icons" 208:5690), as paths.
//
// Each is the SVG Figma exports for the icon, at its natural size with its origin
// at the icon's top-left. Most are pixel art (whole-pixel steps), so place them at
// whole-pixel positions and they stay as sharp as the original bitmaps were.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace os8::icon {

const juce::Path& dropdownDoubleArrow();   //  7 x 10 — the popup's up/down arrows
const juce::Path& dropdownSingleArrow();   //  7 x  4 — the mini-dropdown's arrow
const juce::Path& menuArrow();             //  6 x 11 — submenu
const juce::Path& menuCheckmark();         //  9 x  8 — ticked menu item

} // namespace os8::icon
