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
    const auto state = (! isEnabled() || greyed) ? ButtonState::disabled
                     : (down || latched) ? ButtonState::active
                     : applied       ? ButtonState::applied
                                     : ButtonState::normal;

    os8::paintButton (g, getLocalBounds(), kind, state,
                      state == ButtonState::applied ? appliedText : getButtonText(), textStyle);
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
    os8::paintMenuFrame (g, { 0.0f, 0.0f, (float) width - 3.0f, (float) height - 1.0f });
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
    // A refresh must not trample what the user is typing, nor drop the caret at 0
    // in a field that has focus: while focused, only an untouched field follows,
    // and it stays selected as a whole.
    const bool focused = editor.hasKeyboardFocus (true);
    if (focused && editor.getText() != committed)
    {
        committed = t;
        return;
    }

    committed = t;
    editor.setText (t, false);
    if (focused) editor.selectAll();
    else         clearSelection();
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
    committed = editor.getText();   // what is typed is what the step starts from
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

void Os8TextField::textEditorTextChanged (juce::TextEditor&)
{
    restyle();
    if (onTextChange) onTextChange();
}
void Os8TextField::textEditorReturnKeyPressed (juce::TextEditor&)
{
    commit();
    editor.selectAll();
    restyle();
    if (onReturn) onReturn();
}
void Os8TextField::Editor::focusLost (FocusChangeType cause)
{
    // Commit now, not in textEditorFocusLost: JUCE posts that one, so a click that
    // takes focus away and also refreshes this field (deselecting the Window the
    // tray works on) would overwrite what was typed before it was committed.
    juce::TextEditor::focusLost (cause);
    owner.commit();
    owner.clearSelection();
}

void Os8TextField::Editor::mouseDoubleClick (const juce::MouseEvent& e)
{
    // A triple click selects the whole field, as in any Mac text field (JUCE only
    // knows the double click's word).
    juce::TextEditor::mouseDoubleClick (e);
    if (e.getNumberOfClicks() >= 3) selectAll();
    owner.restyle();
}

void Os8TextField::textEditorFocusLost (juce::TextEditor&) {}

void Os8TextField::clearSelection()
{
    // JUCE keeps an unfocused editor's selection and still paints it, half
    // transparent, in the highlighted ink: the stray highlights and the white
    // text of a field that is no longer selected. A field without focus shows none.
    editor.setHighlightedRegion ({});
    editor.setCaretPosition (0);
    restyle();
}
void Os8TextField::globalFocusChanged (juce::Component*)           { restyle(); }

void Os8TextField::textEditorEscapeKeyPressed (juce::TextEditor&)
{
    editor.setText (committed, false);
    editor.selectAll();
    restyle();
    if (onEscape) onEscape();
}

os8::FieldState Os8TextField::currentState() const
{
    if (forcedState) return *forcedState;
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
    // Only a focused field shows a selection at all.
    const bool focused = editor.hasKeyboardFocus (true) || forcedState == os8::FieldState::highlighted;
    const auto selection = size == os8::FieldSize::xSmall ? os8::colour::gray600 : os8::colour::selectedTextField;
    editor.setColour (juce::TextEditor::highlightColourId, focused ? selection : juce::Colours::transparentBlack);
    editor.setColour (juce::TextEditor::highlightedTextColourId,
                      focused ? os8::fieldTextColour (size, os8::FieldState::highlighted) : ink);
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
    if (! isEnabled()) clearSelection();
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
    {
        menu.addItem (i + 1, items[i], ! disabledItems.contains (i), i == selected);
        if (separatorsAfter.contains (i)) menu.addSeparator();
    }

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
        if (result <= 0) return;
        if (result - 1 == safe->selected && safe->notifySameItem && safe->onChange)
            safe->onChange (safe->selected);   // picking what it already says still means something
        else
            safe->setSelectedIndex (result - 1, juce::sendNotification);
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

// --- Os8TransportButton -------------------------------------------------------

Os8TransportButton::Os8TransportButton (os8::icon::Id id) : juce::Button ({}), icon (id)
{
    setSize (os8::kTransportFace + 2, os8::kTransportFace + 2);
}

void Os8TransportButton::paintButton (juce::Graphics& g, bool, bool down)
{
    const auto state = ! isEnabled()              ? os8::TransportState::disabled
                     : (down || getToggleState()) ? os8::TransportState::active
                                                  : os8::TransportState::normal;
    os8::paintTransportButton (g, { 1, 1 }, icon, state);
}

// --- Os8ComboPlay -------------------------------------------------------------

namespace {
// Menu cell order, left to right, as drawn: backward, ping-pong, forward.
int cellOf (Os8ComboPlay::Direction d)
{
    switch (d)
    {
        case Os8ComboPlay::Direction::backward: return 0;
        case Os8ComboPlay::Direction::pingPong: return 1;
        case Os8ComboPlay::Direction::forward:  return 2;
    }
    return 2;
}

os8::icon::Id iconOf (Os8ComboPlay::Direction d)
{
    switch (d)
    {
        case Os8ComboPlay::Direction::backward: return os8::icon::Id::playBackward;
        case Os8ComboPlay::Direction::pingPong: return os8::icon::Id::playPingPong;
        case Os8ComboPlay::Direction::forward:  return os8::icon::Id::playForward;
    }
    return os8::icon::Id::playForward;
}
} // namespace

/** The direction menu while it is open: placed so it ends where the play face ends,
    covering the face and growing leftwards, as in the design. Modal and transient: a
    click anywhere else closes it. */
class Os8ComboPlay::DirectionMenu : public juce::Component
{
public:
    explicit DirectionMenu (Os8ComboPlay& o) : owner (o)
    {
        setSize (os8::kDirectionMenuWidth + 1, os8::kDirectionMenuHeight + 1);   // + drop shadow
        setWantsKeyboardFocus (true);
    }

    void paint (juce::Graphics& g) override
    {
        os8::paintDirectionMenu (g, {}, cellOf (owner.direction), hover);
    }

    void mouseMove (const juce::MouseEvent& e) override  { setHover (cellAt (e.x)); }
    void mouseDrag (const juce::MouseEvent& e) override  { setHover (cellAt (e.x)); }
    void mouseExit (const juce::MouseEvent&) override    { setHover (-1); }

    void mouseUp (const juce::MouseEvent& e) override
    {
        const int cell = cellAt (e.x);
        if (cell < 0 || ! getLocalBounds().contains (e.getPosition())) return;
        const Direction picked[] = { Direction::backward, Direction::pingPong, Direction::forward };
        const auto choice = picked[cell];

        // Close after this callback returns: closing deletes this component, and a
        // component must not be deleted from inside its own mouse handler.
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<Os8ComboPlay> (&owner), choice]
        {
            if (safe == nullptr) return;
            safe->closeMenu();
            safe->setDirection (choice, juce::sendNotification);
        });
    }

    void inputAttemptWhenModal() override { closeLater(); }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k.isKeyCode (juce::KeyPress::escapeKey)) { closeLater(); return true; }
        return false;
    }

