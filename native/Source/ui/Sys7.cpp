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

void Sys7Button::setDefaultButton (bool shouldBeDefault)
{
    if (defaultButton == shouldBeDefault) return;
    defaultButton = shouldBeDefault;
    repaint();
}

void Sys7Button::paintButton (Graphics& g, bool over, bool down)
{
    const bool on = isEnabled();
    const int inset = defaultButton ? kDefaultRingInset : 0;
    auto face = getLocalBounds().reduced (inset).toFloat().reduced (1.0f);

    if (defaultButton)
    {
        // System 7's default button wears a heavy ring, standing off the face by a
        // hairline. It is how the dialog says which button Return is wired to
        // without spending a word on it.
        g.setColour (on ? sys7::kInk : sys7::kDim);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.5f), 6.0f, 3.0f);
    }

    g.setColour (down ? sys7::kInk : sys7::kFace);
    g.fillRoundedRectangle (face, 3.0f);

    // Weight carries hover, exactly as the Windows' borders do on the canvas.
    g.setColour (on ? sys7::kInk : sys7::kDim);
    g.drawRoundedRectangle (face, 3.0f, over && ! down && on ? 2.0f : 1.0f);

    g.setColour (down ? sys7::kWhite : (on ? sys7::kInk : sys7::kDim));
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (getButtonText(), getLocalBounds().reduced (inset), juce::Justification::centred, false);
}

// --- Sys7AlertDialog ---------------------------------------------------------

Sys7AlertDialog::Sys7AlertDialog (const juce::String& t, const juce::String& checkboxText,
                                  const juce::String& confirmText, const juce::String& cancelText)
    : title (t), confirm (confirmText), cancel (cancelText)
{
    confirm.setDefaultButton (true);
    confirm.onClick = [this] { exitModalState (1); };
    cancel.onClick = [this] { exitModalState (0); };
    addAndMakeVisible (confirm);
    addAndMakeVisible (cancel);

    if (checkboxText.isNotEmpty())
    {
        checkbox = std::make_unique<juce::ToggleButton> (checkboxText);
        checkbox->setColour (juce::ToggleButton::textColourId, sys7::kInk);
        checkbox->setColour (juce::ToggleButton::tickColourId, sys7::kInk);
        // The box's outline is the disabled-tick colour; left at its default it is
        // a grey that disappears against the dialog's face.
        checkbox->setColour (juce::ToggleButton::tickDisabledColourId, sys7::kInk);
        // Clicking the box must not steal Return from the default button.
        checkbox->setWantsKeyboardFocus (false);
        addAndMakeVisible (*checkbox);
    }

    setWantsKeyboardFocus (true);
    setAlwaysOnTop (true);
}

void Sys7AlertDialog::show (juce::Component& host, const juce::String& title,
                            const juce::String& checkboxText, const juce::String& confirmText,
                            const juce::String& cancelText,
                            std::function<void (bool, bool)> onChoice)
{
    auto* top = host.getTopLevelComponent();
    if (top == nullptr) return;

    // Owned by the modal manager from here on: `exitModalState` deletes it once the
    // callback has run, which is why the callback must not touch it — the tick is
    // read out through this shared flag rather than off the dying component.
    auto ticked = std::make_shared<bool> (false);

    auto* dialog = new Sys7AlertDialog (title, checkboxText, confirmText, cancelText);
    if (dialog->checkbox != nullptr)
        dialog->checkbox->onClick = [c = dialog->checkbox.get(), ticked]
        {
            *ticked = c->getToggleState();
        };

    top->addAndMakeVisible (dialog);
    dialog->setBounds (top->getLocalBounds());
    dialog->grabKeyboardFocus();

    dialog->enterModalState (true,
                             juce::ModalCallbackFunction::create ([choice = std::move (onChoice), ticked] (int result)
                             {
                                 if (choice) choice (result == 1, *ticked);
                             }),
                             true);
}

juce::Rectangle<int> Sys7AlertDialog::boxBounds() const
{
    int h = kPad + 18 + kPad + kButtonH + kPad;
    if (checkbox != nullptr) h += kCheckH;
    return juce::Rectangle<int> (kBoxW, h).withCentre (getLocalBounds().getCentre());
}

void Sys7AlertDialog::paint (Graphics& g)
{
    // Dimmed, not blacked out: what the effect is about to eat is on the canvas
    // behind, and the question is easier to answer while you can still see it.
    g.fillAll (juce::Colours::black.withAlpha (0.35f));

    const auto box = boxBounds();

    // The System 7 alert frame: a black rule, a white gutter, then the face.
    g.setColour (sys7::kInk);
    g.fillRect (box);
    g.setColour (sys7::kWhite);
    g.fillRect (box.reduced (2));
    g.setColour (sys7::kFace);
    g.fillRect (box.reduced (4));
    g.setColour (sys7::kInk);
    g.drawRect (box.reduced (4), 1);

    auto text = box.reduced (kPad + 4, kPad);

    g.setColour (sys7::kInk);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (title, text.removeFromTop (18), juce::Justification::centredLeft, false);
}

void Sys7AlertDialog::resized()
{
    auto inner = boxBounds().reduced (kPad, kPad);

    if (checkbox != nullptr)
    {
        // Under the title, aligned with it: the checkbox is the only body the box
        // has, so it sits where the text used to rather than beside the buttons.
        checkbox->setBounds (inner.getX() + 4, inner.getY() + 18, inner.getWidth() - 4, kCheckH);
    }

    auto row = inner.removeFromBottom (kButtonH);

    // The default button sits outermost, with room for its ring; Cancel to its left.
    auto confirmArea = row.removeFromRight (72 + Sys7Button::kDefaultRingInset * 2)
                          .expanded (0, Sys7Button::kDefaultRingInset);
    confirm.setBounds (confirmArea);
    row.removeFromRight (6);
    cancel.setBounds (row.removeFromRight (72));
}

void Sys7AlertDialog::parentSizeChanged()
{
    // The backdrop is the whole window, so it has to follow the window — otherwise
    // resizing while the alert is up leaves live UI exposed beside a modal dialog.
    if (auto* p = getParentComponent())
        setBounds (p->getLocalBounds());
}

bool Sys7AlertDialog::keyPressed (const juce::KeyPress& k)
{
    if (k.isKeyCode (juce::KeyPress::returnKey) && ! k.getModifiers().isAnyModifierKeyDown())
    {
        exitModalState (1);
        return true;
    }

    if (k.isKeyCode (juce::KeyPress::escapeKey))
    {
        exitModalState (0);
        return true;
    }

    return false;
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
