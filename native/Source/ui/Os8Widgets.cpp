#include "Os8Widgets.h"

#include "Os8Icons.h"

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

// --- Os8LookAndFeel -----------------------------------------------------------

namespace {
// Where a menu item's parts sit, in the item component's own coordinates. JUCE
// starts items at x = 0 where Figma starts them 1 px inside the frame, hence the +1.
constexpr int kItemLeft = 1;
constexpr int kItemTextX = kItemLeft + 19;
constexpr int kItemRightColumn = 13;   // the submenu arrow's column, plus padding
} // namespace

Os8LookAndFeel::Os8LookAndFeel()
{
    // A transparent background makes JUCE give the menu a non-opaque window, which
    // is what lets the frame sit inside it with room for its drop shadow.
    setColour (juce::PopupMenu::backgroundColourId, juce::Colours::transparentBlack);
}

namespace {
struct LookAndFeelHolder : juce::DeletedAtShutdown
{
    Os8LookAndFeel lf;
    ~LookAndFeelHolder() override { clearSingletonInstance(); }
    JUCE_DECLARE_SINGLETON_INLINE (LookAndFeelHolder, true)
};
} // namespace

Os8LookAndFeel& Os8LookAndFeel::instance()
{
    return LookAndFeelHolder::getInstance()->lf;
}

void Os8LookAndFeel::drawPopupMenuBackgroundWithOptions (juce::Graphics& g, int width, int height,
                                                         const juce::PopupMenu::Options&)
{
    using namespace os8::colour;
    const juce::Rectangle<float> frame (0.0f, 0.0f, (float) width - 3.0f, (float) height - 1.0f);
    juce::Path shape;
    shape.addRectangle (frame);

    os8::dropShadow (g, shape, 1.0f, 1.0f, juce::Colour (0xff262626));
    g.setColour (gray100);
    g.fillPath (shape);
    os8::innerShadow (g, shape, 0.0f,  2.0f, 0, white);
    os8::innerShadow (g, shape, 2.0f,  0.0f, 0, white);
    os8::innerShadow (g, shape, 0.0f, -2.0f, 0, juce::Colour (0xff808080));
    os8::innerShadow (g, shape, -2.0f, 0.0f, 0, juce::Colour (0xff808080));
    os8::strokeInside (g, frame, 0.0f, 1.0f, black);
}

void Os8LookAndFeel::getIdealPopupMenuItemSizeWithOptions (const juce::String& text, bool isSeparator,
                                                           int, int& idealWidth, int& idealHeight,
                                                           const juce::PopupMenu::Options&)
{
    if (isSeparator)
    {
        idealWidth = 40;
        idealHeight = kSeparatorHeight;
        return;
    }

    idealWidth = kItemTextX + os8::widthOf (text, os8::TextStyle::smallLabel) + kItemRightColumn;
    idealHeight = kItemHeight + kItemGap;
}

void Os8LookAndFeel::drawPopupMenuItemWithOptions (juce::Graphics& g, const juce::Rectangle<int>& area,
                                                   bool isHighlighted, const juce::PopupMenu::Item& item,
                                                   const juce::PopupMenu::Options&)
{
    using namespace os8::colour;
    const int contentWidth = area.getWidth() - kItemLeft;

    if (item.isSeparator)
    {
        // "Menu_separator" 560:6713: four half-pixel rules — light, mid, dark, white —
        // which a 1x screen blends into a two-row groove.
        const float y = (float) area.getCentreY() - 1.0f;
        const juce::Colour rules[] = { gray200, gray400, gray500, white };
        for (int i = 0; i < 4; ++i)
        {
            g.setColour (rules[i]);
            g.fillRect (juce::Rectangle<float> ((float) kItemLeft + 1.0f, y + (float) i * 0.5f,
                                                (float) contentWidth - 2.0f, 0.5f));
        }
        return;
    }

    const bool enabled = item.isEnabled;
    const bool hot = isHighlighted && enabled;

    if (hot)
    {
        g.setColour (selectedMenuItem);
        g.fillRect (area.getX() + kItemLeft, area.getY(), contentWidth, kItemHeight);
    }

    const auto ink = ! enabled ? gray400 : (hot ? white : black);
    g.setColour (ink);

    if (item.isTicked)
        g.fillPath (os8::icon::outline (os8::icon::Id::menuCheckmark),
                    juce::AffineTransform::translation ((float) (area.getX() + kItemLeft + 4), (float) area.getY() + 4.0f));

    os8::drawText (g, item.text, os8::TextStyle::smallLabel,
                   { (float) (area.getX() + kItemTextX), (float) area.getY() + 2.5f,
                     (float) (contentWidth - kItemTextX - kItemRightColumn), 10.0f });

    if (item.subMenu != nullptr)
        g.fillPath (os8::icon::outline (os8::icon::Id::menuArrow),
                    juce::AffineTransform::translation ((float) (area.getX() + kItemLeft + contentWidth - 10),
                                                        (float) area.getY() + 3.0f));
}

