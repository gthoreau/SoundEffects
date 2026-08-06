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
//   - a 100% button, the way back when the slider has wandered.
//
// Drawn in the System 7 vocabulary of Sys7.h — flat face, 1px black frames, weight
// for hover — plus the solid black drop shadow System 7 gave the things that float
// above a window rather than sit in one.

#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "Sys7.h"

#include <functional>

class ViewPalette : public juce::Component
{
public:
    ViewPalette();

    // The palette is as wide as it likes until the canvas under it gets narrow, and
    // then it gives ground — everything but the slider is fixed, so what shrinks is
    // the travel. Past kMinWidth it stops: a slider with no throw left is not worth
    // the room it saves, and at that size it may overhang the panels a little.
    static constexpr int kMaxWidth = 336;
    static constexpr int kMinWidth = 260;
    static constexpr int kHeight = 38;
    /** The drop shadow lives inside the bounds, so the frame stops short of them. */
    static constexpr int kShadow = 3;

    /** The layout toggle moved: true for the grid, false for free placement. */
    std::function<void (bool)> onMosaicModeChanged;
    /** The slider (or a - / + step) asked for a new zoom factor, 1.0 being 100%. */
    std::function<void (float)> onZoomChanged;
    /** The 100% button. Separate from onZoomChanged so the canvas can treat it as
        the gesture it is rather than as a slider that happened to land on 1.0. */
    std::function<void()> onZoomReset;

    /** Push the canvas's state in. Neither fires a callback — these are the
        readouts, and the canvas is what they read. */
    void setMosaicMode (bool grid);
    void setZoom (float zoom);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    /** The parts that answer the pointer. The two slider ends step; the trough
        drags; the segments toggle. The 100% button is a real Sys7Button. */
    enum class Part { none, grid, free, minus, plus, trough };

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

    juce::Rectangle<int> gridSeg, freeSeg, zoomLabel, minusRect, plusRect, troughRect;
    Sys7Button resetButton { "100%" };

    bool  mosaic = true;
    float zoom = 1.0f;
    bool  dragging = false;      // while true the ZOOM label reads the percentage
    Part  hovered = Part::none;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ViewPalette)
};
