// Os8Widgets — the interactive controls of the Mac OS 8 vocabulary. Each one is a
// thin JUCE component around a painter in Os8.h, so the gallery and the panels draw
// from exactly the same code.

#pragma once

#include "Os8.h"

/** Buttons 87:8, as a JUCE button.

    Held down it shows Active. `flashApplied()` shows Applied ("Done!") for
    `kAppliedMs`, the spec's ~0.5 s of feedback after Apply — tied to nothing but the
    clock, because applying an effect is near-instant and the flash is there to be
    seen, not to measure anything. */
class Os8Button : public juce::Button,
                  private juce::Timer
{
public:
    explicit Os8Button (const juce::String& text, os8::ButtonKind = os8::ButtonKind::secondary);

    /** How long the Applied state stays up. One constant, to be tuned by eye. */
    static constexpr int kAppliedMs = 500;

    /** Show the Applied state (and its text) for kAppliedMs. */
    void flashApplied();

    void setAppliedText (const juce::String& t) { appliedText = t; repaint(); }
    void setKind (os8::ButtonKind k)            { kind = k; repaint(); }
    os8::ButtonKind getKind() const noexcept    { return kind; }

    bool isShowingApplied() const noexcept      { return applied; }

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    void timerCallback() override;

    os8::ButtonKind kind;
    juce::String appliedText { "Done!" };
    bool applied = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Os8Button)
};

/** Checkbox 87:1401 or Radiobutton 292:4290 with its label, laid out as every
    instance in the design system is: the 12 px control, a 4 px gap, then the label
    one pixel lower than the control's top. Radios become exclusive through JUCE's
    own radio groups (setRadioGroupId). */
class Os8Toggle : public juce::ToggleButton
{
public:
    enum class Shape { checkbox, radio };

    Os8Toggle (const juce::String& label, Shape, os8::TextStyle = os8::TextStyle::smallLabel);

    /** Width that fits the control and its whole label. */
    int preferredWidth() const;

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    static constexpr int kLabelGap = 4;

private:
    Shape shape;
    os8::TextStyle style;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Os8Toggle)
};
