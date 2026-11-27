#include "Os8Popup.h"

namespace colour = os8::colour;

namespace {

/** Buttons 87:8 measure their text plus a margin: Cancel is 56 wide, Apply 53 (the
    primary's smallest, which its ring needs). */
int buttonWidth (const juce::String& text, os8::ButtonKind kind)
{
    const int w = os8::widthOf (text, os8::TextStyle::headline);
    return kind == os8::ButtonKind::primary ? std::max (53, w + 18) : w + 12;
}

} // namespace

Os8Popup::Os8Popup (const juce::String& t, int rows, int width, int in)
    : title (t), rowsHeight (rows), contentW (width), inset (in)
{
    const int h = kRowsTop + rowsHeight + kBottomSpacer + kButtonsH + kFoot;
    setSize (contentW + 2 * inset + 2 * kStroke + kShadow, h + 2 * kStroke + kShadow);
    setWantsKeyboardFocus (true);
    // A press on the title bar or between the controls leaves the caret where it is.
    setMouseClickGrabsKeyboardFocus (false);
}

Os8Popup::~Os8Popup() = default;

Os8Button& Os8Popup::setButtons (const juce::String& secondaryText, const juce::String& primaryText, int escapeResult)
{
    escapeAnswer = escapeResult;
    primary = std::make_unique<Os8Button> (primaryText, os8::ButtonKind::primary);
    primary->onClick = [this] { dismiss (1); };
    secondary.reset();
    if (secondaryText.isNotEmpty())
    {
        secondary = std::make_unique<Os8Button> (secondaryText, os8::ButtonKind::secondary);
        secondary->onClick = [this] { dismiss (0); };
    }

    for (auto* b : { primary.get(), secondary.get() })
    {
        if (b == nullptr) continue;
        b->setWantsKeyboardFocus (false);
        b->setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }
    layoutButtons();
    return *primary;
}

void Os8Popup::setPrimaryText (const juce::String& text)
{
    if (primary == nullptr || primary->getButtonText() == text) return;
    primary->setButtonText (text);
    layoutButtons();
}

void Os8Popup::layoutButtons()
{
    // As Leave Silence (1447:9159) has them: the primary at the right edge, 4 px down
    // its 33 px row; the secondary 10 px to its left, 6 px down.
    const int rowY = kStroke + kRowsTop + rowsHeight + kBottomSpacer;
    const int pw = buttonWidth (primary->getButtonText(), os8::ButtonKind::primary);
    primary->setBounds (kStroke + inset + contentW - pw, rowY + 4, pw, os8::buttonHeight (os8::ButtonKind::primary));
    if (secondary == nullptr) return;
    const int sw = buttonWidth (secondary->getButtonText(), os8::ButtonKind::secondary);
    secondary->setBounds (primary->getX() - 10 - sw, rowY + 6, sw, os8::buttonHeight (os8::ButtonKind::secondary));
}

juce::Rectangle<int> Os8Popup::frameBounds() const noexcept
{
    return { kStroke, kStroke, getWidth() - 2 * kStroke - kShadow, getHeight() - 2 * kStroke - kShadow };
}

void Os8Popup::addLabel (const juce::String& text, float x, float y, os8::TextStyle style)
{
    labels.push_back ({ text, x, y, style });
    repaint();
}

void Os8Popup::showOver (juce::Component& over, juce::Point<int> topLeft)
{
    auto* top = over.getTopLevelComponent();
    const auto at = top->getLocalPoint (&over, topLeft);

    top->addAndMakeVisible (this);
    keepInside (at);
    toFront (true);
    // Anything that ends the modal state from outside (a quit cancels every modal
    // component) answers as Escape would, so no popup is left on screen unanswered.
    enterModalState (true, juce::ModalCallbackFunction::create (
                               [safe = juce::Component::SafePointer<Os8Popup> (this)] (int)
                               {
                                   if (safe != nullptr) safe->dismiss (safe->escapeAnswer);
                               }),
                     false);
}

void Os8Popup::dismiss (int result)
{
    if (dismissed) return;
    dismissed = true;
    exitModalState (result);
    setVisible (false);

    // Run the answer, then go, once whatever pressed the button has finished with it.
    juce::MessageManager::callAsync ([this, result]
    {
        std::unique_ptr<Os8Popup> self (this);
        if (auto* parent = getParentComponent()) parent->removeChildComponent (this);
        if (onDismiss) onDismiss (result);
    });
}

void Os8Popup::keepInside (juce::Point<int> at)
{
    // A popup bigger than the window keeps its title bar and left edge in reach.
    if (auto* parent = getParentComponent())
    {
        at.x = juce::jlimit (0, std::max (0, parent->getWidth() - getWidth()), at.x);
        at.y = juce::jlimit (0, std::max (0, parent->getHeight() - getHeight()), at.y);
    }
    setTopLeftPosition (at);
}

void Os8Popup::mouseDown (const juce::MouseEvent& e)
{
    dragging = e.mods.isLeftButtonDown()
               && frameBounds().withHeight (os8::kTitleBarHeight).expanded (kStroke).contains (e.getPosition());
    dragFrom = getPosition();
}

void Os8Popup::mouseDrag (const juce::MouseEvent& e)
{
    // From the screen positions: the popup moves under the pointer as it is dragged.
    if (dragging)
        keepInside (dragFrom + (e.getScreenPosition() - e.getMouseDownScreenPosition()));
}

void Os8Popup::parentSizeChanged()
{
    keepInside (getPosition());
}

void Os8Popup::paint (juce::Graphics& g)
{
    const auto frame = frameBounds();
    const auto outline = frame.expanded (kStroke);

    g.setColour (colour::black.withAlpha (0.25f));
    g.fillRect (outline.translated (kShadow, kShadow));
    g.setColour (colour::black);
    g.fillRect (outline);
    g.setColour (alert ? colour::gray500 : colour::gray000);
    g.fillRect (frame);

    os8::paintTitleBar (g, frame.withHeight (os8::kTitleBarHeight), title, true, 6, 6,
                        alert ? colour::white : colour::black);

    // The content area: 4 px in from the sides and foot, ruled in black outside it.
    const auto area = frame.withTrimmedTop (os8::kTitleBarHeight).reduced (4, 0).withTrimmedBottom (kFoot);
    g.setColour (colour::gray000);
    g.fillRect (area);
    g.setColour (colour::black);
    g.drawRect (area.expanded (1), 1);

    const float left = (float) (kStroke + inset), rowsTop = (float) (kStroke + kRowsTop);
    for (const auto& l : labels)
        os8::drawText (g, l.text, l.style,
                       { left + l.x, rowsTop + l.y, (float) contentW - l.x, os8::sizeOf (l.style) });
}

bool Os8Popup::keyPressed (const juce::KeyPress& k)
{
    if (k.getKeyCode() == juce::KeyPress::returnKey && ! k.getModifiers().isAnyModifierKeyDown())
    {
        if (primary != nullptr && primary->isEnabled()) dismiss (1);
        return true;
    }
    if (k.getKeyCode() == juce::KeyPress::escapeKey
        || (k.getModifiers().isCommandDown() && k.getTextCharacter() == '.'))
    {
        dismiss (escapeAnswer);
        return true;
    }
    return true;   // nothing else gets past a modal popup
}

void Os8Popup::inputAttemptWhenModal()
{
    // A click outside: stay put, as the Mac OS dialogs did (and keep the caret).
    toFront (false);
}
