#include "CanvasComponent.h"

#include <algorithm>
#include <array>
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

// A Window revealed by focusWindow() is left at least this far inside the viewport,
// so it never lands flush against an edge (or half under a panel's shadow).
constexpr int kRevealMargin = 24;
}

juce::Colour CanvasComponent::tintFromPalette (int index) noexcept
{
    return sys7::tintFromPalette (index);   // the palette itself lives in Sys7
}

CanvasComponent::CanvasComponent (se::Engine& e) : engine (e)
{
    setWantsKeyboardFocus (true);
    addAndMakeVisible (world);
    world.setBounds (0, 0, kWorldSize, kWorldSize);
    addAndMakeVisible (rightPan);   // above the world, so it is asked first
    pan = originPan();
    applyViewTransform();

    slider.onFrame = [this] { if (carrying() && ! carry.landing) followPointer(); };
    slider.onArrived = [this] (juce::Component& c)
    {
        if (auto* wc = dynamic_cast<WindowComponent*> (&c)) wc->storeGeometry();
        if (carry.landing && &c == carry.wc.getComponent()) endCarry();
    };
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

int CanvasComponent::pickTint()
{
    // Take the colour out of the pool for as long as a Window is wearing it, so a
    // fresh Window cannot come up in a coat already on the board. Closing a Window
    // hands its colour back — otherwise a session that opened and closed a dozen
    // sounds would run out of paint.
    //
    // Past 24 open Windows (one per theme) the pool is empty by definition, so it
    // falls back to the LEAST-worn colours: with 25 open, the repeat is a colour used
    // once rather than one of a pair. Same rule, one level down, and it never has no answer.
    std::array<int, (size_t) kNumTints> worn {};
    for (uint64_t wid : engine.windowIds())
        if (auto* w = engine.windowById (wid))
            if (w->tint >= 0 && w->tint < kNumTints)
                ++worn[(size_t) w->tint];

    const int fewest = *std::min_element (worn.begin(), worn.end());

    std::vector<int> pool;
    pool.reserve ((size_t) kNumTints);
    for (int i = 0; i < kNumTints; ++i)
        if (worn[(size_t) i] == fewest) pool.push_back (i);

    // Never twice running, as long as the pool leaves a choice — two Windows made
    // back to back are the pair most likely to be compared.
    if (pool.size() > 1)
        pool.erase (std::remove (pool.begin(), pool.end(), lastTint), pool.end());

    const int pick = pool[(size_t) juce::Random::getSystemRandom().nextInt ((int) pool.size())];
    lastTint = pick;
    return pick;
}

void CanvasComponent::placeNew (WindowComponent& wc, int indexAmongNew)
{
    auto* w = engine.windowById (wc.windowId());

    // Restore a remembered size, else let the window pick its natural one.
    if (w != nullptr && w->width > 0.0f && w->height > 0.0f)
        wc.applySize ((int) w->width, (int) w->height);
    else
        wc.setSize (wc.preferredWidth(), wc.preferredHeight());

    // A coat of paint, drawn from the colours no OPEN Window is already wearing.
    if (w != nullptr)
    {
        if (w->tint < 0) w->tint = pickTint();
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
        wc->recorder = recorder;
        wc->recordSetupFor = recordSetupFor;
        wc->onClose = [this] (uint64_t wid) { engine.removeWindow (wid); };
        wc->onGeometryChanged = [this] { if (mosaicEnabled && ! carrying()) layoutGrid(); };
        auto* raw = wc.get();
        // A grow-box drag is a column-width drag: the rest of the column follows it
        // in and out. (onGeometryChanged, right behind it, does the relayout.)
        wc->onWidthChanged = [this, raw] (int w) { if (mosaicEnabled) setColumnWidth (*raw, w); };
        // Free placement: the drag IS the position, and the Window moves itself. In
        // the grid it is carried: see Reorder.h.
        wc->onDragStarted = [this, raw] { if (mosaicEnabled) beginCarry (*raw); };
        wc->onDragged = [this] (juce::Point<int>) { if (carrying() && ! carry.landing) followPointer(); };
        wc->onDropped = [this]
        {
            // Let go over the board: it lands in its gap. Anywhere else: it goes home.
            if (carrying() && ! carry.landing)
                land (getScreenBounds().contains (juce::Desktop::getMousePosition()));
        };
        wc->onSelected = [this] (uint64_t wid) { selectWindow (wid); };
        wc->onSourceRequested = [this] (uint64_t wid) { if (onSourceRequested) onSourceRequested (wid); };
        wc->recordReady = recordReady;
        world.addAndMakeVisible (*wc);
        placeNew (*wc, newCount++);
        windows.push_back (std::move (wc));
    }
    if (newCount > 0) cascade += newCount;

    // A Window came or went under a drag: the arrangement being built is gone.
    if ((newCount > 0 || removedAny) && carrying()) abortCarry();

    if (mosaicEnabled && (newCount > 0 || removedAny)) layoutGrid();

    // A Window you just made or just opened is the one you mean — selecting it
    // saves a click, and matches where your attention already is.
    if (newCount > 0)
        selectWindow (windows.back()->windowId());

    // A closed Window takes the selection with it — leaving it dangling would let
    // the effects panel offer to apply to something that no longer exists.
    if (selectedId != 0 && find (selectedId) == nullptr)
        selectWindow (0);

    // Refresh each (transport state / playhead / repaint), and keep each one's
    // colour and mixer slot in step: both can change from elsewhere (Window ▸ Color,
    // a strip dragged to another slot in the mixer).
    for (auto& w : windows)
    {
        auto* ew = engine.windowById (w->windowId());
        w->setSlotNumber (ew != nullptr && ew->slot >= 0 ? ew->slot + 1 : 0);
        if (ew != nullptr && ew->tint >= 0)
            w->setTint (tintFromPalette (ew->tint));
        w->refresh();
    }
}

void CanvasComponent::invalidateWaveform (uint64_t id)
{
    for (auto& w : windows)
        if (id == 0 || w->windowId() == id)
            w->invalidateWaveform();
}

void CanvasComponent::setRecording (se::Recorder* r, std::function<bool (uint64_t, se::RecordSetup&)> setupFor,
                                    std::function<bool (uint64_t)> ready)
{
    recorder = r;
    recordSetupFor = std::move (setupFor);
    recordReady = std::move (ready);
    for (auto& w : windows) { w->recorder = recorder; w->recordSetupFor = recordSetupFor; w->recordReady = recordReady; }
}

void CanvasComponent::selectWindow (uint64_t id)
{
    if (selectedId == id) return;
    selectedId = id;

    for (auto& w : windows)
        w->setSelected (w->windowId() == id);

    if (onSelectionChanged) onSelectionChanged();
}

void CanvasComponent::focusWindow (uint64_t id)
{
    auto* wc = find (id);
    if (wc == nullptr) return;

    selectWindow (id);   // no-op if it was already the selected one...
    wc->toFront (true);  // ...but raising and revealing are worth repeating

    // Reveal: the Window's bounds are in world coordinates, so put them through the
    // view transform to find where they actually are on screen. Only pan by as much
    // as it takes to get each edge inside — a Window already in view does not move,
    // and one taller than the viewport is aligned to its top-left rather than being
    // dragged back and forth by two contradictory clamps.
    const auto b = wc->getBounds().toFloat();
    const juce::Rectangle<float> onScreen { b.getX() * zoom + pan.x, b.getY() * zoom + pan.y,
                                            b.getWidth() * zoom, b.getHeight() * zoom };

    const float left   = (float) kRevealMargin;
    const float top    = (float) kRevealMargin;
    const float right  = (float) getWidth()  - kRevealMargin;
    const float bottom = (float) getHeight() - kRevealMargin;

    float dx = 0.0f, dy = 0.0f;
    if (onScreen.getRight()  > right)  dx = right  - onScreen.getRight();
    if (onScreen.getBottom() > bottom) dy = bottom - onScreen.getBottom();
    if (onScreen.getX() + dx < left)   dx = left - onScreen.getX();
    if (onScreen.getY() + dy < top)    dy = top  - onScreen.getY();

    if (dx != 0.0f || dy != 0.0f) setPan (pan + juce::Point<float> (dx, dy));
}

// --- layout ------------------------------------------------------------------

void CanvasComponent::mosaic()
{
    if (carrying()) abortCarry();
    mosaicEnabled = true;
    layoutGrid();
    if (onViewChanged) onViewChanged();
}

void CanvasComponent::setMosaicMode (bool shouldTile)
{
    if (shouldTile) { mosaic(); return; }
    if (carrying()) abortCarry();
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

void CanvasComponent::setDropTarget (juce::Rectangle<int> r)
{
    if (world.dropTarget == r) return;
    world.repaint (world.dropTarget.expanded (2));
    world.dropTarget = r;
    world.repaint (r.expanded (2));
}

void CanvasComponent::layoutGrid (bool animate)
{
    // Anything without a cell yet (freshly opened) gets one first.
    if (! carrying())
        for (auto& wc : windows)
            if (auto* ew = engine.windowById (wc->windowId()))
                if (ew->gridCol == se::Window::kNoCell) assignCell (*wc);

    auto* floating = carry.wc.getComponent();
    for (const auto& col : buildColumns())
    {
        int y = col.y;
        for (auto* w : col.items)
        {
            // Every window in the column is stretched to the column's width; the
            // cell is exactly as tall as the window. No padding: they touch.
            const juce::Rectangle<int> cell (col.x, y, col.width, w->getHeight());
            y += w->getHeight();
            if (w == floating) { setDropTarget (cell); continue; }

            // Only positions slide; a column that changed width snaps to it.
            w->setSize (cell.getWidth(), cell.getHeight());
            if (animate || carrying()) slider.moveTo (*w, cell.getPosition());
            else                       slider.jumpTo (*w, cell.getPosition());
            if (! slider.isMoving (*w)) w->storeGeometry();   // else once it arrives
        }
    }
}

// --- the carried Window ------------------------------------------------------

void CanvasComponent::beginCarry (WindowComponent& wc)
{
    if (carry.landing) endCarry();   // the last one settles first

    carry = {};
    carry.id = wc.windowId();
    carry.wc = &wc;
    for (auto& w : windows)
        if (auto* ew = engine.windowById (w->windowId()))
            carry.cells.push_back ({ w->windowId(), ew->gridCol, ew->gridRow });
    carry.grab = wc.getLocalPoint (nullptr, juce::Desktop::getMousePosition());

    slider.forget (wc);
    lift.apply (wc);
    setDropTarget (wc.getBounds());
    slider.setTicking (true);   // follow the pointer (and auto-scroll) every frame
}

void CanvasComponent::restoreCells()
{
    for (const auto& c : carry.cells)
        if (auto* ew = engine.windowById (c.id)) { ew->gridCol = c.col; ew->gridRow = c.row; }
}

std::vector<CanvasComponent::Spot> CanvasComponent::candidateSpots()
{
    // The grid as it would be without the carried Window: every slot between two
    // Windows of a column, above its top and below its bottom, a new column at
    // either end — and home, whatever home looks like.
    restoreCells();
    std::vector<Spot> spots { Spot {} };
    auto* ew = engine.windowById (carry.id);
    if (ew == nullptr) return spots;

    const int col = ew->gridCol;
    ew->gridCol = se::Window::kNoCell;   // sits out of buildColumns
    const auto base = buildColumns();
    ew->gridCol = col;

    for (const auto& c : base)
        for (int i = 0; i <= (int) c.items.size(); ++i)
            spots.push_back ({ Spot::intoColumn, c.key, i });
    if (! base.empty())
    {
        spots.push_back ({ Spot::newColumn, base.front().key - 1, 0 });
        spots.push_back ({ Spot::newColumn, base.back().key + 1, 0 });
    }
    return spots;
}

void CanvasComponent::applySpot (const Spot& spot)
{
    restoreCells();
    auto* wc = carry.wc.getComponent();
    if (wc == nullptr || spot.kind == Spot::home) return;
    if (spot.kind == Spot::newColumn) { setCell (*wc, spot.key, 0); return; }

    // The column without the carried Window, top to bottom.
    std::vector<std::pair<int, WindowComponent*>> order;
    for (auto& w : windows)
        if (w.get() != wc)
            if (auto* ew = engine.windowById (w->windowId()); ew != nullptr && ew->gridCol == spot.key)
                order.push_back ({ ew->gridRow, w.get() });
    std::sort (order.begin(), order.end(),
               [] (const auto& a, const auto& b) { return a.first < b.first; });

    // Rows are addresses, not offsets, so where the new one is spliced in decides
    // which way the column grows:
    //   - above the top     → one row further into the negatives; nothing else moves
    //                         and the column extends UPWARD past the origin line,
    //   - below the bottom  → one row past the last; nothing else moves,
    //   - in between        → the rows below it make room, the rows above stay put.
    const auto at = (size_t) juce::jlimit (0, (int) order.size(), spot.index);
    if (order.empty())
        setCell (*wc, spot.key, 0);
    else if (at == 0)
        setCell (*wc, spot.key, order.front().first - 1);
    else if (at == order.size())
        setCell (*wc, spot.key, order.back().first + 1);
    else
    {
        const int row = order[at].first;
        for (size_t r = at; r < order.size(); ++r)
            setCell (*order[r].second, spot.key, order[r].first + 1);
        setCell (*wc, spot.key, row);
    }
}

juce::Rectangle<int> CanvasComponent::spotRect (const Spot& spot)
{
    applySpot (spot);
    for (const auto& col : buildColumns())
    {
        int y = col.y;
        for (auto* w : col.items)
        {
            if (w == carry.wc.getComponent()) return { col.x, y, col.width, w->getHeight() };
            y += w->getHeight();
        }
    }
    return {};
}

void CanvasComponent::followPointer()
{
    auto* wc = carry.wc.getComponent();
    if (wc == nullptr) { abortCarry(); return; }

    const auto mouse = juce::Desktop::getMousePosition();

    // Near an edge of the board: scroll it, faster the deeper into the edge band
    // the pointer goes.
    const auto local = getLocalPoint (nullptr, mouse).toFloat();
    const juce::Point<float> speed { reorder::edgeSpeed (local.x, 0.0f, (float) getWidth()),
                                     reorder::edgeSpeed (local.y, 0.0f, (float) getHeight()) };
    if (! speed.isOrigin()) setPan (pan - speed);

    // The Window rides with the pointer, held where it was grabbed — and at least a
    // corner of it stays in view, so it cannot be lost off the edge.
    constexpr int kKeep = 48;
    const auto visTL = world.getLocalPoint (this, juce::Point<int>());
    const auto visBR = world.getLocalPoint (this, juce::Point<int> (getWidth(), getHeight()));
    auto topLeft = world.getLocalPoint (nullptr, mouse) - carry.grab;
    topLeft.x = juce::jlimit (visTL.x - wc->getWidth() + kKeep, juce::jmax (visTL.x, visBR.x - kKeep), topLeft.x);
    topLeft.y = juce::jlimit (visTL.y - wc->getHeight() + kKeep, juce::jmax (visTL.y, visBR.y - kKeep), topLeft.y);
    topLeft = { juce::jmax (0, topLeft.x), juce::jmax (0, topLeft.y) };
    wc->setTopLeftPosition (topLeft);

    // The gap goes to the nearest cell (centre to centre) the Window could land
    // in, and only moves on once another is clearly nearer.
    const auto centre = wc->getBounds().getCentre();
    const auto distance = [&centre] (juce::Rectangle<int> r) { return centre.getDistanceFrom (r.getCentre()); };

    const int current = distance (spotRect (carry.spot));
    Spot best = carry.spot;
    int bestDistance = current;
    for (const auto& s : candidateSpots())
        if (const int d = distance (spotRect (s)); d < bestDistance) { best = s; bestDistance = d; }

    const bool move = ! (best == carry.spot) && bestDistance + reorder::kHysteresis < current;
    if (move) carry.spot = best;
    applySpot (carry.spot);
    if (move) layoutGrid (true);
}

void CanvasComponent::land (bool commit)
{
    auto* wc = carry.wc.getComponent();
    if (wc == nullptr) { abortCarry(); return; }

    carry.landing = true;
    slider.setTicking (false);
    if (! commit) carry.spot = {};   // home, and the others back around it
    applySpot (carry.spot);
    layoutGrid (true);               // also sets the gap it lands in

    slider.moveTo (*wc, world.dropTarget.getPosition(), reorder::kDropMs);
    if (! slider.isMoving (*wc)) endCarry();   // already there
}

void CanvasComponent::endCarry()
{
    // The cells already say where it went (or, sent home, where it was).
    carry = {};
    lift.restore();
    slider.setTicking (false);
    setDropTarget ({});
    if (mosaicEnabled) layoutGrid (true);
}

void CanvasComponent::abortCarry()
{
    if (auto* wc = carry.wc.getComponent()) wc->abandonTitleDrag();
    restoreCells();
    carry = {};
    lift.restore();
    slider.setTicking (false);
    setDropTarget ({});
    if (mosaicEnabled) layoutGrid();
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

void CanvasComponent::setZoom (float newZoom)
{
    zoomAround (newZoom, getLocalBounds().getCentre().toFloat());
}

void CanvasComponent::zoomToFit (int clearBottom)
{
    juce::Rectangle<int> all;
    for (auto& w : windows)
        all = all.isEmpty() ? w->getBounds() : all.getUnion (w->getBounds());
    if (all.isEmpty()) { resetView(); return; }

    const auto inset = (float) kOriginInset;
    auto view = getLocalBounds().toFloat();
    view.removeFromBottom ((float) std::max (0, clearBottom));
    view = view.reduced (inset);
    if (view.getWidth() <= 0.0f || view.getHeight() <= 0.0f) return;

    zoom = std::clamp (std::min (view.getWidth() / (float) all.getWidth(),
                                 view.getHeight() / (float) all.getHeight()), kMinZoom, 1.0f);
    setPan ({ view.getX() - (float) all.getX() * zoom, view.getY() - (float) all.getY() * zoom });
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

void CanvasComponent::RightPan::mouseDown (const juce::MouseEvent& e)
{
    // A view gesture: the selection and the Windows are left alone.
    start = e.position;
    panAtStart = owner.pan;
}

void CanvasComponent::RightPan::mouseDrag (const juce::MouseEvent& e)
{
    owner.setPan (panAtStart + (e.position - start));
}

void CanvasComponent::World::paint (juce::Graphics& g)
{
    if (dropTarget.isEmpty()) return;
    // The open gap the carried Window would land in: an empty, lightly outlined
    // cell (PROVISIONAL, from the design system's greys; no Figma node yet).
    g.setColour (os8::colour::gray000);
    g.fillRect (dropTarget);
    g.setColour (os8::colour::gray400);
    g.drawRect (dropTarget.reduced (1), 1);
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
    if (code == juce::KeyPress::escapeKey && carrying() && ! carry.landing)
    {
        if (auto* wc = carry.wc.getComponent()) wc->abandonTitleDrag();
        land (false);   // everything glides back
        return true;
    }
    if (code == juce::KeyPress::escapeKey && panMode) { setPanMode (false); return true; }
    if (code == '0') { resetView(); return true; }
    if (code == '=' || code == '+') { zoomAround (zoom * 1.25f, getLocalBounds().getCentre().toFloat()); return true; }
    if (code == '-' || code == '_') { zoomAround (zoom / 1.25f, getLocalBounds().getCentre().toFloat()); return true; }
    return false;
}

void CanvasComponent::resized()
{
    rightPan.setBounds (getLocalBounds());
    if (mosaicEnabled) layoutGrid();
    setPan (pan);   // re-clamp against the new viewport
}

void CanvasComponent::paint (juce::Graphics& g)
{
    // The workspace: soft white, so the Windows' own grey chrome and their pastel
    // tints read as objects laid ON a surface rather than as darker patches of it.
    g.fillAll (juce::Colour (0xfffafafa));
}
