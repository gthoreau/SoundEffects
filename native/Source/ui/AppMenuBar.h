// AppMenuBar — the application's top bar ("App-top-bar" 700:14311): the canvas
// name, then File / Edit / Selection / Transport / Window, drawn inside the app so
// the standalone and the plugin have the same menus.
//
// In the standalone the bar IS the window's title bar: the real traffic lights sit
// in its first 81 px (MacWindowChrome), and dragging or double-clicking its empty
// parts moves or zooms the window.

#pragma once

#include "Os8Widgets.h"

/** The look of the application menus ("Application-menu" 543:6437,
    "Application-menu-items" 569:10035) and of the bar itself.

    Unlike the dropdown menus, these have a diagonal bevel and a black drop shadow,
    18 px rows (16 + a 2 px gap), 11 pt type, a shortcut column in #666666 and an
    optional 10 px icon before the label. The shortcut text is not JUCE's: it is
    cmd::describe() of the command's key, so the menu shows exactly what the
    shortcut table binds, written as the design writes it. */
class Os8AppMenuLookAndFeel : public Os8LookAndFeel
{
public:
    static Os8AppMenuLookAndFeel& instance();

    // Popup menus.
    void drawPopupMenuBackgroundWithOptions (juce::Graphics&, int width, int height,
                                             const juce::PopupMenu::Options&) override;
    void drawPopupMenuItemWithOptions (juce::Graphics&, const juce::Rectangle<int>& area,
                                       bool isHighlighted, const juce::PopupMenu::Item&,
                                       const juce::PopupMenu::Options&) override;
    void getIdealPopupMenuItemSizeWithOptions (const juce::String& text, bool isSeparator,
                                               int standardMenuItemHeight, int& idealWidth,
                                               int& idealHeight, const juce::PopupMenu::Options&) override;
    juce::Font getPopupMenuFont() override { return os8::font (os8::TextStyle::menuItem); }
    int getPopupMenuBorderSizeWithOptions (const juce::PopupMenu::Options&) override { return kMenuBorder; }

    /** Rows start 3 px into the frame (the dropdown menus' border is 2). */
    static constexpr int kMenuBorder = 3;

    // The bar.
    void drawMenuBarBackground (juce::Graphics&, int width, int height, bool isMouseOverBar,
                                juce::MenuBarComponent&) override;
    void drawMenuBarItem (juce::Graphics&, int width, int height, int itemIndex,
                          const juce::String& itemText, bool isMouseOverItem, bool isMenuOpen,
                          bool isMouseOverBar, juce::MenuBarComponent&) override;
    int getMenuBarItemWidth (juce::MenuBarComponent&, int itemIndex, const juce::String& itemText) override;
    juce::Font getMenuBarFont (juce::MenuBarComponent&, int itemIndex, const juce::String& itemText) override;

    static constexpr int kItemPad = 8;   // left and right of each bar label

private:
    friend struct Os8AppMenuLookAndFeelHolder;
    Os8AppMenuLookAndFeel() = default;
};

class AppTopBar : public juce::Component
{
public:
    /** `windowChrome`: this bar is the standalone window's title bar (traffic lights
        to its left, drags and double-clicks move the window). False in a plugin. */
    AppTopBar (juce::MenuBarModel&, bool windowChrome);

    void setCanvasName (const juce::String&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    /** The bar's own height; it paints a one-pixel shadow just below it, so the
        component is one pixel taller. */
    static constexpr int kHeight = 32;
    static constexpr int kTrafficLightsWidth = 81;

private:
    int nameWidth() const;

    bool chrome;
    juce::String canvasName;
    juce::MenuBarComponent menus;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AppTopBar)
};