// --- Os8TextField -------------------------------------------------------------

Os8TextField::Os8TextField (os8::FieldSize s) : size (s)
{
    setPaintingIsUnclipped (true);   // the bevel and the focus halo live outside

    const auto m = os8::metricsOf (size);
    editor.setFont (os8::font (m.style));
    editor.setMultiLine (false);
    editor.setScrollbarsShown (false);
    editor.setPopupMenuEnabled (false);
    editor.setSelectAllWhenFocused (true);
    editor.setBorder ({});

    // Put the editor's baseline where Figma's text node puts it.
    const int baseline = (int) std::round ((float) m.textTop + os8::baselineOf (m.style));
    editor.setIndents (m.textLeft, baseline - (int) std::round (os8::font (m.style).getAscent()));

    for (auto id : { juce::TextEditor::backgroundColourId, juce::TextEditor::outlineColourId,
                     juce::TextEditor::focusedOutlineColourId, juce::TextEditor::shadowColourId })
        editor.setColour (id, juce::Colours::transparentBlack);
    editor.setColour (juce::CaretComponent::caretColourId, os8::colour::black);

    editor.addListener (this);
    addAndMakeVisible (editor);
    juce::Desktop::getInstance().addFocusChangeListener (this);
    restyle();
}

Os8TextField::~Os8TextField()
{
    juce::Desktop::getInstance().removeFocusChangeListener (this);
}

void Os8TextField::setText (const juce::String& t, juce::NotificationType n)
{
    committed = t;
    editor.setText (t, false);
    if (n != juce::dontSendNotification && onCommit) onCommit (t);
    restyle();
}

void Os8TextField::setNumeric (double stepValue, int places, double minimum, double maximum)
{
    numeric = true;
    stepSize = stepValue;
    decimals = places;
    minValue = minimum;
    maxValue = maximum;
    editor.setInputRestrictions (0, "0123456789.-");
}

double Os8TextField::getValue() const
{
    return editor.getText().getDoubleValue();
}

void Os8TextField::setValue (double v, juce::NotificationType n)
{
    setText (format (juce::jlimit (minValue, maxValue, v)), n);
}

juce::String Os8TextField::format (double v) const
{
    return decimals > 0 ? juce::String (v, decimals) : juce::String (juce::roundToInt (v));
}

double Os8TextField::stepFor (Unit u) noexcept
{
    switch (u)
    {
        case Unit::seconds:   return 0.01;
        case Unit::kilohertz: return 0.1;
        case Unit::milliseconds: case Unit::samples: case Unit::bytes: case Unit::percent:
        case Unit::hertz: case Unit::bits: case Unit::count:
            return 1.0;
    }
    return 1.0;
}

int Os8TextField::decimalsFor (Unit u) noexcept
{
    switch (u)
    {
        case Unit::seconds:      return 3;
        case Unit::milliseconds: return 2;
        case Unit::kilohertz:    return 1;
        case Unit::samples: case Unit::bytes: case Unit::percent: case Unit::hertz:
        case Unit::bits: case Unit::count:
            return 0;
    }
    return 0;
}

bool Os8TextField::step (int direction, bool big)
{
    if (! numeric) return false;
    const double next = getValue() + (double) direction * stepSize * (big ? 10.0 : 1.0);
    setValue (next, juce::sendNotification);
    editor.selectAll();
    restyle();
    return true;
}

bool Os8TextField::Editor::keyPressed (const juce::KeyPress& k)
{
    const auto mods = k.getModifiers();
    if (! mods.isCommandDown() && ! mods.isAltDown() && ! mods.isCtrlDown())
    {
        if (k.getKeyCode() == juce::KeyPress::upKey   && owner.step (+1, mods.isShiftDown())) return true;
        if (k.getKeyCode() == juce::KeyPress::downKey && owner.step (-1, mods.isShiftDown())) return true;
    }

    const bool used = juce::TextEditor::keyPressed (k);
    owner.restyle();
    return used;
}

