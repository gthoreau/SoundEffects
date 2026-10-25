#include "SidePanel.h"

namespace {
constexpr int kBoxSize = 11;
constexpr int kBoxMargin = 6;   // from the bar's right end, as the stripes' inset
}

SidePanel::SidePanel (const juce::String& t, juce::Component& c) : title (t), content (c)
{
    addAndMakeVisible (content);
}

void SidePanel::setCollapsed (bool shouldCollapse, juce::NotificationType n)
{
    if (collapsed == shouldCollapse) return;
    collapsed = shouldCollapse;
    content.setVisible (! collapsed);
    repaint();
    if (n != juce::dontSendNotification && onCollapsedChanged) onCollapsedChanged (collapsed);
}

juce::Rectangle<int> SidePanel::titleBar() const
{
    return { 1, 1, getWidth() - 2, os8::kTitleBarHeight };
}

juce::Rectangle<int> SidePanel::collapseBox() const
{
    const auto bar = titleBar();
    return { bar.getRight() - kBoxMargin - kBoxSize, bar.getY() + 5, kBoxSize, kBoxSize };
}

void SidePanel::resized()
{
    content.setBounds (getLocalBounds().reduced (1).withTrimmedTop (os8::kTitleBarHeight));
}

void SidePanel::paint (juce::Graphics& g)
{
    using namespace os8::colour;
    g.fillAll (gray000);

    const auto bar = titleBar();
    const auto box = collapseBox();
    // Stripes run from 6 px in on the left to 6 px short of the collapse box.
    os8::paintTitleBar (g, bar, title, true, 6, bar.getRight() - box.getX() + 6);

    os8::paintTitleBarBox (g, box.getPosition(), pressingBox);

    // A window-shade glyph: the bar's own two-line mark, drawn in the box.
    g.setColour (black);
    g.fillRect (box.getX() + 2, box.getY() + 3, kBoxSize - 4, 1);
    g.fillRect (box.getX() + 2, box.getY() + 7, kBoxSize - 4, 1);

    if (titleRule && ! collapsed)
        g.fillRect (bar.withTop (bar.getBottom() - 1));   // black, from the box glyph above

    g.setColour (black);
    g.drawRect (getLocalBounds(), 1);
}

void SidePanel::mouseDown (const juce::MouseEvent& e)
{
    pressingBox = collapseBox().expanded (2).contains (e.getPosition());
    if (pressingBox) repaint (collapseBox().expanded (3));
}

void SidePanel::mouseUp (const juce::MouseEvent& e)
{
    if (! pressingBox) return;
    pressingBox = false;
    repaint (collapseBox().expanded (3));
    if (collapseBox().expanded (2).contains (e.getPosition()))
        setCollapsed (! collapsed);
}

void SidePanel::mouseDoubleClick (const juce::MouseEvent& e)
{
    // Double-clicking the title bar shades the panel, as it did a Mac OS 8 window.
    if (titleBar().contains (e.getPosition()) && ! collapseBox().expanded (2).contains (e.getPosition()))
        setCollapsed (! collapsed);
}
