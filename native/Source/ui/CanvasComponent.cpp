#include "CanvasComponent.h"

#include <algorithm>
#include <cmath>

CanvasComponent::CanvasComponent (se::Engine& e) : engine (e) {}

WindowComponent* CanvasComponent::find (uint64_t id) const
{
    for (auto& w : windows)
        if (w->windowId() == id) return w.get();
    return nullptr;
}

void CanvasComponent::placeNew (WindowComponent& wc, int indexAmongNew)
{
    auto* w = engine.windowById (wc.windowId());
    const int width = wc.preferredWidth();
    const int height = wc.preferredHeight();

    // If the engine's Window has no placement yet, cascade it.
    if (w != nullptr && w->x == 0.0f && w->y == 0.0f)
    {
        const int off = 24 * ((cascade + indexAmongNew) % 8);
        w->x = (float) (20 + off);
        w->y = (float) (20 + off);
    }
    const int x = w ? (int) w->x : 20;
    const int y = w ? (int) w->y : 20;
    wc.setBounds (x, y, width, height);
}

void CanvasComponent::refresh()
{
    const auto& ids = engine.windowIds();

    // Remove components whose Window is gone.
    windows.erase (std::remove_if (windows.begin(), windows.end(),
                     [&ids] (const std::unique_ptr<WindowComponent>& w)
                     { return std::find (ids.begin(), ids.end(), w->windowId()) == ids.end(); }),
                   windows.end());

    // Add components for new Windows.
    int newCount = 0;
    for (uint64_t id : ids)
    {
        if (find (id) != nullptr) continue;
        auto wc = std::make_unique<WindowComponent> (engine, id);
        wc->clipboard = &clipboard;
        wc->onClose = [this] (uint64_t wid) { engine.removeWindow (wid); };
        addAndMakeVisible (*wc);
        placeNew (*wc, newCount++);
        windows.push_back (std::move (wc));
    }
    if (newCount > 0) cascade += newCount;

    // Refresh each (transport state / playhead / repaint).
    for (auto& w : windows)
        w->refresh();
}

void CanvasComponent::mosaic()
{
    if (windows.empty()) return;

    const int cols = (int) std::ceil (std::sqrt ((double) windows.size()));
    const int pad = 12;
    int cellW = 0, cellH = 0;
    for (auto& w : windows) { cellW = std::max (cellW, w->preferredWidth()); cellH = std::max (cellH, w->preferredHeight()); }

    int i = 0;
    for (auto& w : windows)
    {
        const int r = i / cols, c = i % cols;
        const int x = pad + c * (cellW + pad);
        const int y = pad + r * (cellH + pad);
        w->setTopLeftPosition (x, y);
        if (auto* ew = engine.windowById (w->windowId())) { ew->x = (float) x; ew->y = (float) y; }
        ++i;
    }
}

void CanvasComponent::paint (juce::Graphics& g)
{
    // System 7 desktop grey. (A stipple pattern can come with the chrome polish.)
    g.fillAll (juce::Colour (0xffcccccc));
}
