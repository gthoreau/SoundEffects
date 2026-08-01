#include "Sys7.h"

using juce::Graphics;
using juce::Rectangle;

namespace {
// HSL(h, 55%, 84%) worked out to hex so individual entries can be nudged without
// disturbing the rest.
const juce::Colour kTints[sys7::kNumTints] = {
    juce::Colour (0xffedc0c0),   // red
    juce::Colour (0xffedd6c0),   // orange
    juce::Colour (0xffededc0),   // yellow
    juce::Colour (0xffd6edc0),   // chartreuse
    juce::Colour (0xffc0edc0),   // green
    juce::Colour (0xffc0edd6),   // spring
    juce::Colour (0xffc0eded),   // cyan
    juce::Colour (0xffc0d6ed),   // azure
    juce::Colour (0xffc0c0ed),   // blue
    juce::Colour (0xffd6c0ed),   // violet
    juce::Colour (0xffedc0ed),   // magenta
    juce::Colour (0xffedc0d6),   // rose
};
}

juce::Colour sys7::tintFromPalette (int index) noexcept
{
    return kTints[(size_t) juce::jlimit (0, kNumTints - 1, index)];
}

void Sys7Button::paintButton (Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool on = isEnabled();

    g.setColour (down ? sys7::kInk : sys7::kFace);
    g.fillRoundedRectangle (r, 3.0f);

    // Weight carries hover, exactly as the Windows' borders do on the canvas.
    g.setColour (on ? sys7::kInk : sys7::kDim);
    g.drawRoundedRectangle (r, 3.0f, over && ! down && on ? 2.0f : 1.0f);

    g.setColour (down ? sys7::kWhite : (on ? sys7::kInk : sys7::kDim));
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred, false);
}

void Sys7LookAndFeel::drawScrollbar (Graphics& g, juce::ScrollBar&, int x, int y, int w, int h,
                                     bool isVertical, int thumbStart, int thumbSize,
                                     bool isMouseOver, bool)
{
    const Rectangle<int> track (x, y, w, h);
    g.setColour (sys7::kWhite);
    g.fillRect (track);
    g.setColour (sys7::kInk);
    g.drawRect (track, 1);

    if (thumbSize <= 0) return;

    const auto thumb = (isVertical ? Rectangle<int> (x, thumbStart, w, thumbSize)
                                   : Rectangle<int> (thumbStart, y, thumbSize, h)).reduced (1);
    g.setColour (sys7::kFace);
    g.fillRect (thumb);
    g.setColour (sys7::kInk);
    g.drawRect (thumb, isMouseOver ? 2 : 1);
}
