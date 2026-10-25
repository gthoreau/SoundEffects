#include "AppMenuBar.h"

#include "AppCommands.h"
#include "MacWindowChrome.h"
#include "Os8Icons.h"

// --- Os8AppMenuLookAndFeel -------------------------------------------------------

namespace {

// An application menu's frame is 4 px wider than its rows (2 px either side) and
// its first row starts 3 px down. JUCE lays rows from x = 0 inside a window it
// widens by the border on both sides, so: border 3, each row component spans the
// whole frame width and draws its 171 px highlight 2 px in, and the spare pixels
// at the window's right and bottom are transparent — room for the drop shadow.
constexpr int kRowInset = 2;          // highlight x offset inside the frame
constexpr int kRow = 16, kRowGap = 2;
constexpr int kSeparator = 4;         // 2 px groove + 2 px gap
constexpr int kLabelX = kRowInset + 19;
constexpr int kIconX = kRowInset + 20, kIconLabelX = kRowInset + 37;
constexpr int kRightColumn = 16;      // shortcut's right edge sits this far in
constexpr int kShortcutGap = 7;

const juce::String shortcutFor (const juce::PopupMenu::Item& item)
{
    if (auto* spec = cmd::find (item.itemID))
        if (! spec->keys.empty())
            return cmd::describe (spec->keys.front());
    return {};
}

} // namespace

struct Os8AppMenuLookAndFeelHolder : juce::DeletedAtShutdown
{
    Os8AppMenuLookAndFeel lf;
    ~Os8AppMenuLookAndFeelHolder() override { clearSingletonInstance(); }
    JUCE_DECLARE_SINGLETON_INLINE (Os8AppMenuLookAndFeelHolder, true)
};

Os8AppMenuLookAndFeel& Os8AppMenuLookAndFeel::instance()
{
    return Os8AppMenuLookAndFeelHolder::getInstance()->lf;
}

void Os8AppMenuLookAndFeel::drawPopupMenuBackgroundWithOptions (juce::Graphics& g, int width, int height,
                                                                const juce::PopupMenu::Options&)
{
    using namespace os8::colour;
    const juce::Rectangle<float> frame (0.0f, 0.0f, (float) (width - 2 * kMenuBorder),
                                        (float) height - 2.0f);
    juce::Path shape;
    shape.addRectangle (frame);

    os8::dropShadow (g, shape, 1.0f, 1.0f, black);
    g.setColour (gray100);
    g.fillPath (shape);
    os8::innerShadow (g, shape, -2.0f, -2.0f, 0, juce::Colour (0xff808080));
    os8::innerShadow (g, shape,  2.0f,  2.0f, 0, white);
    os8::strokeInside (g, frame, 0.0f, 1.0f, black);
}

void Os8AppMenuLookAndFeel::getIdealPopupMenuItemSizeWithOptions (const juce::String& text, bool isSeparator,
                                                                  int, int& idealWidth, int& idealHeight,
                                                                  const juce::PopupMenu::Options&)
{
    if (isSeparator)
    {
        idealWidth = 40;
        idealHeight = kSeparator;
        return;
    }

    // JUCE passes only text here: the label, then (when there is a shortcut) three
    // spaces and JUCE's own rendering of it. Recover the label, look its command up
    // to measure OUR rendering of the shortcut and to know whether the row carries
    // an icon, and fit the frame to the widest row, as the design's menus are.
    const auto label = text.upToFirstOccurrenceOf ("   ", false, false);
    const cmd::Spec* spec = nullptr;
    for (const auto& row : cmd::table())
        if (label == row.name) { spec = &row; break; }

    const bool hasIcon = label.startsWith ("Color:")
                      || (spec != nullptr && (spec->id == cmd::record || spec->id == cmd::stop || spec->id == cmd::play
                                              || spec->id == cmd::pause || spec->id == cmd::loop || spec->id == cmd::fastForward));
    const auto shortcut = spec != nullptr && ! spec->keys.empty() ? cmd::describe (spec->keys.front()) : juce::String();

    idealWidth = (hasIcon ? kIconLabelX : kLabelX) + os8::widthOf (label, os8::TextStyle::menuItem)
               + (shortcut.isEmpty() ? 0 : kShortcutGap + os8::widthOf (shortcut, os8::TextStyle::menuItem))
               + kRightColumn;
    idealHeight = kRow + kRowGap;
}