void Os8TextField::Editor::focusGained (FocusChangeType cause)
{
    juce::TextEditor::focusGained (cause);
    owner.restyle();
}

void Os8TextField::commit()
{
    if (numeric)
    {
        const auto shown = format (juce::jlimit (minValue, maxValue, getValue()));
        if (shown != editor.getText()) editor.setText (shown, false);
    }

    if (editor.getText() != committed)
    {
        committed = editor.getText();
        if (onCommit) onCommit (committed);
    }
    restyle();
}

void Os8TextField::textEditorTextChanged (juce::TextEditor&)       { restyle(); }
void Os8TextField::textEditorReturnKeyPressed (juce::TextEditor&)  { commit(); editor.selectAll(); restyle(); }
void Os8TextField::textEditorFocusLost (juce::TextEditor&)         { commit(); }
void Os8TextField::globalFocusChanged (juce::Component*)           { restyle(); }

void Os8TextField::textEditorEscapeKeyPressed (juce::TextEditor&)
{
    editor.setText (committed, false);
    editor.selectAll();
    restyle();
}

os8::FieldState Os8TextField::currentState() const
{
    if (! isEnabled()) return os8::FieldState::disabled;
    if (! editor.hasKeyboardFocus (true)) return os8::FieldState::normal;

    const auto sel = editor.getHighlightedRegion();
    const bool everything = ! editor.isEmpty() && sel.getStart() == 0 && sel.getEnd() >= editor.getTotalNumChars();
    return everything ? os8::FieldState::highlighted : os8::FieldState::active;
}

void Os8TextField::restyle()
{
    const auto state = currentState();
    const auto ink = os8::fieldTextColour (size, state);

    // The selection is drawn in the field's own Highlighted fill, so "everything
    // selected" and "the Highlighted state" are one and the same picture.
    const auto selection = size == os8::FieldSize::xSmall ? os8::colour::gray600 : os8::colour::selectedTextField;
    editor.setColour (juce::TextEditor::highlightColourId, selection);
    editor.setColour (juce::TextEditor::highlightedTextColourId, os8::fieldTextColour (size, os8::FieldState::highlighted));
    editor.applyColourToAllText (ink, true);
    editor.setColour (juce::TextEditor::textColourId, ink);

    // Unclipped painting reaches past our bounds; repaint the margin with us.
    if (auto* parent = getParentComponent())
        parent->repaint (getBoundsInParent().expanded (3));
    repaint();
}

void Os8TextField::paint (juce::Graphics& g)
{
    os8::paintTextField (g, getLocalBounds(), size, currentState());
}

void Os8TextField::resized()
{
    editor.setBounds (getLocalBounds());
}

void Os8TextField::enablementChanged()
{
    editor.setEnabled (isEnabled());
    restyle();
}

// --- Os8Dropdown --------------------------------------------------------------

Os8Dropdown::Os8Dropdown (Style s) : style (s)
{
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

void Os8Dropdown::setItems (const juce::StringArray& newItems, const juce::StringArray& newShort)
{
    items = newItems;
    shortLabels = newShort;
    if (selected >= items.size()) selected = items.isEmpty() ? -1 : 0;
    repaint();
}

void Os8Dropdown::setSelectedIndex (int index, juce::NotificationType n)
{
    if (! juce::isPositiveAndBelow (index, items.size()) || index == selected) return;
    selected = index;
    repaint();
    if (n != juce::dontSendNotification && onChange) onChange (selected);
}

int Os8Dropdown::preferredWidth() const
{
    if (style == Style::standard) return getWidth();

    int widest = 0;
    for (int i = 0; i < items.size(); ++i)
        widest = juce::jmax (widest, os8::widthOf (shortLabels[i].isNotEmpty() ? shortLabels[i] : items[i],
                                                   os8::TextStyle::xSmallLabel));
    return os8::kMiniDropdownWidth + kMiniLabelGap + widest;
}

void Os8Dropdown::paint (juce::Graphics& g)
{
    const auto state = ! isEnabled() ? os8::DropdownState::disabled
                     : menuOpen      ? os8::DropdownState::active
                                     : os8::DropdownState::normal;
    const auto text = items[selected];

    if (style == Style::standard)
    {
        os8::paintDropdown (g, getLocalBounds().withHeight (os8::kDropdownHeight), state, text);
        return;
    }

    os8::paintMiniDropdown (g, {}, state);
    g.setColour (os8::colour::black);
    const auto label = shortLabels[selected].isNotEmpty() ? shortLabels[selected] : text;
    // "ms" sits at (23, 1.5) beside the 19 px button.
    os8::drawText (g, label, os8::TextStyle::xSmallLabel,
                   { (float) (os8::kMiniDropdownWidth + kMiniLabelGap), 1.5f, 60.0f, 9.0f });
}

void Os8Dropdown::mouseDown (const juce::MouseEvent&)
{
    if (isEnabled() && ! items.isEmpty()) showMenu();
}

void Os8Dropdown::showMenu()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel (&Os8LookAndFeel::instance());
    for (int i = 0; i < items.size(); ++i)
        menu.addItem (i + 1, items[i], true, i == selected);

    menuOpen = true;
    repaint();

    auto options = juce::PopupMenu::Options().withTargetComponent (this);
    if (style == Style::standard)
        options = options.withMinimumWidth (getWidth() + 1);   // frame = control + 2

    menu.showMenuAsync (options, [safe = juce::Component::SafePointer<Os8Dropdown> (this)] (int result)
    {
        if (safe == nullptr) return;
        safe->menuOpen = false;
        safe->repaint();
        if (result > 0) safe->setSelectedIndex (result - 1, juce::sendNotification);
    });
}

