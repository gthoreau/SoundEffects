#include "Sys7.h"

using juce::Graphics;
using juce::Rectangle;

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
