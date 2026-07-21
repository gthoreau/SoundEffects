// CanvasComponent — the board that holds the sound Windows (Figma/Miro-style,
// System 7 skin).
//
// The Windows live in a child "world" layer that carries the view transform, so
// zoom and pan are one AffineTransform and every child keeps working (JUCE maps
// mouse events through the transform for us).
//
// Layout is a grid by default. The grid is virtual and unbounded: every Window
// holds a cell address (column, row) and columns are independent stacks, so
//   - a column is as wide as the widest Window in it, and every Window in that
//     column is stretched to that width,
//   - a cell is exactly as tall as the Window in it, so cells in one column have
//     whatever heights they like — rows do NOT line up across columns,
//   - nothing is padded: Windows touch, and the grid starts at the origin.
// Resizing a Window therefore reflows its whole column (and shifts the columns to
// its right). Dragging a Window re-targets its cell, with the rest reflowing live
// around the gap it will land in. The Mosaic button toggles the grid off, leaving
// Windows wherever they were dropped (free placement).
//
// Keeps its WindowComponents in sync with engine.windowIds().

#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "WindowComponent.h"
#include "engine/Engine.h"

#include <functional>
#include <memory>
#include <vector>

class CanvasComponent : public juce::Component
{
public:
    explicit CanvasComponent (se::Engine& engine);

    /** Reconcile WindowComponents with the engine, then repaint. Editor timer. */
    void refresh();

    /** Re-flow every Window into the grid and (re-)enable grid mode. */
    void mosaic();
    /** Turn the grid off — Windows stay put and drag moves them freely. */
    void setMosaicMode (bool shouldTile);
    bool isMosaicMode() const noexcept { return mosaicEnabled; }

    /** The 12 pastels new Windows are tinted from. */
    static juce::Colour tintFromPalette (int index) noexcept;
    static constexpr int kNumTints = 12;

    /** Hand-drag panning (H to enter, V to leave). */
    void setPanMode (bool shouldPan);
    bool isPanMode() const noexcept { return panMode; }

    float zoomLevel() const noexcept { return zoom; }
    void  resetView();

    static constexpr float kMinZoom = 0.2f;
    static constexpr float kMaxZoom = 4.0f;

    /** Fired when zoom / pan mode / mosaic mode change, so the toolbar can update. */
    std::function<void()> onViewChanged;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    /** Transparent layer that carries the zoom/pan transform for the Windows. */
    struct World : juce::Component
    {
        World() { setInterceptsMouseClicks (false, true); }
    };

    /** One column of the virtual grid: its Windows top-to-bottom, plus the width
        it takes (the widest desired width in it) and where it sits. */
    struct Column
    {
        std::vector<WindowComponent*> items;
        int width = 0, x = 0, height = 0;
    };

    WindowComponent* find (uint64_t id) const;
    void placeNew (WindowComponent& wc, int indexAmongNew);

    /** Gather the assigned Windows into columns, compacting the cell addresses so
        columns are 0..n-1 and rows 0..k-1 within each. */
    std::vector<Column> buildColumns();
    /** Give a Window with no cell yet one: a new column while the row of columns
        still fits the viewport, else the shortest existing column. */
    void assignCell (WindowComponent& wc);
    /** Re-target `dragged`'s cell from where it has been dragged to. */
    void retargetCell (WindowComponent& dragged, juce::Point<int> topLeft);
    /** Lay every Window out; `floating` (if any) keeps its position, but still
        takes up its slot, so the gap it would drop into stays open. */
    void layoutGrid (WindowComponent* floating = nullptr);
    void applyViewTransform();
    /** Zoom to `newZoom`, keeping the world point under `anchor` (canvas coords) put. */
    void zoomAround (float newZoom, juce::Point<float> anchor);
    void setPan (juce::Point<float> newPan);

    se::Engine& engine;
    World world;
    std::vector<std::unique_ptr<WindowComponent>> windows;
    std::unique_ptr<se::SoundDocument> clipboard;   // shared across windows (⌘X/C/V)
    int cascade = 0;

    bool mosaicEnabled = true;
    int  lastTint = -1;          // so two windows running never get the same coat
    bool panMode = false;
    bool panDragging = false;
    juce::Point<float> panDragStart, panAtDragStart;

    float zoom = 1.0f;
    juce::Point<float> pan { 0.0f, 0.0f };   // canvas px offset of the world origin

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CanvasComponent)
};
