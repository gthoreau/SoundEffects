// SidePanel — the frame of the Mixer and Effects panels ("Side-panels" 636:14866):
// a 270 px column, #eeeeee with a black frame, a striped title bar, and the panel's
// own content below it.
//
// The title bar carries a collapse box at its right (a provisional design, by
// request): collapsed, the panel shrinks to its title bar alone, like a window
// shade, and the canvas gets the column back. Each panel remembers its state in
// AppPrefs.

#pragma once

#include "Os8.h"

#include <functional>

class SidePanel : public juce::Component
{
public:
    /** `content` is owned by the caller and laid out inside the frame. */
    SidePanel (const juce::String& title, juce::Component& content);

    void setCollapsed (bool, juce::NotificationType = juce::sendNotification);

    /** A black rule on the title bar's last row: the top edge of the content's own
        outside stroke (the Mixer's New/Open bar), which Figma paints over the bar. */
    void setTitleRule (bool r) { titleRule = r; repaint(); }
    bool isCollapsed() const noexcept { return collapsed; }

    /** The collapse box was used; the editor re-lays the columns. */
    std::function<void (bool collapsed)> onCollapsedChanged;

    /** Height of a collapsed panel: the title bar inside the frame. */
    static constexpr int kCollapsedHeight = os8::kTitleBarHeight + 2;
    static constexpr int kWidth = 270;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    juce::Rectangle<int> titleBar() const;
    juce::Rectangle<int> collapseBox() const;

    juce::String title;
    juce::Component& content;
    bool collapsed = false;
    bool titleRule = false;
    bool pressingBox = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SidePanel)
};
