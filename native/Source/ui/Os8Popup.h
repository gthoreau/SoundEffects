// Os8Popup — "popups" (1447:9163) as a modal dialog.
//
// The frame every popup of the 0.6.0 design wears: a Mac OS 8 movable modal window.
// A #eeeeee body inside a 2 px black outline, a hard 25 % shadow two pixels down and
// right, the active title bar with its title centred, and under it the content area
// ruled in black 4 px in from the sides and the bottom. The popup's own rows start
// 31 px down; 8 px under them comes the 33 px buttons row, a secondary button
// (Cancel) beside the primary one at the right.
//
// The caller adds its controls with addContent(), in content-box coordinates measured
// from the top of the rows. Most frames are 280 px wide; their rows sit 14 or 15 px
// in (Leave Silence and the rest), which is why the inset is the caller's to name.
//
// Return presses the primary button, Escape the secondary one, and the popup blocks
// the rest of the app until one of them is pressed. It is dragged by its title bar
// and never leaves the app's window.

#pragma once

#include "Os8Widgets.h"

#include <functional>
#include <memory>
#include <vector>

class Os8Popup : public juce::Component
{
public:
    Os8Popup (const juce::String& title, int rowsHeight,
              int contentWidth = kContentW, int inset = kInset);
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
        area.translate (kStroke + inset, kStroke + kRowsTop);
        raw->setBounds (area);
        addAndMakeVisible (*raw);
        parts.push_back (std::move (comp));
        return raw;
    }

    /** A caption painted in the rows, its text box at (x, y). The design sets every
        label in Headlines (12 pt). */
    void addLabel (const juce::String& text, float x, float y,
                   os8::TextStyle = os8::TextStyle::headline);

    int contentWidth() const noexcept { return contentW; }

    /** The frame the design draws, without its outline and shadow, in the popup's
        own coordinates. */
    juce::Rectangle<int> frameBounds() const noexcept;

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
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void parentSizeChanged() override;

    static constexpr int kContentW = 252, kInset = 14;       // a 280 px frame
    static constexpr int kRowsTop = 31, kBottomSpacer = 8, kButtonsH = 33, kFoot = 4;
    static constexpr int kStroke = 2, kShadow = 2;           // both outside the frame

private:
    juce::String title;
    int rowsHeight = 0, contentW = kContentW, inset = kInset;
    void layoutButtons();
    void keepInside (juce::Point<int> wanted);
    juce::Point<int> dragFrom;      // the popup's position when the title was pressed
    bool dragging = false;

    std::unique_ptr<Os8Button> primary, secondary;
    std::vector<std::unique_ptr<juce::Component>> parts;
    struct Label { juce::String text; float x, y; os8::TextStyle style; };
    std::vector<Label> labels;
    bool dismissed = false;
    int escapeAnswer = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Os8Popup)
};
