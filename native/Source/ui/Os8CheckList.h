// Os8CheckList — the ticked list of the popups (Import Sound 1447:9162, Send to
// Canvas 1447:9158): a white field of rows 23⅓ px apart, each a checkbox, a name and
// right-aligned facts, with a 5 px navy scroll thumb when they do not all fit. A name
// reads black while ticked and grey while not.

#pragma once

#include "Os8Widgets.h"

#include <functional>
#include <memory>
#include <vector>

class Os8CheckList : public juce::Component
{
public:
    struct Item
    {
        juce::String name;
        juce::StringArray facts;        // one per column
        bool ticked = false;
    };

    /** The rows' pitch in Import Sound and Send to Canvas; Audio/Midi Setup's lists
        are set closer. */
    static constexpr float kPitch = 70.0f / 3.0f;

    /** `factRights` gives each fact column's right edge, from the list's left. */
    Os8CheckList (std::vector<Item>, std::vector<int> factRights, float rowPitch = kPitch);
    ~Os8CheckList() override;

    int size() const noexcept { return (int) rows.size(); }
    bool isTicked (int) const;
    int numTicked() const;
    void setTicked (int, bool);
    void setAllTicked (bool);
    std::vector<int> ticked() const;

    /** A row was clicked. */
    std::function<void()> onChange;
    std::function<void (int row)> onRowClicked;

    /** The height that shows `rows` rows whole, no more than `maximum`. */
    static int heightFor (int rows, int maximum) noexcept;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    class Row;
    float contentHeight() const noexcept;
    float maxScroll() const noexcept;
    juce::Rectangle<float> thumb() const noexcept;      // empty when all rows fit
    void scrollTo (float);

    juce::Component clip;                // inside the field's frame
    std::vector<std::unique_ptr<Row>> rows;
    float pitch = kPitch;
    float scroll = 0.0f, dragFrom = 0.0f;
    bool draggingThumb = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Os8CheckList)
};
