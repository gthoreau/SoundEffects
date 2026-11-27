#include "SetupSheet.h"

namespace colour = os8::colour;

namespace {
// 1447:9215: the content box sits 9 px in and is 251 px wide.
constexpr int kInset = 9, kContentW = 251;
constexpr int kButtonsY = 74;
}

SetupSheet::SetupSheet (Kind k, const juce::String& name, std::function<bool (const juce::String&)> nameTaken)
    : kind (k), taken (std::move (nameTaken)), sheet (k == Kind::saveAs ? "Save setup as" : "Manage Setup")
{
    setMouseClickGrabsKeyboardFocus (false);
    sheet.setMouseClickGrabsKeyboardFocus (false);
    addAndMakeVisible (sheet);
    sheet.addAndMakeVisible (nameField);

    for (auto* b : { &save, &cancel, &remove })
    {
        b->setWantsKeyboardFocus (false);
        b->setMouseClickGrabsKeyboardFocus (false);
    }
    sheet.addAndMakeVisible (save);
    sheet.addAndMakeVisible (cancel);
    if (kind == Kind::manage) sheet.addAndMakeVisible (remove);

    save.onClick = [this] { pressSave(); };
    cancel.onClick = [this] { dismiss(); };
    remove.onClick = [this]
    {
        if (leaving) return;
        if (onDelete) onDelete();
        dismiss();
    };

    nameField.setText (name);
    nameField.onTextChange = [this] { nameChanged(); };
    nameField.onReturn = [this] { pressSave(); };
    nameField.onEscape = [this] { dismiss(); };
    nameChanged();
}

SetupSheet::~SetupSheet() = default;

void SetupSheet::layOver (juce::Rectangle<int> tray)
{
    trayH = tray.getHeight();
    setBounds (tray.withHeight (std::max (trayH, kSheetH + kShadow)));
}

void SetupSheet::resized()
{
    sheet.setSize (getWidth(), kSheetH + kShadow);
    nameField.setBounds (48, 46, 207, os8::metricsOf (os8::FieldSize::small).height);
    layoutButtons();
    place();
}

void SetupSheet::layoutButtons()
{
    const auto width = [] (const juce::String& t) { return os8::widthOf (t, os8::TextStyle::headline); };
    const int sw = std::max (48, width (save.getButtonText()) + 18);
    save.setBounds (kInset + kContentW - sw, kButtonsY + 4, sw, os8::buttonHeight (os8::ButtonKind::primary));
    const int cw = width (cancel.getButtonText()) + 12;
    cancel.setBounds (save.getX() - 10 - cw, kButtonsY + 6, cw, os8::buttonHeight (os8::ButtonKind::secondary));
    remove.setBounds (kInset, kButtonsY + 6, width (remove.getButtonText()) + 13,
                      os8::buttonHeight (os8::ButtonKind::secondary));
}

void SetupSheet::nameChanged()
{
    const auto name = nameField.getText().trim();
    save.setEnabled (name.isNotEmpty());
    const juce::String text = taken != nullptr && taken (name) ? "Replace" : "Save";
    if (save.getButtonText() != text)
    {
        save.setButtonText (text);
        layoutButtons();
    }
}

void SetupSheet::pressSave()
{
    const auto name = nameField.getText().trim();
    if (leaving || name.isEmpty()) return;
    if (onSave) onSave (name);
    dismiss();
}

void SetupSheet::slideDown()
{
    leaving = false;
    lastTick = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (60);
}

void SetupSheet::dismiss()
{
    if (leaving) return;
    leaving = true;
    nameField.setEnabled (false);       // gives the keyboard up while it leaves
    lastTick = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (60);
}

void SetupSheet::showAtOnce()
{
    stopTimer();
    out = 1.0f;
    place();
}

void SetupSheet::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float step = (float) ((now - lastTick) / (double) kSlideMs);
    lastTick = now;
    out = juce::jlimit (0.0f, 1.0f, out + (leaving ? -step : step));
    place();

    if (leaving ? out > 0.0f : out < 1.0f) return;
    stopTimer();
    if (leaving)
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<SetupSheet> (this)]
        {
            if (safe != nullptr && safe->onGone) safe->onGone();
        });
}

void SetupSheet::place()
{
    const float eased = out * out * (3.0f - 2.0f * out);
    sheet.setTopLeftPosition (0, juce::roundToInt ((eased - 1.0f) * (float) (kSheetH + kShadow)));
    repaint();
}

void SetupSheet::paint (juce::Graphics& g)
{
    g.setColour (colour::black.withAlpha (0.25f * out));
    g.fillRect (0, 0, getWidth(), trayH);
}

void SetupSheet::Sheet::paint (juce::Graphics& g)
{
    // The frame: white, two inner bevels, a black inside stroke and a hard grey
    // shadow two pixels below.
    const juce::Rectangle<int> frame (0, 0, getWidth(), kSheetH);
    juce::Path shape;
    shape.addRectangle (frame.toFloat());
    os8::dropShadow (g, shape, 0.0f, (float) kShadow, juce::Colour (0xff808080));
    g.setColour (colour::white);
    g.fillRect (frame);
    os8::innerShadow (g, shape, -2.0f, -2.0f, 0, juce::Colour (0xffd0d0d0));
    os8::innerShadow (g, shape, 2.0f, 2.0f, 0, juce::Colour (0xffe7e7e7));
    g.setColour (colour::black);
    g.drawRect (frame, 1);

    os8::drawText (g, title, os8::TextStyle::smallLabel, { (float) kInset, 13.0f, (float) kContentW, 10.0f });
    os8::drawText (g, "Name:", os8::TextStyle::smallLabel, { (float) kInset, 49.0f, 40.0f, 10.0f });
    g.setColour (colour::gray400);
    g.fillRect (kInset, 27, kContentW, 1);
}
