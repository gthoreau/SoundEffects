// CanvasComponent — the board that holds the sound Windows (Figma/Miro-style,
// System 7 skin). This increment: absolute placement, drag-to-move, and Mosaic
// auto-tile. Pan/zoom come next.
//
// Keeps its WindowComponents in sync with engine.windowIds(). UNVERIFIED against
// JUCE — build locally.

#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "WindowComponent.h"
#include "engine/Engine.h"

#include <memory>
#include <vector>

class CanvasComponent : public juce::Component
{
public:
    explicit CanvasComponent (se::Engine& engine);

    /** Reconcile WindowComponents with the engine, then repaint. Editor timer. */
    void refresh();

    /** Auto-tile all Windows into a neat grid. */
    void mosaic();

    void paint (juce::Graphics&) override;

private:
    WindowComponent* find (uint64_t id) const;
    void placeNew (WindowComponent& wc, int indexAmongNew);

    se::Engine& engine;
    std::vector<std::unique_ptr<WindowComponent>> windows;
    std::unique_ptr<se::SoundDocument> clipboard;   // shared across windows (⌘X/C/V)
    int cascade = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CanvasComponent)
};
