#include "Os8CheckList.h"

#include <cmath>

namespace colour = os8::colour;

namespace {
// 1447:9162: the rows start 5 px in, 23⅓ px apart; the thumb sits 10 px in from the
// right edge, 5 px wide, in a track 5 px short of the top and 5 px of the foot.
constexpr float kPitch = 70.0f / 3.0f;
constexpr int kPad = 5, kRowHeight = 19, kThumbW = 5, kThumbRight = 10;
const juce::Colour kThumb { 0xff333399 };
}

class Os8CheckList::Row : public juce::ToggleButton
{
public:
    Row (const Item& i, const std::vector<int>& rights) : item (i), factRights (rights)
    {
        setButtonText (item.name);
        setToggleState (item.ticked, juce::dontSendNotification);
        setWantsKeyboardFocus (false);
        setMouseClickGrabsKeyboardFocus (false);
    }

    void paintButton (juce::Graphics& g, bool, bool) override
    {
        os8::paintCheckbox (g, { (float) kPad, 4.0f }, getToggleState(), isEnabled());

        // The columns are placed from the list's left; this row starts 1 px in.
        int nameRight = getWidth();
        for (int i = 0; i < item.facts.size() && i < (int) factRights.size(); ++i)
        {
            const int w = os8::widthOf (item.facts[i], os8::TextStyle::headline);
            const int right = factRights[(size_t) i] - 1;
            g.setColour (colour::gray400);
            os8::drawText (g, item.facts[i], os8::TextStyle::headline,
                           { (float) (right - w), 4.0f, (float) w + 1.0f, 12.0f });
            if (item.facts[i].isNotEmpty()) nameRight = std::min (nameRight, right - w - 8);
        }

        const int nameX = kPad + os8::kToggleSize + Os8Toggle::kLabelGap - 1;
        auto name = item.name;
        const juce::String more = juce::String::fromUTF8 ("\xE2\x80\xA6");
        if (os8::widthOf (name, os8::TextStyle::headline) > nameRight - nameX)
        {
            while (name.length() > 1 && os8::widthOf (name + more, os8::TextStyle::headline) > nameRight - nameX)
                name = name.dropLastCharacters (1);
            name += more;
        }
        g.setColour (getToggleState() && isEnabled() ? colour::black : colour::gray400);
        os8::drawText (g, name, os8::TextStyle::headline,
                       { (float) nameX, 4.0f, (float) std::max (0, nameRight - nameX), 12.0f });
    }

private:
    Item item;
    std::vector<int> factRights;
};

Os8CheckList::Os8CheckList (std::vector<Item> items, std::vector<int> factRights)
{
    setMouseClickGrabsKeyboardFocus (false);
    clip.setInterceptsMouseClicks (false, true);
    clip.setMouseClickGrabsKeyboardFocus (false);
    addAndMakeVisible (clip);
    for (const auto& item : items)
    {
        rows.push_back (std::make_unique<Row> (item, factRights));
        auto* r = rows.back().get();
        r->onClick = [this] { if (onChange) onChange(); };
        // The wheel over a row scrolls the list.
        r->addMouseListener (this, false);
        clip.addAndMakeVisible (*r);
    }
}

Os8CheckList::~Os8CheckList() = default;

bool Os8CheckList::isTicked (int i) const
{
    return juce::isPositiveAndBelow (i, size()) && rows[(size_t) i]->getToggleState();
}

int Os8CheckList::numTicked() const
{
    return (int) ticked().size();
}

std::vector<int> Os8CheckList::ticked() const
{
    std::vector<int> out;
    for (int i = 0; i < size(); ++i)
        if (isTicked (i)) out.push_back (i);
    return out;
}

void Os8CheckList::setTicked (int i, bool on)
{
    if (juce::isPositiveAndBelow (i, size()))
        rows[(size_t) i]->setToggleState (on, juce::dontSendNotification);
}

void Os8CheckList::setAllTicked (bool on)
{
    for (int i = 0; i < size(); ++i) setTicked (i, on);
}

int Os8CheckList::heightFor (int n, int maximum) noexcept
{
    return std::min (maximum, 2 * kPad + (int) std::ceil ((float) n * kPitch));
}

float Os8CheckList::contentHeight() const noexcept { return 2.0f * kPad + (float) size() * kPitch; }

float Os8CheckList::maxScroll() const noexcept
{
    // A pixel of rounding is not worth a scroll bar.
    const float over = contentHeight() - (float) getHeight();
    return over > 1.0f ? over : 0.0f;
}

juce::Rectangle<float> Os8CheckList::thumb() const noexcept
{
    if (maxScroll() <= 0.0f) return {};
    const float track = (float) (getHeight() - 2 * kPad);
    const float length = std::max (16.0f, track * (float) getHeight() / contentHeight());
    const float y = (float) kPad + (track - length) * (scroll / maxScroll());
    return { (float) (getWidth() - kThumbRight), y, (float) kThumbW, length };
}

void Os8CheckList::scrollTo (float to)
{
    to = juce::jlimit (0.0f, maxScroll(), to);
    if (juce::approximatelyEqual (to, scroll)) return;
    scroll = to;
    resized();
    repaint();
}

void Os8CheckList::resized()
{
    clip.setBounds (getLocalBounds().reduced (1));
    scroll = juce::jlimit (0.0f, maxScroll(), scroll);
    // Rows stop short of the thumb, so a press on it is the list's.
    const int w = clip.getWidth() - (maxScroll() > 0.0f ? kThumbRight + 3 : 0);
    for (int i = 0; i < size(); ++i)
        rows[(size_t) i]->setBounds (0, juce::roundToInt ((float) kPad + (float) i * kPitch - scroll) - 1,
                                     w, kRowHeight);
}

void Os8CheckList::paint (juce::Graphics& g)
{
    os8::paintTextField (g, getLocalBounds(), os8::FieldSize::large, os8::FieldState::normal);
}

void Os8CheckList::paintOverChildren (juce::Graphics& g)
{
    g.setColour (kThumb);
    g.fillRect (thumb());
}

void Os8CheckList::mouseDown (const juce::MouseEvent& e)
{
    draggingThumb = false;
    if (e.eventComponent != this || thumb().isEmpty()) return;
    const auto p = e.position;
    if (p.x < (float) (getWidth() - kThumbRight - 3)) return;
    if (p.y < thumb().getY())            scrollTo (scroll - (float) getHeight() + kPitch);
    else if (p.y > thumb().getBottom())  scrollTo (scroll + (float) getHeight() - kPitch);
    else                                 draggingThumb = true;
    dragFrom = scroll;
}

void Os8CheckList::mouseDrag (const juce::MouseEvent& e)
{
    if (! draggingThumb || e.eventComponent != this) return;
    const float free = (float) (getHeight() - 2 * kPad) - thumb().getHeight();
    if (free > 0.0f)
        scrollTo (dragFrom + (float) e.getDistanceFromDragStartY() * maxScroll() / free);
}

void Os8CheckList::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    // Heard twice for a wheel over a row (as its listener and as its parent): once.
    if (e.eventComponent != this) return;
    scrollTo (scroll - w.deltaY * 256.0f);
}
