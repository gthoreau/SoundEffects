#include "CanvasComponent.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
// The world is a big fixed board; the view transform slides a viewport over it.
// Windows are kept inside it so they are never clipped away.
constexpr int kWorldSize = 20000;

// Twelve pastels, one per hue clock position: HSL(h, 55%, 84%) worked out to hex
// so individual entries can be nudged without disturbing the rest.
const juce::Colour kTints[CanvasComponent::kNumTints] = {
    juce::Colour (0xffedc0c0),   // red
    juce::Colour (0xffedd6c0),   // orange
    juce::Colour (0xffededc0),   // yellow
    juce::Colour (0xffd6edc0),   // chartreuse
    juce::Colour (0xffc0edc0),   // green
    juce::Colour (0xffc0edd6),   // spring
    juce::Colour (0xffc0eded),   // cyan
    juce::Colour (0xffc0d6ed),   // azure
    juce::Colour (0xffc0c0ed),   // blue
    juce::Colour (0xffd6c0ed),   // violet
    juce::Colour (0xffedc0ed),   // magenta
    juce::Colour (0xffedc0d6),   // rose
};
}

juce::Colour CanvasComponent::tintFromPalette (int index) noexcept
{
    return kTints[(size_t) juce::jlimit (0, kNumTints - 1, index)];
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

    // A coat of paint, picked at random but never twice running.
    if (w != nullptr)
    {
        if (w->tint < 0)
        {
            int pick = juce::Random::getSystemRandom().nextInt (kNumTints);
            if (pick == lastTint) pick = (pick + 1 + juce::Random::getSystemRandom().nextInt (kNumTints - 1)) % kNumTints;
            w->tint = pick;
            lastTint = pick;
        }
        wc.setTint (tintFromPalette (w->tint));
    }

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
        wc->onGeometryChanged = [this] { if (mosaicEnabled) layoutGrid(); };
        auto* raw = wc.get();
        wc->onDragged = [this, raw] (juce::Point<int> topLeft)
        {
            if (! mosaicEnabled) return;      // free placement: the drag IS the position
            retargetCell (*raw, topLeft);
            layoutGrid (raw);                 // reflow around the gap it will drop into
        };
        wc->onDropped = [this] { if (mosaicEnabled) layoutGrid(); };
        world.addAndMakeVisible (*wc);
        placeNew (*wc, newCount++);
        windows.push_back (std::move (wc));
    }
    if (newCount > 0) cascade += newCount;

    if (mosaicEnabled && (newCount > 0 || removedAny)) layoutGrid();

    // Refresh each (transport state / playhead / repaint).
    for (auto& w : windows)
        w->refresh();
}

// --- layout ------------------------------------------------------------------

void CanvasComponent::mosaic()
{
    mosaicEnabled = true;
    layoutGrid();
    if (onViewChanged) onViewChanged();
}

void CanvasComponent::setMosaicMode (bool shouldTile)
{
    if (shouldTile) { mosaic(); return; }
    mosaicEnabled = false;
    if (onViewChanged) onViewChanged();
}

std::vector<CanvasComponent::Column> CanvasComponent::buildColumns()
{
    struct Entry { int col, row; WindowComponent* w; };
    std::vector<Entry> entries;
    entries.reserve (windows.size());

    for (auto& wc : windows)
        if (auto* ew = engine.windowById (wc->windowId()))
            if (ew->gridCol >= 0)                       // unassigned ones sit this out
                entries.push_back ({ ew->gridCol, ew->gridRow, wc.get() });

    std::stable_sort (entries.begin(), entries.end(),
                      [] (const Entry& a, const Entry& b)
                      { return a.col != b.col ? a.col < b.col : a.row < b.row; });

    std::vector<Column> columns;
    int lastCol = std::numeric_limits<int>::min();
    for (const auto& e : entries)
    {
        if (e.col != lastCol) { columns.push_back ({}); lastCol = e.col; }
        columns.back().items.push_back (e.w);
    }

    // Compact the addresses (columns 0..n-1, rows 0..k-1) and measure each column:
    // as wide as its widest member wants to be, as tall as its members stacked.
    int x = 0;
    for (size_t c = 0; c < columns.size(); ++c)
    {
        auto& col = columns[c];
        for (size_t r = 0; r < col.items.size(); ++r)
        {
            if (auto* ew = engine.windowById (col.items[r]->windowId()))
            {
                ew->gridCol = (int) c;
                ew->gridRow = (int) r;
            }
            col.width = std::max (col.width, col.items[r]->preferredWidth());
            col.height += col.items[r]->getHeight();
        }
        col.x = x;
        x += col.width;
    }
    return columns;
}

