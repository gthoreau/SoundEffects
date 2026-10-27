#include "FilePopups.h"

#include "engine/SampleFormat.h"

namespace filepopups {

namespace {

constexpr int kRowH = 20;
constexpr int kMaxVisibleRows = 8;

/** Fit `text` into `width` px at Small Label, ending in an ellipsis when it must be cut. */
juce::String fitted (juce::String text, int width)
{
    if (os8::widthOf (text, os8::TextStyle::smallLabel) <= width) return text;
    while (text.length() > 1
           && os8::widthOf (text + juce::String::fromUTF8 ("\xE2\x80\xA6"), os8::TextStyle::smallLabel) > width)
        text = text.dropLastCharacters (1);
    return text + juce::String::fromUTF8 ("\xE2\x80\xA6");
}

/** One Sound's row in the list: its checkbox (the name) and the facts, right-aligned. */
class SoundRow : public juce::Component
{
public:
    SoundRow (const se::WindowState& s, int width) : facts (shortSummary (s))
    {
        const int factsW = os8::widthOf (facts, os8::TextStyle::smallLabel);
        toggle = std::make_unique<Os8Toggle> (fitted (juce::String (s.name), width - factsW - 40),
                                              Os8Toggle::Shape::checkbox);
        toggle->setWantsKeyboardFocus (false);
        toggle->setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (*toggle);
        setSize (width, kRowH);
    }

    static juce::String shortSummary (const se::WindowState& s)
    {
        return juce::String (s.tracks.size()) + (s.tracks.size() == 1 ? " track  " : " tracks  ")
             + juce::String (s.rateHz / 1000.0, 1) + " kHz";
    }

    void resized() override
    {
        toggle->setBounds (4, 4, toggle->preferredWidth(), os8::kToggleSize);
    }

    void paint (juce::Graphics& g) override
    {
        const int w = os8::widthOf (facts, os8::TextStyle::smallLabel);
        g.setColour (juce::Colour (0xff666666));
        os8::drawText (g, facts, os8::TextStyle::smallLabel,
                       { (float) (getWidth() - w - 4), 5.0f, (float) w, 10.0f });
    }

    std::unique_ptr<Os8Toggle> toggle;

private:
    juce::String facts;
};

} // namespace

juce::String summaryOf (const se::WindowState& s)
{
    size_t longest = 0;
    for (const auto& t : s.tracks) longest = std::max (longest, t.data.size());
    const double seconds = s.rateHz > 0.0
        ? (double) (longest / (size_t) std::max (1, se::frameSizeFor (s.bps))) / s.rateHz : 0.0;

    return juce::String (s.tracks.size()) + (s.tracks.size() == 1 ? " track" : " tracks")
         + juce::String::fromUTF8 (" \xC2\xB7 ") + juce::String (s.rateHz / 1000.0, 1) + " kHz"
         + juce::String::fromUTF8 (" \xC2\xB7 ") + juce::String (s.bps) + " bit"
         + juce::String::fromUTF8 (" \xC2\xB7 ") + juce::String (seconds, 2) + " s";
}

std::unique_ptr<Os8Popup> exportSound (const se::WindowState& sound)
{
    auto popup = std::make_unique<Os8Popup> ("Export Sound", 62);
    auto& p = *popup;
    p.addLabel ("Save this Sound as a .sefx file of its own:", 0.0f, 4.0f);
    p.addLabel (fitted (juce::String (sound.name), Os8Popup::kContentW), 0.0f, 22.0f);
    p.addLabel (summaryOf (sound), 0.0f, 36.0f);
    p.addLabel ("It opens like any canvas, audio included.", 0.0f, 50.0f);
    p.setButtons ("Cancel", juce::String::fromUTF8 ("Export\xE2\x80\xA6"));
    return popup;
}

std::unique_ptr<Os8Popup> importSound (const juce::String& fileName,
                                       const std::vector<se::WindowState>& sounds,
                                       std::function<void (std::vector<size_t>)> doImport)
{
    const int rows = (int) sounds.size();
    const int visible = std::min (rows, kMaxVisibleRows);
    const int listH = visible * kRowH + 2;
    const int multi = rows > 1 ? 1 : 0;

    auto popup = std::make_unique<Os8Popup> ("Import Sound", 20 + (multi ? kRowH : 0) + listH);
    auto& p = *popup;
    p.addLabel (fitted ("Sounds in " + fileName + ":", Os8Popup::kContentW), 0.0f, 4.0f);

    auto& go = p.setButtons ("Cancel", "Import");

    // The list: a viewport over one column of rows once there are more than fit.
    auto column = std::make_unique<juce::Component>();
    std::vector<SoundRow*> rowPtrs;
    const int contentW = Os8Popup::kContentW - (rows > visible ? 12 : 0);
    for (int i = 0; i < rows; ++i)
    {
        auto row = std::make_unique<SoundRow> (sounds[(size_t) i], contentW);
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
    p.addContent (std::move (view), { 0, 20 + (multi ? kRowH : 0), Os8Popup::kContentW, listH });

    auto* goButton = &go;
    auto update = [rowPtrs, goButton]
    {
        bool any = false;
        for (auto* r : rowPtrs) any = any || r->toggle->getToggleState();
        goButton->setEnabled (any);
    };
    for (auto* r : rowPtrs)
        r->toggle->onClick = update;

    if (multi)
    {
        auto all = std::make_unique<Os8Toggle> ("Select all", Os8Toggle::Shape::checkbox);
        all->setWantsKeyboardFocus (false);
        all->setMouseClickGrabsKeyboardFocus (false);
        auto* allPtr = p.addContent (std::move (all), { 4, 24, 0, os8::kToggleSize });
        allPtr->setSize (allPtr->preferredWidth(), os8::kToggleSize);
        allPtr->onClick = [rowPtrs, allPtr, update]
        {
            for (auto* r : rowPtrs) r->toggle->setToggleState (allPtr->getToggleState(), juce::dontSendNotification);
            update();
        };
    }
    else
    {
        rowPtrs[0]->toggle->setToggleState (true, juce::dontSendNotification);
    }
    update();

    p.onDismiss = [rowPtrs, doImport] (int result)
    {
        if (result != 1 || ! doImport) return;
        std::vector<size_t> picked;
        for (size_t i = 0; i < rowPtrs.size(); ++i)
            if (rowPtrs[i]->toggle->getToggleState()) picked.push_back (i);
        if (! picked.empty()) doImport (std::move (picked));
    };
    return popup;
}

} // namespace filepopups
