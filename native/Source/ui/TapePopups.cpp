#include "TapePopups.h"

namespace tapepopups {

namespace {

constexpr int kRowH = 20;
constexpr int kMaxVisibleRows = 6;
const juce::Colour kFacts { 0xff000000 };   // as the whole tape's length above

/** A row of the takes list: the checkbox and, right-aligned, "sent" and the length. */
class TakeRow : public juce::Component
{
public:
    TakeRow (int index, const juce::String& factsText, int width) : facts (factsText)
    {
        toggle = std::make_unique<Os8Toggle> ("Take " + juce::String (index + 1), Os8Toggle::Shape::checkbox);
        toggle->setWantsKeyboardFocus (false);
        toggle->setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (*toggle);
        setSize (width, kRowH);
    }

    void resized() override { toggle->setBounds (20, 4, toggle->preferredWidth(), os8::kToggleSize); }

    void paint (juce::Graphics& g) override
    {
        const int w = os8::widthOf (facts, os8::TextStyle::smallLabel);
        g.setColour (kFacts);
        os8::drawText (g, facts, os8::TextStyle::smallLabel, { (float) (getWidth() - w - 4), 5.0f, (float) w + 2.0f, 10.0f });
    }

    std::unique_ptr<Os8Toggle> toggle;

private:
    juce::String facts;
};

} // namespace

juce::String durationText (size_t frames, double rate)
{
    const double s = rate > 0.0 ? (double) frames / rate : 0.0;
    const int cs = (int) std::floor (s * 100.0);
    return juce::String (cs / 6000) + ":" + juce::String ((cs / 100) % 60).paddedLeft ('0', 2)
         + "." + juce::String (cs % 100).paddedLeft ('0', 2) + " s";
}

std::unique_ptr<Os8Popup> sendToCanvas (const se::Tape& tape,
                                        std::function<void (const SendChoice&)> send)
{
    const int rows = tape.numTakes();
    const int visible = std::min (rows, kMaxVisibleRows);
    const int listH = visible * kRowH + 2;
    const int listTop = 60;
    const int cleanTop = listTop + listH + 6;

    auto popup = std::make_unique<Os8Popup> ("Send to Canvas", cleanTop + os8::kToggleSize + 4);
    auto& p = *popup;
    p.addLabel ("Send the tape to the canvas:", 0.0f, 4.0f, os8::TextStyle::smallLabel);
    auto& go = p.setButtons ("Cancel", "Send");

    const double rate = tape.rate();
    const auto addRadio = [&p] (const juce::String& text, int y)
    {
        auto r = std::make_unique<Os8Toggle> (text, Os8Toggle::Shape::radio);
        r->setWantsKeyboardFocus (false);
        r->setMouseClickGrabsKeyboardFocus (false);
        r->setRadioGroupId (1);
        r->setClickingTogglesState (true);
        auto* raw = p.addContent (std::move (r), { 4, y, 0, os8::kToggleSize });
        raw->setSize (raw->preferredWidth(), os8::kToggleSize);
        return raw;
    };
    auto* whole = addRadio ("The whole tape, as one Sound", 24);
    p.addLabel (durationText (tape.length(), rate),
                (float) (Os8Popup::kContentW - os8::widthOf (durationText (tape.length(), rate), os8::TextStyle::smallLabel) - 4), 25.0f, os8::TextStyle::smallLabel);
    auto* takes = addRadio (rows == 1 ? "Its take, as one Sound:" : "Takes, one Sound each:", 42);

    // The takes list: a viewport once there are more than fit.
    bool anyUnsent = false;
    for (const auto& t : tape.takes()) anyUnsent = anyUnsent || ! t.sent;
    auto column = std::make_unique<juce::Component>();
    std::vector<TakeRow*> rowPtrs;
    const int contentW = Os8Popup::kContentW - (rows > visible ? 12 : 0);
    for (int i = 0; i < rows; ++i)
    {
        const bool sent = tape.takes()[(size_t) i].sent;
        auto row = std::make_unique<TakeRow> (i, (sent ? "sent   " : "") + durationText (tape.takeEnd (i) - tape.takeStart (i), rate), contentW);
        row->toggle->setToggleState (! sent || ! anyUnsent, juce::dontSendNotification);
        row->setTopLeftPosition (0, i * kRowH);
        rowPtrs.push_back (row.get());
        column->addAndMakeVisible (row.release());
    }
    column->setSize (contentW, rows * kRowH);
    auto view = std::make_unique<juce::Viewport>();
    view->setViewedComponent (column.release(), true);
    view->setScrollBarsShown (rows > visible, false);
    view->setScrollBarThickness (10);
    view->setWantsKeyboardFocus (false);
    p.addContent (std::move (view), { 0, listTop, Os8Popup::kContentW, listH });

    auto cleanBox = std::make_unique<Os8Toggle> ("Clean conversion (dithered)", Os8Toggle::Shape::checkbox);
    cleanBox->setWantsKeyboardFocus (false);
    cleanBox->setToggleState (true, juce::dontSendNotification);   // every opening
    auto* cl = p.addContent (std::move (cleanBox), { 4, cleanTop, 0, os8::kToggleSize });
    cl->setSize (cl->preferredWidth(), os8::kToggleSize);

    whole->setToggleState (true, juce::dontSendNotification);

    auto* goButton = &go;
    auto update = [rowPtrs, goButton, takes]
    {
        bool any = false;
        for (auto* r : rowPtrs) any = any || r->toggle->getToggleState();
        goButton->setEnabled (! takes->getToggleState() || any);
    };
    whole->onClick = update;
    takes->onClick = update;
    for (auto* r : rowPtrs)
        r->toggle->onClick = [takes, update]
        {
            takes->setToggleState (true, juce::dontSendNotification);   // ticking a take means "takes"
            update();
        };
    update();

    p.onDismiss = [rowPtrs, whole, cl, send] (int result)
    {
        if (result != 1 || ! send) return;
        SendChoice c;
        c.whole = whole->getToggleState();
        c.clean = cl->getToggleState();
        for (size_t i = 0; i < rowPtrs.size(); ++i)
            if (rowPtrs[i]->toggle->getToggleState()) c.takes.push_back ((int) i);
        if (c.whole || ! c.takes.empty()) send (c);
    };
    return popup;
}

} // namespace tapepopups
