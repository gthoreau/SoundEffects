// Os8Icons — the design system's icons ("icons" 208:5690).
//
// Each icon is the SVG Figma exports for it, kept verbatim in Os8Icons.cpp and drawn
// through juce::Drawable, so multi-colour pixel art (the speakers, the zoom glasses,
// the trash can) comes out exactly as drawn. All of them are whole-pixel art: draw
// them at whole-pixel positions and they stay as sharp as the original bitmaps.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace os8::icon {

enum class Id
{
    record,
    stop,
    pause,
    playForward,
    playBackward,
    playPingPong,
    fastForward,
    loop,
    next,
    previous,
    sendToCanvas,
    speakerLeftOn,
    speakerLeftOff,
    speakerRightOn,
    speakerRightOff,
    zoomOut,
    zoomIn,
    zoomReset,
    zoomArrowLeft,
    zoomArrowRight,
    trashcan,
    activeTrack,
    newTrack,
    dropdownDoubleArrow,
    dropdownSingleArrow,
    selectionStart,
    selectionEnd,
    selectionLength,
    canvasGrid,
    canvasFree,
    keyboard,
    menuArrow,
    menuCheckmark,
    loopBig,          // the Loop transport button (Transport-button 761:17286)
    activeTrackDrag,  // the ghost of a Track being moved, duplicated or trashed
    newTrackDrag,     // the ghost of a Track being added
};

/** The icon's natural size, in pixels. */
juce::Point<int> sizeOf (Id);

/** Draw the icon in its own colours with its top-left at `origin`. */
void draw (juce::Graphics&, Id, juce::Point<float> origin, float opacity = 1.0f);

/** A copy of the icon as a Drawable — what a PopupMenu row takes as its image. */
std::unique_ptr<juce::Drawable> makeDrawable (Id);

/** The icon's silhouette, every colour merged — for one-colour icons that change
    colour with state (black → white when pressed, grey when disabled, red in the
    transport). Origin at the icon's top-left. */
const juce::Path& outline (Id);

} // namespace os8::icon