void Os8AppMenuLookAndFeel::drawPopupMenuItemWithOptions (juce::Graphics& g, const juce::Rectangle<int>& area,
                                                          bool isHighlighted, const juce::PopupMenu::Item& item,
                                                          const juce::PopupMenu::Options&)
{
    using namespace os8::colour;
    const int x = area.getX(), y = area.getY(), w = area.getWidth();
    const int inner = w - 2 * kRowInset;

    if (item.isSeparator)
    {
        // "Menu_separator": four half-pixel rules, blended into a 2 px groove at 1x.
        const juce::Colour rules[] = { gray200, gray400, gray500, white };
        for (int i = 0; i < 4; ++i)
        {
            g.setColour (rules[i]);
            g.fillRect (juce::Rectangle<float> ((float) (x + kRowInset), (float) y + (float) i * 0.5f,
                                                (float) inner, 0.5f));
        }
        return;
    }

    const bool enabled = item.isEnabled;
    const bool hot = isHighlighted && enabled;
    if (hot)
    {
        g.setColour (selectedMenuItem);
        g.fillRect (x + kRowInset, y, inner, kRow);
    }

    const auto ink = ! enabled ? gray400 : (hot ? white : black);

    if (item.isTicked)
    {
        g.setColour (ink);
        g.fillPath (os8::icon::outline (os8::icon::Id::menuCheckmark),
                    juce::AffineTransform::translation ((float) (x + kRowInset + 4), (float) y + 4.0f));
    }

    int labelX = kLabelX;
    if (item.image != nullptr)
    {
        // A 10 x 10 icon box; a larger drawable is scaled down to fit it.
        const juce::Rectangle<float> box ((float) (x + kIconX), (float) y + 3.0f, 10.0f, 10.0f);
        item.image->drawWithin (g, box, juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize,
                                enabled ? 1.0f : 0.4f);
        labelX = kIconLabelX;
    }

    g.setColour (ink);
    os8::drawText (g, item.text, os8::TextStyle::menuItem,
                   { (float) (x + labelX), (float) y + 2.0f, (float) (w - labelX - kRightColumn), 11.0f });

    const auto shortcut = shortcutFor (item);
    if (shortcut.isNotEmpty())
    {
        g.setColour (! enabled ? gray400 : (hot ? white : gray500));
        os8::drawText (g, shortcut, os8::TextStyle::menuItem,
                       { (float) x, (float) y + 2.0f, (float) (w - kRightColumn), 11.0f },
                       juce::Justification::right);
    }

    if (item.subMenu != nullptr)
    {
        g.setColour (ink);
        g.fillPath (os8::icon::outline (os8::icon::Id::menuArrow),
                    juce::AffineTransform::translation ((float) (x + w - kRowInset - 7), (float) y + 3.0f));
    }
}

void Os8AppMenuLookAndFeel::drawMenuBarBackground (juce::Graphics&, int, int, bool, juce::MenuBarComponent&)
{
    // The top bar paints the background; the menu component only draws its titles.
}

void Os8AppMenuLookAndFeel::drawMenuBarItem (juce::Graphics& g, int width, int height, int,
                                             const juce::String& text, bool, bool isMenuOpen,
                                             bool, juce::MenuBarComponent&)
{
    using namespace os8::colour;

    // Highlighted while its menu is open — a Mac menu title lights up when pulled
    // down, not when merely hovered.
    if (isMenuOpen)
    {
        g.setColour (selectedMenuItem);
        g.fillRect (0, 0, width, height);
    }

    g.setColour (isMenuOpen ? white : black);
    os8::drawText (g, text, os8::TextStyle::menuItem,
                   { (float) kItemPad, 10.0f, (float) (width - kItemPad), 11.0f });
}

int Os8AppMenuLookAndFeel::getMenuBarItemWidth (juce::MenuBarComponent&, int, const juce::String& text)
{
    return os8::widthOf (text, os8::TextStyle::menuItem) + 2 * kItemPad;
}

juce::Font Os8AppMenuLookAndFeel::getMenuBarFont (juce::MenuBarComponent&, int, const juce::String&)
{
    return os8::font (os8::TextStyle::menuItem);
}

// --- AppTopBar --------------------------------------------------------------------

AppTopBar::AppTopBar (juce::MenuBarModel& model, bool windowChrome)
    : chrome (windowChrome), menus (&model)
{
    menus.setLookAndFeel (&Os8AppMenuLookAndFeel::instance());
    addAndMakeVisible (menus);
}

void AppTopBar::setCanvasName (const juce::String& name)
{
    if (name == canvasName) return;
    canvasName = name;
    resized();
    repaint();
}

int AppTopBar::nameWidth() const
{
    return os8::widthOf (canvasName, os8::TextStyle::menuItem) + 2 * Os8AppMenuLookAndFeel::kItemPad;
}

void AppTopBar::resized()
{
    const int nameX = chrome ? kTrafficLightsWidth : 10;
    const int menusX = nameX + (canvasName.isEmpty() ? 0 : nameWidth());
    menus.setBounds (menusX, 1, juce::jmax (0, getWidth() - menusX), kHeight - 1);
}

void AppTopBar::paint (juce::Graphics& g)
{
    using namespace os8::colour;
    g.setColour (gray000);
    g.fillRect (0, 0, getWidth(), kHeight);

    // The bar's drop shadow: one row of 25% black just under it.
    g.setColour (black.withAlpha (0.25f));
    g.fillRect (0, kHeight, getWidth(), 1);

    if (canvasName.isNotEmpty())
    {
        // The canvas name is a bar item with a rule on each side.
        const int x = chrome ? kTrafficLightsWidth : 10;
        const int w = nameWidth();
        g.setColour (black);
        g.fillRect (x, 1, 1, kHeight - 1);
        g.fillRect (x + w - 1, 1, 1, kHeight - 1);
        os8::drawText (g, canvasName, os8::TextStyle::menuItem,
                       { (float) (x + Os8AppMenuLookAndFeel::kItemPad), 11.0f, (float) w, 11.0f });
    }
}

void AppTopBar::mouseDown (const juce::MouseEvent& e)
{
    if (chrome && e.getNumberOfClicks() == 1)
        os8::chrome::beginWindowDrag (*this);
}

void AppTopBar::mouseDoubleClick (const juce::MouseEvent&)
{
    if (chrome)
        os8::chrome::titleBarDoubleClicked (*this);
}
