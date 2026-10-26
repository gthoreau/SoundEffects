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

Os8Popup::Os8Popup (const juce::String& t, int rows) : title (t), rowsHeight (rows)
{
    const int h = 2 * kInset + kTitleH + kTopSpacer + rowsHeight + kBottomSpacer + kButtonsH;
    setSize (kWidth + kShadow, h + kShadow);
    setWantsKeyboardFocus (true);
}

Os8Popup::~Os8Popup() = default;

Os8Button& Os8Popup::setButtons (const juce::String& secondaryText, const juce::String& primaryText, int escapeResult)
{
    escapeAnswer = escapeResult;
    primary = std::make_unique<Os8Button> (primaryText, os8::ButtonKind::primary);
    primary->onClick = [this] { dismiss (1); };
    secondary = std::make_unique<Os8Button> (secondaryText, os8::ButtonKind::secondary);
    secondary->onClick = [this] { dismiss (0); };

    for (auto* b : { primary.get(), secondary.get() })
    {
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
    // As Leave Silence (543:8056) has them: the primary at the right edge, 4 px down
    // its 33 px row; the secondary 10 px to its left, 6 px down.
    const int rowY = getHeight() - kShadow - kInset - kButtonsH;
    const int pw = buttonWidth (primary->getButtonText(), os8::ButtonKind::primary);
    primary->setBounds (kInset + kContentW - pw, rowY + 4, pw, os8::buttonHeight (os8::ButtonKind::primary));
    const int sw = buttonWidth (secondary->getButtonText(), os8::ButtonKind::secondary);
    secondary->setBounds (primary->getX() - 10 - sw, rowY + 6, sw, os8::buttonHeight (os8::ButtonKind::secondary));
}

void Os8Popup::addLabel (const juce::String& text, float x, float y)
{
    labels.push_back ({ text, x, y });
    repaint();
}

void Os8Popup::showOver (juce::Component& over, juce::Point<int> topLeft)
{
    auto* top = over.getTopLevelComponent();
    auto at = top->getLocalPoint (&over, topLeft);
    at.x = juce::jlimit (0, std::max (0, top->getWidth() - getWidth()), at.x);
    at.y = juce::jlimit (0, std::max (0, top->getHeight() - getHeight()), at.y);

    top->addAndMakeVisible (this);
    setTopLeftPosition (at);
    toFront (true);
    enterModalState (true, nullptr, false);
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

void Os8Popup::paint (juce::Graphics& g)
{
    const juce::Rectangle<int> frame (0, 0, kWidth, getHeight() - kShadow);
    juce::Path shape;
    shape.addRectangle (frame.toFloat());

    os8::dropShadow (g, shape, 1.0f, 1.0f, juce::Colour (0xff808080));
    g.setColour (colour::white);
    g.fillRect (frame);
    os8::innerShadow (g, shape, -2.0f, -2.0f, 0, juce::Colour (0xffd0d0d0));
    os8::innerShadow (g, shape, 2.0f, 2.0f, 0, juce::Colour (0xffe7e7e7));
    g.setColour (colour::black);
    g.drawRect (frame, 1);

    // The title row, ruled in grey beneath, as a tray's.
    g.setColour (colour::black);
    os8::drawText (g, title, os8::TextStyle::smallLabel, { (float) kInset, (float) kInset + 4.0f, (float) kContentW, 10.0f });
    g.setColour (colour::gray400);
    g.fillRect (kInset, kInset + kTitleH - 1, kContentW, 1);

    g.setColour (colour::black);
    const float rowsTop = (float) (kInset + kTitleH + kTopSpacer);
    for (const auto& l : labels)
        os8::drawText (g, l.text, os8::TextStyle::smallLabel,
                       { (float) kInset + l.x, rowsTop + l.y, (float) kContentW - l.x, 10.0f });
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
