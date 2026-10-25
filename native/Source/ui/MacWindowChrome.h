// MacWindowChrome — the standalone window's title bar, folded into the app's own
// top bar (Figma "App-top-bar" 700:14311).
//
// The native title bar is hidden and the content runs to the top of the window,
// under the real traffic lights, which are moved to where the design puts them.
// Everything here is a no-op off macOS and for windows that are not native peers
// (the plugin editor lives in the host's window and never calls it).

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace os8::chrome {

/** Hide `window`'s title bar, extend its content under it, and keep the three
    traffic lights centred `barCentreY` px from the top at the given x centres —
    re-applied whenever AppKit lays the title bar out again (resize, full screen). */
void adoptAppTopBar (juce::Component& window, float barCentreY, std::initializer_list<float> buttonCentresX);

/** Start a native window drag from the current mouse-down: called by the top bar on
    its empty areas, so the window moves (and snaps, and tiles) like any Mac window. */
void beginWindowDrag (juce::Component& anyComponentInWindow);

/** What a double-click on a title bar does, per the user's system setting:
    zoom (default), minimise, or nothing. */
void titleBarDoubleClicked (juce::Component& anyComponentInWindow);

} // namespace os8::chrome
