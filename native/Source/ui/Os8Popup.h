// Os8Popup — "Frames / Popup" (289:2918) as a modal dialog.
//
// The frame every popup of the design wears (Leave Silence 543:8056, Select Exactly,
// ...): 270 px wide, white, a 1 px black stroke inside, a hard #808080 shadow one pixel
// down and right, and two inner bevels (#d0d0d0 up-left, #e7e7e7 down-right). Its content
// box is 252 px wide, 9 px in, and reads like an effect tray: a 19 px title row ruled in
// grey, a 14 px spacer, the popup's own rows, an 8 px spacer and the 33 px buttons row,
// a secondary button (Cancel) beside the primary one at the right.
//
// The caller adds its controls with addContent(), in content-box coordinates measured
// from the top of the rows (just under the 14 px spacer). Return presses the primary
// button, Escape the secondary one, and the popup blocks the rest of the app until one
// of them is pressed.

#pragma once

#include "Os8Widgets.h"

#include <functional>
#include <memory>
#include <vector>

class Os8Popup : public juce::Component
{
public:
    Os8Popup (const juce::String& title, int rowsHeight);
    ~Os8Popup() override;

    /** The buttons row, right to left as the design lays it out: the primary button at
        the right edge, then the secondary one 10 px to its left. */
    /** Escape (and ⌘.) answers `escapeResult`: the secondary button's 0 when that is
        Cancel, 1 when Cancel is the primary one (a destructive question). */
    Os8Button& setButtons (const juce::String& secondary, const juce::String& primary, int escapeResult = 0);
    /** Rename the primary button ("Save" → "Replace"), the row re-measured. */
    void setPrimaryText (const juce::String&);
    Os8Button* primaryButton() const noexcept   { return primary.get(); }
    Os8Button* secondaryButton() const noexcept { return secondary.get(); }

    /** Put `comp` at `area` in the rows' coordinates (x from the content box's left,
        y from the top of the rows). The popup owns it. */
    template <typename C>
    C* addContent (std::unique_ptr<C> comp, juce::Rectangle<int> area)
    {
        auto* raw = comp.get();
        area.translate (kInset, kInset + kTitleH + kTopSpacer);
        raw->setBounds (area);
        addAndMakeVisible (*raw);
        parts.push_back (std::move (comp));
        return raw;
    }

    /** A caption painted in the rows, Small Label 10 pt, its text box at (x, y). */
    void addLabel (const juce::String& text, float x, float y);

    /** Which button closed it: 1 the primary, 0 the secondary. Called once, after the
        popup has left the screen; the popup deletes itself afterwards. */
    std::function<void (int)> onDismiss;

    /** Show it modally over `over`'s window, its top-left at `topLeft` (in `over`'s
        coordinates), kept inside that window. */
    void showOver (juce::Component& over, juce::Point<int> topLeft);

    /** Close as if a button were pressed. */
    void dismiss (int result);

    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void inputAttemptWhenModal() override;

    static constexpr int kWidth = 270, kInset = 9, kContentW = 252;
    static constexpr int kTitleH = 19, kTopSpacer = 14, kBottomSpacer = 8, kButtonsH = 33;
    static constexpr int kShadow = 1;    // the hard shadow, outside the frame

private:
    juce::String title;
    int rowsHeight = 0;
    void layoutButtons();

    std::unique_ptr<Os8Button> primary, secondary;
    std::vector<std::unique_ptr<juce::Component>> parts;
    struct Label { juce::String text; float x, y; };
    std::vector<Label> labels;
    bool dismissed = false;
    int escapeAnswer = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Os8Popup)
};
