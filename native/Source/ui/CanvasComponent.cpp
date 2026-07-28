#include "CanvasComponent.h"

#include <algorithm>
#include <cmath>

namespace {
// The world is a big fixed board; the view transform slides a viewport over it.
// Windows are kept inside it so they are never clipped away.
//
// The board's ORIGIN — where the grid is anchored — sits at its centre, not at its
// top-left corner: the canvas has to reach as far up and left of the first Window
// as it does down and right of the last one. Nothing is pinned to a corner.
constexpr int kWorldSize = 20000;
constexpr int kWorldOrigin = kWorldSize / 2;

// The origin is parked this far in from the viewport's top-left on a view reset,
// so the board is visibly open above and to the left of the first Window.
constexpr int kOriginInset = 48;

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
    pan = originPan();
    applyViewTransform();
}

juce::Point<float> CanvasComponent::originPan() const noexcept
{
    // Where the world origin has to land for it to sit kOriginInset in from the
    // viewport's top-left corner.
    const float o = -(float) kWorldOrigin * zoom + (float) kOriginInset;
    return { o, o };
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

    // If the engine's Window has no placement yet, cascade it from the origin.
    if (w != nullptr && w->x == 0.0f && w->y == 0.0f)
    {
        const int off = 24 * ((cascade + indexAmongNew) % 8);
        w->x = (float) (kWorldOrigin + off);
        w->y = (float) (kWorldOrigin + off);
    }
    wc.setTopLeftPosition (w ? (int) w->x : kWorldOrigin, w ? (int) w->y : kWorldOrigin);
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
        // A grow-box drag is a column-width drag: the rest of the column follows it
        // in and out. (onGeometryChanged, right behind it, does the relayout.)
        wc->onWidthChanged = [this, raw] (int w) { if (mosaicEnabled) setColumnWidth (*raw, w); };
        wc->onDragged = [this, raw] (juce::Point<int> topLeft)
        {
            if (! mosaicEnabled) return;      // free placement: the drag IS the position
            retargetCell (*raw, topLeft);
            layoutGrid (raw);                 // reflow around the gap it will drop into
        };
        wc->onDropped = [this] { if (mosaicEnabled) layoutGrid(); };
        wc->onSelected = [this] (uint64_t wid) { selectWindow (wid); };
        world.addAndMakeVisible (*wc);
        placeNew (*wc, newCount++);
        windows.push_back (std::move (wc));
    }
    if (newCount > 0) cascade += newCount;

    if (mosaicEnabled && (newCount > 0 || removedAny)) layoutGrid();

    // A Window you just made or just opened is the one you mean — selecting it
    // saves a click, and matches where your attention already is.
    if (newCount > 0)
        selectWindow (windows.back()->windowId());

    // A closed Window takes the selection with it — leaving it dangling would let
    // the effects panel offer to apply to something that no longer exists.
    if (selectedId != 0 && find (selectedId) == nullptr)
        selectWindow (0);

    // Refresh each (transport state / playhead / repaint).
    for (auto& w : windows)
        w->refresh();
}

void CanvasComponent::invalidateWaveform (uint64_t id)
{
    for (auto& w : windows)
        if (id == 0 || w->windowId() == id)
            w->invalidateWaveform();
}

void CanvasComponent::selectWindow (uint64_t id)
{
    if (selectedId == id) return;
    selectedId = id;

    for (auto& w : windows)
        w->setSelected (w->windowId() == id);

    if (onSelectionChanged) onSelectionChanged();
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
            if (ew->gridCol != se::Window::kNoCell)     // unassigned ones sit this out
                entries.push_back ({ ew->gridCol, ew->gridRow, wc.get() });

    std::stable_sort (entries.begin(), entries.end(),
                      [] (const Entry& a, const Entry& b)
                      { return a.col != b.col ? a.col < b.col : a.row < b.row; });

    std::vector<Column> columns;
    for (const auto& e : entries)
    {
        if (columns.empty() || e.col != columns.back().key)
        {
            columns.push_back ({});
            columns.back().key = e.col;
        }
        columns.back().items.push_back (e.w);
    }

    // Cell addresses — column AND row — are left exactly as they are. Nothing is
    // renumbered to 0..n-1: a negative address is perfectly legal, and keeping it is
    // what lets the grid grow left and UP out of the origin without shunting
    // everything that is already placed right and down.
    for (auto& col : columns)
    {
        for (auto* item : col.items)
        {
            // A column is one width: whatever its members ask for, they agree on it
            // (setColumnWidth keeps them in step), so the max is that width.
            col.width = std::max (col.width, item->preferredWidth());
            col.height += item->getHeight();
        }

        // Row 0 sits on the origin line; the negative rows stack upward from it, so
        // a column reaches above that line exactly as far as it has been pulled.
        col.y = kWorldOrigin;
        for (auto* item : col.items)
        {
            if (rowKey (*item) >= 0) break;
            col.y -= item->getHeight();
        }
    }

    // Place them touching, either side of the origin: column 0 starts there and the
    // negative columns stack leftwards from it. (If column 0 has been emptied, the
    // first non-negative column takes its place.)
    size_t anchor = 0;
    while (anchor < columns.size() && columns[anchor].key < 0) ++anchor;

    int x = kWorldOrigin;
    for (size_t c = anchor; c < columns.size(); ++c) { columns[c].x = x; x += columns[c].width; }
    x = kWorldOrigin;
    for (size_t c = anchor; c-- > 0; ) { x -= columns[c].width; columns[c].x = x; }
    return columns;
}

