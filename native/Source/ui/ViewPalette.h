// ViewPalette — the floating strip of view controls that rides over the canvas,
// centred on it and clear of its bottom edge.
//
// It is a palette rather than a toolbar because what it drives is the VIEW, not the
// document: it belongs over the board it moves, where the eye already is, instead of
// in a corner the pointer has to travel to. Three controls, in the order you reach
// for them:
//
//   - the layout toggle: grid (Mosaic) or free placement, two icons that show the
//     two arrangements rather than naming them,
//   - the zoom slider, marked - and +, with a detent at 100%,
//   - a 100% button, the way back when the slider has wandered,
//   - and, set apart at the right, the Keyboard toggle: the module's own ICN# 128,
//     inverted while its palette is up. It is the odd one out — what it shows is an
//     effect, not a view — but it is a PALETTE toggle, and the palette it shows
//     hangs directly below this one, so this is where the hand looks for it.
//
// Drawn in the Mac OS 8 vocabulary of Os8.h, to the design's Canvas-palette frame
// (Figma 534:11202 / 636:21378): a fixed 343 x 38 frame with a soft shadow under it.

#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "Os8.h"
#include "Os8Widgets.h"

#include <functional>

class ViewPalette : public juce::Component
{
public:
    ViewPalette();

    // The palette is as wide as it likes until the canvas under it gets narrow, and
    // then it gives ground — everything but the slider is fixed, so what shrinks is
    // the travel. Past kMinWidth it stops: a slider with no throw left is not worth
    // the room it saves, and at that size it may overhang the panels a little.
    // The two numbers are the strip's own metrics; the 38 is the Keyboard toggle
    // (28) plus the gap that sets it apart (10), kept as a sum so the widths the
    // zoom cluster was designed around stay readable.
    static constexpr int kMaxWidth = 343;
    static constexpr int kMinWidth = 343;
    static constexpr int kFrameHeight = 38;
    static constexpr int kHeight = kFrameHeight + os8::kPaletteShadow;
    /** The drop shadow lives inside the bounds, so the frame stops short of them. */
    static constexpr int kShadow = os8::kPaletteShadow;

    /** The layout toggle moved: true for the grid, false for free placement. */
    std::function<void (bool)> onMosaicModeChanged;
    /** The slider (or a - / + step) asked for a new zoom factor, 1.0 being 100%. */
    std::function<void (float)> onZoomChanged;
    /** The 100% button. Separate from onZoomChanged so the canvas can treat it as
        the gesture it is rather than as a slider that happened to land on 1.0. */
    std::function<void()> onZoomReset;
    /** The Keyboard toggle was clicked; true asks for the palette to be shown. */
    std::function<void (bool)> onKeyboardToggled;

    /** Push the canvas's state in. Neither fires a callback — these are the
        readouts, and the canvas is what they read. */
    void setMosaicMode (bool grid);
    void setZoom (float zoom);
    void setKeyboardShown (bool shown);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    /** The parts that answer the pointer. The two slider ends step; the trough
        drags; the segments toggle. The 100% button is a real Os8Button. */
    enum class Part { none, grid, free, minus, plus, trough, keys };

    Part partAt (juce::Point<int> p) const;
    void setHovered (Part p);
    /** Take the zoom from a pointer x in the trough and hand it out. */
    void zoomFromDrag (int x);

    /** Zoom is worked in octaves: the slider travel is linear in log2(zoom), so a
        given nudge doubles or halves by the same amount wherever it is made — which
        is what zooming feels like from the inside. It also lands 100% just past the
        middle of the travel, where the detent is. */
    static float posToZoom (float pos) noexcept;
    static float zoomToPos (float zoom) noexcept;
    /** Where a 0..1 position sits in the trough, inset so the cap never overhangs. */
    int  xForPos (float pos) const noexcept;
    float posForX (int x) const noexcept;

    juce::Rectangle<int> body() const noexcept;

    juce::Rectangle<int> gridSeg, freeSeg, zoomLabel, minusRect, plusRect, troughRect, keysSeg;
    Os8Button resetButton { "100%", os8::ButtonKind::secondary };

    bool  mosaic = true;
    bool  keyboardShown = false;
    float zoom = 1.0f;
    bool  dragging = false;      // while true the ZOOM label reads the percentage
    Part  hovered = Part::none;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ViewPalette)
};
