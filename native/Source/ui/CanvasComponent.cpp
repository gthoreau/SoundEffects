#include "CanvasComponent.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr int kPad = 12;
// The world is a big fixed board; the view transform slides a viewport over it.
// Windows are kept inside it so they are never clipped away.
constexpr int kWorldSize = 20000;
}

CanvasComponent::CanvasComponent (se::Engine& e) : engine (e)
{
    setWantsKeyboardFocus (true);
    addAndMakeVisible (world);
    world.setBounds (0, 0, kWorldSize, kWorldSize);
    applyViewTransform();
}

WindowComponent* CanvasComponent::find (uint64_t id) const
{
    for (auto& w : windows)
        if (w->windowId() == id) return w.get();
    return nullptr;
}

void CanvasComponent::placeNew (WindowComponent& wc, int indexAmongNew)
{
    auto* w = engine.windowById (wc.windowId());

    // Restore a remembered size, else let the window pick its natural one.
    if (w != nullptr && w->width > 0.0f && w->height > 0.0f)
        wc.applySize ((int) w->width, (int) w->height);
    else
        wc.setSize (wc.preferredWidth(), wc.preferredHeight());

    // If the engine's Window has no placement yet, cascade it.
    if (w != nullptr && w->x == 0.0f && w->y == 0.0f)
    {
        const int off = 24 * ((cascade + indexAmongNew) % 8);
        w->x = (float) (20 + off);
        w->y = (float) (20 + off);
    }
    wc.setTopLeftPosition (w ? (int) w->x : 20, w ? (int) w->y : 20);
}

void CanvasComponent::refresh()
{
    const auto& ids = engine.windowIds();

    // Remove components whose Window is gone.
    const auto before = windows.size();
    windows.erase (std::remove_if (windows.begin(), windows.end(),
                     [&ids] (const std::unique_ptr<WindowComponent>& w)
                     { return std::find (ids.begin(), ids.end(), w->windowId()) == ids.end(); }),
                   windows.end());
    const bool removedAny = windows.size() != before;

    // Add components for new Windows.
    int newCount = 0;
    for (uint64_t id : ids)
    {
        if (find (id) != nullptr) continue;
        auto wc = std::make_unique<WindowComponent> (engine, id);
        wc->clipboard = &clipboard;
        wc->onClose = [this] (uint64_t wid) { engine.removeWindow (wid); };
        wc->onGeometryChanged = [this] { if (mosaicEnabled) layoutMasonry(); };
        wc->onUserMoved = [this]
        {
            // Placing a window by hand opts out of auto-tiling.
            if (mosaicEnabled) { mosaicEnabled = false; if (onViewChanged) onViewChanged(); }
        };
        world.addAndMakeVisible (*wc);
        placeNew (*wc, newCount++);
        windows.push_back (std::move (wc));
    }
    if (newCount > 0) cascade += newCount;

    if (mosaicEnabled && (newCount > 0 || removedAny)) layoutMasonry();

    // Refresh each (transport state / playhead / repaint).
    for (auto& w : windows)
        w->refresh();
}

// --- layout ------------------------------------------------------------------

void CanvasComponent::mosaic()
{
    mosaicEnabled = true;
    layoutMasonry();
    if (onViewChanged) onViewChanged();
}

void CanvasComponent::layoutMasonry()
{
    if (windows.empty()) return;

    // The column unit is the narrowest window, so a wide window simply spans
    // several columns instead of forcing every column to be as wide as it is.
    int colW = std::numeric_limits<int>::max();
    for (auto& w : windows) colW = std::min (colW, w->getWidth());
    colW = std::max (colW, WindowComponent::kMinWidth);

    // How many columns fit across the visible viewport, in world units.
    const int viewW = std::max (1, (int) ((float) getWidth() / zoom)) - kPad;
    const int cols = std::clamp (viewW / (colW + kPad), 1, (int) windows.size());

    // Walk the windows in engine order; each lands in the run of columns where it
    // can sit highest. That is what makes one window's new size shove the rest.
    std::vector<int> bottom ((size_t) cols, kPad);

    for (auto& w : windows)
    {
        const int span = std::clamp ((w->getWidth() + kPad + colW + kPad - 1) / (colW + kPad), 1, cols);

        int bestCol = 0, bestY = std::numeric_limits<int>::max();
        for (int c = 0; c + span <= cols; ++c)
        {
            int y = kPad;
            for (int k = c; k < c + span; ++k) y = std::max (y, bottom[(size_t) k]);
            if (y < bestY) { bestY = y; bestCol = c; }
        }

        w->setTopLeftPosition (kPad + bestCol * (colW + kPad), bestY);
        w->storeGeometry();
        for (int k = bestCol; k < bestCol + span; ++k)
            bottom[(size_t) k] = bestY + w->getHeight() + kPad;
    }
}

