#include "Os8Widgets.h"

// --- Os8Button ----------------------------------------------------------------

Os8Button::Os8Button (const juce::String& text, os8::ButtonKind k)
    : juce::Button (text), kind (k)
{
    setButtonText (text);
}

void Os8Button::flashApplied()
{
    applied = true;
    repaint();
    startTimer (kAppliedMs);
}

void Os8Button::timerCallback()
{
    stopTimer();
    applied = false;
    repaint();
}

void Os8Button::paintButton (juce::Graphics& g, bool, bool down)
{
    using os8::ButtonState;
    const auto state = ! isEnabled() ? ButtonState::disabled
                     : down          ? ButtonState::active
                     : applied       ? ButtonState::applied
                                     : ButtonState::normal;

    os8::paintButton (g, getLocalBounds(), kind, state,
                      state == ButtonState::applied ? appliedText : getButtonText());
}

// --- Os8Toggle ----------------------------------------------------------------

Os8Toggle::Os8Toggle (const juce::String& label, Shape s, os8::TextStyle st)
    : juce::ToggleButton (label), shape (s), style (st)
{
    // A radio that is clicked while on stays on: that is what a radio group means.
    if (shape == Shape::radio)
        setClickingTogglesState (true);
}

int Os8Toggle::preferredWidth() const
{
    const auto text = getButtonText();
    return os8::kToggleSize + (text.isEmpty() ? 0 : kLabelGap + os8::widthOf (text, style));
}

void Os8Toggle::paintButton (juce::Graphics& g, bool, bool)
{
    const float top = std::floor ((float) (getHeight() - os8::kToggleSize) * 0.5f);
    const bool on = getToggleState();
    const bool enabled = isEnabled();

    if (shape == Shape::checkbox) os8::paintCheckbox (g, { 0.0f, top }, on, enabled);
    else                          os8::paintRadio    (g, { 0.0f, top }, on, enabled);

    if (getButtonText().isNotEmpty())
    {
        g.setColour (enabled ? os8::colour::black : os8::colour::gray400);
        os8::drawText (g, getButtonText(), style,
                       { (float) (os8::kToggleSize + kLabelGap), top + 1.0f,
                         (float) getWidth(), os8::lineHeightOf (style) });
    }
}
