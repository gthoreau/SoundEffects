// Os8Gallery — every Os8 building block on one page, in every state.
//
// A workbench, not a feature: it is how each piece of the design system gets
// checked against its Figma component before any panel is built from it. Launch the
// standalone with `--gallery` to get it in a window of its own, or with
// `--gallery-snapshot <file.png>` to render it to an image and quit.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class Os8Gallery : public juce::Component
{
public:
    Os8Gallery();

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Render the gallery off-screen at `scale` and write it as a PNG. */
    static bool writeSnapshot (const juce::File& png, float scale);

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Os8Gallery)
};
