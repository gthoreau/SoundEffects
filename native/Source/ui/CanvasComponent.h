// CanvasComponent — the board that holds the sound Windows (Figma/Miro-style,
// System 7 skin).
//
// The Windows live in a child "world" layer that carries the view transform, so
// zoom and pan are one AffineTransform and every child keeps working (JUCE maps
// mouse events through the transform for us).
//
// Layout is mosaic by default: Windows are packed masonry-style into columns, so
// resizing one Window displaces the ones below and beside it. Dragging a Window
// by its title bar is taken as a deliberate placement and turns mosaic off until
// the Mosaic button is pressed again.
//
// Keeps its WindowComponents in sync with engine.windowIds(). UNVERIFIED against
// JUCE — build locally.

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

    /** Re-tile every Window and (re-)enable mosaic mode. */
    void mosaic();
    bool isMosaicMode() const noexcept { return mosaicEnabled; }

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

    WindowComponent* find (uint64_t id) const;
    void placeNew (WindowComponent& wc, int indexAmongNew);
    void layoutMasonry();
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
    bool panMode = false;
    bool panDragging = false;
    juce::Point<float> panDragStart, panAtDragStart;

    float zoom = 1.0f;
    juce::Point<float> pan { 0.0f, 0.0f };   // canvas px offset of the world origin

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CanvasComponent)
};