// --- Os8Slider ----------------------------------------------------------------

Os8Slider::Os8Slider (os8::SliderKind k)
    : juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::NoTextBox), kind (k)
{
    setLookAndFeel (&Os8LookAndFeel::instance());
}

void Os8Slider::setLevels (float l, float r)
{
    if (juce::approximatelyEqual (l, look.levelLeft) && juce::approximatelyEqual (r, look.levelRight)) return;
    look.levelLeft = l;
    look.levelRight = r;
    repaint();
}

void Os8Slider::setMuted (bool m)            { look.muted = m; repaint(); }
void Os8Slider::setUnder (juce::Colour c)    { look.under = c; repaint(); }
void Os8Slider::setMarkerValue (double v)    { markerValue = v; repaint(); }

float Os8Slider::positionOf (double value)
{
    // The thumb's centre runs from 5.5 px to width - 5.5 px.
    const auto travel = (float) (getWidth() - os8::kThumbWidth);
    return (float) valueToProportionOfLength (value) * travel;
}

void Os8Slider::paint (juce::Graphics& g)
{
    auto l = look;
    l.thumbX = std::round (positionOf (getValue()));

    const double mid = (getMinimum() + getMaximum()) * 0.5;
    const double tick = std::isnan (markerValue) ? (kind == os8::SliderKind::volume ? -1.0 : mid) : markerValue;
    l.markerX = tick < getMinimum() ? -1.0f
                                    : std::round (positionOf (tick) + (float) os8::kThumbWidth * 0.5f) - 0.5f;
    if (kind == os8::SliderKind::pan)
        l.centreX = positionOf (mid) + (float) os8::kThumbWidth * 0.5f;

    os8::paintSlider (g, getLocalBounds().withHeight (os8::kSliderHeight), kind, l);
}

// --- Os8IconButton ------------------------------------------------------------

Os8IconButton::Os8IconButton (os8::icon::Id id) : juce::Button ({}), icon (id) {}

void Os8IconButton::paintButton (juce::Graphics& g, bool, bool down)
{
    const auto state = ! isEnabled()                  ? os8::IconButtonState::disabled
                     : (down || getToggleState())     ? os8::IconButtonState::active
                                                      : os8::IconButtonState::normal;
    os8::paintIconButton (g, getLocalBounds(), state, os8::icon::outline (icon), os8::icon::sizeOf (icon));
}

// --- Os8CanvasToggle ----------------------------------------------------------

Os8CanvasToggle::Os8CanvasToggle()
{
    setSize (os8::kCanvasToggleWidth, os8::kCanvasToggleHeight);
}

void Os8CanvasToggle::setGrid (bool g, juce::NotificationType n)
{
    if (g == grid) return;
    grid = g;
    repaint();
    if (n != juce::dontSendNotification && onChange) onChange (grid);
}

void Os8CanvasToggle::paint (juce::Graphics& g)
{
    os8::paintCanvasToggle (g, {}, grid);
}

void Os8CanvasToggle::mouseDown (const juce::MouseEvent& e)
{
    setGrid (e.x < os8::kCanvasToggleWidth / 2, juce::sendNotification);
}