bool CanvasComponent::isAloneIn (const Column& col, const WindowComponent& wc) noexcept
{
    return col.items.size() == 1 && col.items[0] == &wc;
}

int CanvasComponent::rowKey (const WindowComponent& wc) const
{
    auto* ew = engine.windowById (wc.windowId());
    return ew != nullptr ? ew->gridRow : 0;
}

void CanvasComponent::setCell (WindowComponent& wc, int col, int row)
{
    if (auto* ew = engine.windowById (wc.windowId())) { ew->gridCol = col; ew->gridRow = row; }
}

void CanvasComponent::setColumnWidth (const WindowComponent& source, int width)
{
    auto* ew = engine.windowById (source.windowId());
    if (ew == nullptr || ew->gridCol == se::Window::kNoCell) return;

    // A grow-box drag sets the width of the COLUMN, not of one Window: every member
    // adopts it, so narrowing works exactly as widening does. Without this the
    // column would keep the width of its widest member and a narrowing drag would
    // do nothing visible.
    for (auto& wc : windows)
        if (wc.get() != &source)
            if (auto* other = engine.windowById (wc->windowId()))
                if (other->gridCol == ew->gridCol)
                    wc->setDesiredWidth (width);
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
        setCell (wc, columns.empty() ? 0 : columns.back().key + 1, 0);
        return;
    }

    size_t best = 0;
    for (size_t c = 1; c < columns.size(); ++c)
        if (columns[c].height < columns[best].height) best = c;

    setCell (wc, columns[best].key, rowKey (*columns[best].items.back()) + 1);
}

void CanvasComponent::retargetCell (WindowComponent& dragged, juce::Point<int> topLeft)
{
    auto* ew = engine.windowById (dragged.windowId());
    if (ew == nullptr) return;

    auto columns = buildColumns();
    if (columns.empty()) return;

    // Which column is the dragged corner over? Past either end means it is being
    // pulled out into a brand new column of its own — the grid grows both ways.
    size_t target = columns.size();
    for (size_t c = 0; c < columns.size(); ++c)
        if (topLeft.getX() < columns[c].x + columns[c].width) { target = c; break; }

    // In both cases: don't let a window that is already alone in the end column
    // keep spawning new ones, or it would drift outwards for ever.
    if (topLeft.getX() < columns.front().x)
    {
        if (! isAloneIn (columns.front(), dragged))
        {
            setCell (dragged, columns.front().key - 1, 0);
            return;
        }
        target = 0;
    }
    else if (target == columns.size())
    {
        if (! isAloneIn (columns.back(), dragged))
        {
            setCell (dragged, columns.back().key + 1, 0);
            return;
        }
        target = columns.size() - 1;
    }

    // Insert above the first window whose middle is below the dragged corner. The
    // scan runs over the column WITHOUT the dragged window, i.e. over the reflowed
    // positions the drop will actually produce.
    auto& col = columns[target];
    std::vector<WindowComponent*> order;
    order.reserve (col.items.size());
    for (auto* item : col.items)
        if (item != &dragged) order.push_back (item);

    size_t insert = 0;
    for (int y = col.y; insert < order.size(); ++insert)
    {
        if (topLeft.getY() < y + order[insert]->getHeight() / 2) break;
        y += order[insert]->getHeight();
    }

    // Already exactly there? Then leave every address alone. Re-keying on each
    // mouse-move of a held drag would inflate the row numbers for no visible reason.
    if (col.items.size() == order.size() + 1 && col.items[insert] == &dragged) return;

    // Rows are addresses, not offsets, so where the new one is spliced in decides
    // which way the column grows:
    //   - above the top     → one row further into the negatives; nothing else moves
    //                         and the column extends UPWARD past the origin line,
    //   - below the bottom  → one row past the last; nothing else moves,
    //   - in between        → the rows below it make room, the rows above stay put.
    if (order.empty())
        setCell (dragged, col.key, 0);
    else if (insert == 0)
        setCell (dragged, col.key, rowKey (*order.front()) - 1);
    else if (insert == order.size())
        setCell (dragged, col.key, rowKey (*order.back()) + 1);
    else
    {
        const int row = rowKey (*order[insert]);
        for (size_t r = insert; r < order.size(); ++r)
            setCell (*order[r], col.key, rowKey (*order[r]) + 1);
        setCell (dragged, col.key, row);
    }
}

void CanvasComponent::layoutGrid (WindowComponent* floating)
{
    // Anything without a cell yet (freshly opened) gets one first.
    for (auto& wc : windows)
        if (auto* ew = engine.windowById (wc->windowId()))
            if (ew->gridCol == se::Window::kNoCell) assignCell (*wc);

    for (const auto& col : buildColumns())
    {
        int y = col.y;
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
    // The board can be panned freely in every direction — the only stop is at its
    // far edges, and only so a corner of it always stays on screen. The origin is
    // at the board's centre, so that is ±kWorldSize/2 of travel each way.
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
    setPan (originPan());
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
    // This only fires for clicks that missed every Window (the world layer does
    // not intercept), so it means "click on the board" — deselect. Panning is a
    // view gesture, not a selection one, and must leave the selection alone.
    if (! panMode) selectWindow (0);
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