void CanvasComponent::assignCell (WindowComponent& wc)
{
    auto* ew = engine.windowById (wc.windowId());
    if (ew == nullptr) return;

    const auto columns = buildColumns();
    int used = 0;
    for (const auto& c : columns) used += c.width;

    // Start a fresh column while the row of columns still fits across the
    // viewport; once it is full, drop into whichever column is shortest.
    const int viewW = std::max (1, (int) ((float) getWidth() / zoom));
    if (columns.empty() || used + wc.preferredWidth() <= viewW)
    {
        ew->gridCol = (int) columns.size();
        ew->gridRow = 0;
        return;
    }

    size_t best = 0;
    for (size_t c = 1; c < columns.size(); ++c)
        if (columns[c].height < columns[best].height) best = c;

    ew->gridCol = (int) best;
    ew->gridRow = (int) columns[best].items.size();
}

void CanvasComponent::retargetCell (WindowComponent& dragged, juce::Point<int> topLeft)
{
    auto* ew = engine.windowById (dragged.windowId());
    if (ew == nullptr) return;

    auto columns = buildColumns();
    if (columns.empty()) return;

    // Which column is the dragged corner over? Past the right edge means it is
    // being pulled out into a brand new column of its own.
    int target = (int) columns.size();
    for (size_t c = 0; c < columns.size(); ++c)
        if (topLeft.getX() < columns[c].x + columns[c].width) { target = (int) c; break; }

    if (target == (int) columns.size())
    {
        // Don't let a window that is already alone in the last column keep
        // spawning new ones — that would drift right for ever.
        if (! (ew->gridCol == (int) columns.size() - 1 && columns.back().items.size() == 1))
        {
            ew->gridCol = target;
            ew->gridRow = 0;
            return;
        }
        target = (int) columns.size() - 1;
    }

    // Insert above the first window whose middle is below the dragged corner.
    auto& col = columns[(size_t) target];
    int insert = 0, y = 0;
    for (auto* item : col.items)
    {
        if (item == &dragged) continue;
        if (topLeft.getY() < y + item->getHeight() / 2) break;
        y += item->getHeight();
        ++insert;
    }

    // Rebuild the column's running order with the dragged window spliced in, then
    // renumber from it — safer than nudging rows, which goes wrong when the
    // dragged window is being reordered inside its own column.
    std::vector<WindowComponent*> order;
    order.reserve (col.items.size() + 1);
    for (auto* item : col.items)
        if (item != &dragged) order.push_back (item);
    order.insert (order.begin() + insert, &dragged);

    for (size_t r = 0; r < order.size(); ++r)
        if (auto* o = engine.windowById (order[r]->windowId()))
        {
            o->gridCol = target;
            o->gridRow = (int) r;
        }
}

void CanvasComponent::layoutGrid (WindowComponent* floating)
{
    // Anything without a cell yet (freshly opened) gets one first.
    for (auto& wc : windows)
        if (auto* ew = engine.windowById (wc->windowId()))
            if (ew->gridCol < 0) assignCell (*wc);

    for (const auto& col : buildColumns())
    {
        int y = 0;
        for (auto* w : col.items)
        {
            // Every window in the column is stretched to the column's width; the
            // cell is exactly as tall as the window. No padding: they touch.
            if (w != floating) w->setBounds (col.x, y, col.width, w->getHeight());
            w->storeGeometry();
            y += w->getHeight();
        }
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
    // A trackpad / Magic Mouse sends pixel-precise deltas, which JUCE hands us
    // scaled by 0.5/256 (see juce_NSViewComponentPeer_mac.mm). Undoing that with
    // 512 makes the board track the fingers 1:1 — anything less feels like
    // dragging through treacle. A notched wheel needs a chunky fixed step instead.
    const float scale = wheel.isSmooth ? 512.0f : 1600.0f;

    // Ctrl (or ⌘) + wheel zooms about the pointer; a bare wheel scrolls the board.
    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
    {
        zoomAround (zoom * std::pow (2.0f, wheel.deltaY * (wheel.isSmooth ? 5.0f : 1.5f)), e.position);
        return;
    }

    if (e.mods.isShiftDown())
        setPan (pan + juce::Point<float> ((wheel.deltaY + wheel.deltaX) * scale, 0.0f));
    else
        setPan (pan + juce::Point<float> (wheel.deltaX * scale, wheel.deltaY * scale));
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
    if (mosaicEnabled) layoutGrid();
    setPan (pan);   // re-clamp against the new viewport
}

void CanvasComponent::paint (juce::Graphics& g)
{
    // System 7 desktop grey. (A stipple pattern can come with the chrome polish.)
    g.fillAll (juce::Colour (0xffcccccc));
}