// --- view (zoom / pan) -------------------------------------------------------

void CanvasComponent::applyViewTransform()
{
    world.setTransform (juce::AffineTransform::scale (zoom).translated (pan.x, pan.y));
}

void CanvasComponent::setPan (juce::Point<float> newPan)
{
    // Keep the world's top-left from drifting past the viewport, so the board can
    // never be panned entirely out of sight.
    const float maxX = (float) getWidth()  - 40.0f;
    const float maxY = (float) getHeight() - 40.0f;
    const float minX = -((float) kWorldSize * zoom) + 40.0f;
    const float minY = -((float) kWorldSize * zoom) + 40.0f;
    pan = { std::clamp (newPan.x, minX, maxX), std::clamp (newPan.y, minY, maxY) };
    applyViewTransform();
}

void CanvasComponent::zoomAround (float newZoom, juce::Point<float> anchor)
{
    newZoom = std::clamp (newZoom, kMinZoom, kMaxZoom);
    if (std::abs (newZoom - zoom) < 1.0e-4f) return;

    const juce::Point<float> worldPt { (anchor.x - pan.x) / zoom, (anchor.y - pan.y) / zoom };
    zoom = newZoom;
    setPan ({ anchor.x - worldPt.x * zoom, anchor.y - worldPt.y * zoom });
    if (onViewChanged) onViewChanged();
}

void CanvasComponent::resetView()
{
    zoom = 1.0f;
    setPan ({ 0.0f, 0.0f });
    if (onViewChanged) onViewChanged();
}

void CanvasComponent::setPanMode (bool shouldPan)
{
    if (panMode == shouldPan) return;
    panMode = shouldPan;
    panDragging = false;

    // In pan mode the whole board is one big drag handle: stop the Windows from
    // taking clicks so every press lands here.
    world.setInterceptsMouseClicks (false, ! panMode);
    setMouseCursor (panMode ? juce::MouseCursor::DraggingHandCursor
                            : juce::MouseCursor::NormalCursor);
    if (onViewChanged) onViewChanged();
}

// --- input -------------------------------------------------------------------

void CanvasComponent::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    if (! panMode) return;
    panDragging = true;
    panDragStart = e.position;
    panAtDragStart = pan;
}

void CanvasComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (! panDragging) return;
    setPan (panAtDragStart + (e.position - panDragStart));
}

void CanvasComponent::mouseUp (const juce::MouseEvent&)
{
    panDragging = false;
}

void CanvasComponent::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    // Ctrl (or ⌘) + wheel zooms about the pointer; a bare wheel scrolls the board.
    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
    {
        zoomAround (zoom * std::pow (2.0f, wheel.deltaY * 1.5f), e.position);
        return;
    }

    const float step = 60.0f;
    if (e.mods.isShiftDown())
        setPan (pan + juce::Point<float> ((wheel.deltaY + wheel.deltaX) * step, 0.0f));
    else
        setPan (pan + juce::Point<float> (wheel.deltaX * step, wheel.deltaY * step));
}

void CanvasComponent::mouseMagnify (const juce::MouseEvent& e, float scaleFactor)
{
    zoomAround (zoom * scaleFactor, e.position);   // trackpad pinch
}

bool CanvasComponent::keyPressed (const juce::KeyPress& k)
{
    if (k.getModifiers().isCommandDown()) return false;   // leave ⌘-combos to the Windows

    const int code = k.getKeyCode();
    if (code == 'h' || code == 'H') { setPanMode (true);  return true; }
    if (code == 'v' || code == 'V') { setPanMode (false); return true; }
    if (code == juce::KeyPress::escapeKey && panMode) { setPanMode (false); return true; }
    if (code == '0') { resetView(); return true; }
    if (code == '=' || code == '+') { zoomAround (zoom * 1.25f, getLocalBounds().getCentre().toFloat()); return true; }
    if (code == '-' || code == '_') { zoomAround (zoom / 1.25f, getLocalBounds().getCentre().toFloat()); return true; }
    return false;
}

void CanvasComponent::resized()
{
    if (mosaicEnabled) layoutMasonry();
    setPan (pan);   // re-clamp against the new viewport
}

void CanvasComponent::paint (juce::Graphics& g)
{
    // System 7 desktop grey. (A stipple pattern can come with the chrome polish.)
    g.fillAll (juce::Colour (0xffcccccc));
}