private:
    void closeLater()
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<Os8ComboPlay> (&owner)]
        {
            if (safe != nullptr) safe->closeMenu();
        });
    }

    static int cellAt (int x)
    {
        return juce::isPositiveAndBelow (x, os8::kDirectionMenuWidth) ? x / os8::kDirectionCellWidth : -1;
    }

    void setHover (int h)
    {
        if (h == hover) return;
        hover = h;
        repaint();
    }

    Os8ComboPlay& owner;
    int hover = -1;
};

Os8ComboPlay::Os8ComboPlay()
{
    setSize (kWidth, kHeight);
}

Os8ComboPlay::~Os8ComboPlay()
{
    menu.reset();
}

void Os8ComboPlay::setPlaying (bool p)
{
    if (p == playing) return;
    playing = p;
    repaint();
}

void Os8ComboPlay::setDirection (Direction d, juce::NotificationType n)
{
    if (d == direction) return;
    direction = d;
    repaint();
    if (n != juce::dontSendNotification && onDirectionChange) onDirectionChange (direction);
}

void Os8ComboPlay::paint (juce::Graphics& g)
{
    const bool enabled = isEnabled();
    const auto faceState = ! enabled                    ? os8::TransportState::disabled
                         : (playing || pressingFace)    ? os8::TransportState::active
                                                        : os8::TransportState::normal;
    os8::paintTransportButton (g, { 1, 1 }, iconOf (direction), faceState);

    const auto stripState = ! enabled || ! stripEnabled ? os8::TransportState::disabled
                          : menu != nullptr ? os8::TransportState::active
                                            : os8::TransportState::normal;
    os8::paintPlayDirectionButton (g, { kStripX, 0 }, stripState);
}

void Os8ComboPlay::mouseDown (const juce::MouseEvent& e)
{
    if (! isEnabled()) return;

    if (e.x >= kStripX)
    {
        if (stripEnabled) openMenu();
        return;
    }

    pressingFace = true;
    repaint();
}

void Os8ComboPlay::mouseUp (const juce::MouseEvent& e)
{
    if (! pressingFace) return;
    pressingFace = false;
    repaint();
    if (e.x < kStripX && getLocalBounds().contains (e.getPosition()) && onPlay) onPlay();
}

void Os8ComboPlay::setDirectionMenuEnabled (bool on)
{
    if (stripEnabled == on) return;
    stripEnabled = on;
    if (! on) closeMenu();
    repaint();
}

void Os8ComboPlay::openMenu()
{
    auto* top = getTopLevelComponent();
    if (top == nullptr) return;

    menu = std::make_unique<DirectionMenu> (*this);
    // The menu's right edge sits on the play face's right edge (x = 26).
    const auto anchor = top->getLocalPoint (this, juce::Point<int> (kStripX - 1 - os8::kDirectionMenuWidth, 0));
    menu->setTopLeftPosition (anchor);
    // The menu lives on the top-level component, outside the canvas's zoomed world,
    // so it must wear the zoom the combo is drawn at (its position already follows).
    const float zoom = (float) (top->getLocalPoint (this, juce::Point<int> (1000, 0)).x
                                - top->getLocalPoint (this, juce::Point<int> (0, 0)).x) / 1000.0f;
    if (zoom > 0.0f && ! juce::approximatelyEqual (zoom, 1.0f))
    {
        // A transform scales the component's position as well as its size (it is
        // applied to the parent-space point), so seat it at anchor / zoom.
        menu->setTransform (juce::AffineTransform::scale (zoom));
        menu->setTopLeftPosition (juce::roundToInt ((float) anchor.x / zoom), juce::roundToInt ((float) anchor.y / zoom));
    }
    top->addAndMakeVisible (*menu);
    menu->enterModalState (true);
    repaint();
}

void Os8ComboPlay::closeMenu()
{
    if (menu == nullptr) return;
    menu->exitModalState (0);
    menu.reset();
    repaint();
}
