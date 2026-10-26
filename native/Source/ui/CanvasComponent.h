// CanvasComponent — the board that holds the sound Windows (Figma/Miro-style,
// System 7 skin).
//
// The Windows live in a child "world" layer that carries the view transform, so
// zoom and pan are one AffineTransform and every child keeps working (JUCE maps
// mouse events through the transform for us).
//
// The board reaches equally far in EVERY direction: its origin is at the centre of
// the world layer, not at a corner, so there is as much canvas above and left of
// the first Window as there is below and right of the last.
//
// Layout is a grid by default. The grid is virtual and unbounded: every Window
// holds a cell address (column, row) and columns are independent stacks, so
//   - a column has ONE width — the last width a grow box in it asked for — and
//     every Window in it is drawn at that width,
//   - a cell is exactly as tall as the Window in it, so cells in one column have
//     whatever heights they like — rows do NOT line up across columns,
//   - nothing is padded: Windows touch, and cell (0, 0) starts at the origin.
// BOTH indices may be negative: the grid grows left of and above the origin as
// readily as right of and below it, and neither direction shunts the other along.
// Resizing a Window therefore resizes its whole column (and shifts the columns
// beside it). Dragging a Window re-targets its cell, with the rest reflowing live
// around the gap it will land in. The view palette's layout toggle turns the grid
// off, leaving Windows wherever they were dropped (free placement).
//
// Keeps its WindowComponents in sync with engine.windowIds().

#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "Sys7.h"
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

    /** The 12 pastels new Windows are tinted from (sys7's, so the mixer can wear
        them too — a strip is painted in its Window's coat). */
    static juce::Colour tintFromPalette (int index) noexcept;
    static constexpr int kNumTints = sys7::kNumTints;

    /** Hand-drag panning (H to enter, V to leave). */
    void setPanMode (bool shouldPan);
    bool isPanMode() const noexcept { return panMode; }

    float zoomLevel() const noexcept { return zoom; }
    void  resetView();
    /** Zoom about the CENTRE of the viewport, keeping the pan. What the palette's
        slider and its 100% button drive: there is no pointer to zoom about when the
        gesture happened somewhere else entirely, and the middle of the board you are
        looking at is what you meant to keep. */
    void  setZoom (float newZoom);

    /** Window ▸ Zoom to Fit: zoom out until every Window is in view (never in past
        100 %), the lot parked at the viewport's top-left inset as the grid places it.
        `clearBottom` is how much of the viewport's bottom the floating palettes hide. */
    void  zoomToFit (int clearBottom = 0);
    bool  hasWindows() const noexcept { return ! windows.empty(); }

    static constexpr float kMinZoom = 0.2f;
    static constexpr float kMaxZoom = 4.0f;

    /** Fired when zoom / pan mode / mosaic mode change, so the toolbar can update. */
    std::function<void()> onViewChanged;

    /** The Window the effects panel acts on: the last one clicked, 0 for none.
        Deliberately NOT keyboard focus — a panel with clickable controls takes
        focus, and what a panel operates on must survive that. */
    uint64_t selectedWindowId() const noexcept { return selectedId; }
    /** The selected Window's component — what the application menus act on. */
    WindowComponent* selectedWindowComponent() const { return find (selectedId); }
    void selectWindow (uint64_t id);

    /** Select a Window AND put it in front of you: raised above its neighbours (the
        visible part of that in free placement, where Windows overlap) and panned
        back into the viewport if it had been scrolled off. What the mixer does when
        you click a channel — a channel that pointed at a Window you cannot see
        would otherwise look like it did nothing. */
    void focusWindow (uint64_t id);

    /** Something rewrote a Window's audio from outside (the effects panel) — make
        it redraw its waveform rather than its cached peaks. Pass 0 for all. */
    void invalidateWaveform (uint64_t id);
    /** Selection changed, or the selected Window closed. */
    std::function<void()> onSelectionChanged;
    /** A Window's greyed record button was clicked — show the host's audio device
        settings. Null in a plugin build; the canvas just wires every Window to it. */
    std::function<void()> onAudioSettingsRequested;

    /** Is audio input live? Drives every Window's record button, and every Window
        made from here on. Cheap to call repeatedly — it returns early unchanged. */
    void setRecordEnabled (bool enabled);

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

    /** One column of the virtual grid: its Windows top-to-bottom, its virtual column
        index (`key`, negative to the left of the origin), the width it takes and
        where it sits — `y` being its top, which is above the origin line by however
        many negative rows it holds. */
    struct Column
    {
        std::vector<WindowComponent*> items;
        int key = 0;
        int width = 0, x = 0, y = 0, height = 0;
    };

    WindowComponent* find (uint64_t id) const;
    /** A palette index no open Window is wearing, at random. See the .cpp for what
        happens once there are more Windows than colours. */
    int  pickTint();
    void placeNew (WindowComponent& wc, int indexAmongNew);

    /** Gather the assigned Windows into columns and measure them. Cell addresses are
        left alone, never renumbered: they are stable, and legal on both sides of the
        origin. Cell (0, 0) is laid out AT the origin. */
    std::vector<Column> buildColumns();
    /** True if `wc` is the only Window in `col` — an end column it alone occupies
        must not spawn yet another one when it is dragged further out. */
    static bool isAloneIn (const Column& col, const WindowComponent& wc) noexcept;
    /** A Window's row address; negative rows sit above the origin line. */
    int  rowKey (const WindowComponent& wc) const;
    void setCell (WindowComponent& wc, int col, int row);
    /** Give every other Window in `source`'s column the same desired width, so a
        grow-box drag resizes the column both wider AND narrower. */
    void setColumnWidth (const WindowComponent& source, int width);
    /** Give a Window with no cell yet one: a new column while the row of columns
        still fits the viewport, else the shortest existing column. */
    void assignCell (WindowComponent& wc);
    /** Re-target `dragged`'s cell from where it has been dragged to. */
    void retargetCell (WindowComponent& dragged, juce::Point<int> topLeft);
    /** Lay every Window out; `floating` (if any) keeps its position, but still
        takes up its slot, so the gap it would drop into stays open. */
    void layoutGrid (WindowComponent* floating = nullptr);
    void applyViewTransform();
    /** The pan that parks the world origin just inside the viewport's top-left. */
    juce::Point<float> originPan() const noexcept;
    /** Zoom to `newZoom`, keeping the world point under `anchor` (canvas coords) put. */
    void zoomAround (float newZoom, juce::Point<float> anchor);
    void setPan (juce::Point<float> newPan);

    se::Engine& engine;
    World world;
    std::vector<std::unique_ptr<WindowComponent>> windows;
    std::unique_ptr<se::SoundDocument> clipboard;   // shared across windows (⌘X/C/V)
    int cascade = 0;

    uint64_t selectedId = 0;

    bool mosaicEnabled = true;
    bool recordEnabled = false;  // audio input is muted until the user says otherwise
    int  lastTint = -1;          // so two windows running never get the same coat
    bool panMode = false;
    bool panDragging = false;
    juce::Point<float> panDragStart, panAtDragStart;

    float zoom = 1.0f;
    juce::Point<float> pan { 0.0f, 0.0f };   // canvas px offset of the world origin

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CanvasComponent)
};
