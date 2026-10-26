#include "EditPopups.h"

#include "effects/Units.h"
#include "engine/Edit.h"

namespace editpopups {

namespace {

const char* const kUnitNames[] = { "Samples", "Bytes", "Seconds", "Milliseconds", "Meters", "Feet" };

/** The one duration 0.9.2 remembered for its three duration dialogs (A5−0x5a8), in
    seconds, and the units it was last shown in. */
struct Memory
{
    double seconds = 1.0;
    se::Unit unit = se::Unit::Milliseconds;
};

Memory& memory()
{
    static Memory m;
    return m;
}

Os8TextField::Unit fieldUnit (se::Unit u)
{
    switch (u)
    {
        case se::Unit::Samples:      return Os8TextField::Unit::samples;
        case se::Unit::Bytes:        return Os8TextField::Unit::bytes;
        case se::Unit::Milliseconds: return Os8TextField::Unit::milliseconds;
        case se::Unit::Seconds: case se::Unit::Meters: case se::Unit::Feet: break;
    }
    return Os8TextField::Unit::seconds;
}

/** A duration in seconds as shown in `unit`, and back — at the sound's rate and depth.
    Unlike se::bytesToUnit this does not quantise to whole samples first, so the
    remembered 1 s reads "1000.00", not the 999.98 ms its 22254 samples really last. */
double toUnit (double seconds, se::Unit unit, double rate, int bps)
{
    if (unit == se::Unit::Samples) return std::trunc (seconds * rate);
    if (unit == se::Unit::Bytes)   return std::trunc (seconds * rate) * se::frameSizeFor (bps);
    return seconds * se::secondsScale (unit);
}

double toSeconds (double value, se::Unit unit, double rate, int bps)
{
    if (unit == se::Unit::Samples) return value / rate;
    if (unit == se::Unit::Bytes)   return value / se::frameSizeFor (bps) / rate;
    return value / se::secondsScale (unit);
}

/** The field + units popup pair every one of these dialogs carries (Text field
    "Highlited"/Small 69 × 16 and Dropdown 100 × 17, 4 px apart). The field shows a
    number in the popup's unit; changing the unit rewrites it, as 0.9.2 did. */
struct DurationRow
{
    Os8TextField* field = nullptr;
    Os8Dropdown* units = nullptr;
    se::Unit unit = se::Unit::Milliseconds;
    double rate = 0.0;
    int bps = 8;

    void show (double value)
    {
        const auto fu = fieldUnit (unit);
        field->setNumeric (Os8TextField::stepFor (fu), Os8TextField::decimalsFor (fu), 0.0, 1.0e9);
        field->setValue (value);
    }

    double seconds() const   { return toSeconds (std::max (0.0, field->getValue()), unit, rate, bps); }
    size_t bytes() const     { return se::unitToBytes (std::max (0.0, field->getValue()), unit, rate, bps); }
};

std::shared_ptr<DurationRow> addDurationRow (Os8Popup& popup, const se::SoundDocument& doc,
                                             int x, int y, se::Unit unit, double seconds)
{
    auto row = std::make_shared<DurationRow>();
    row->unit = unit;
    row->rate = doc.rateHz;
    row->bps = doc.bps;

    row->field = popup.addContent (std::make_unique<Os8TextField> (os8::FieldSize::small),
                                   { x, y + 1, 69, os8::metricsOf (os8::FieldSize::small).height });
    auto dd = std::make_unique<Os8Dropdown> (Os8Dropdown::Style::standard);
    juce::StringArray names;
    for (auto* n : kUnitNames) names.add (n);
    dd->setItems (names);
    dd->setSelectedIndex ((int) unit);
    dd->setWantsKeyboardFocus (false);
    row->units = popup.addContent (std::move (dd), { x + 73, y, 100, os8::kDropdownHeight });

    std::weak_ptr<DurationRow> weak = row;
    row->units->onChange = [weak] (int index)
    {
        auto r = weak.lock();
        if (r == nullptr) return;
        const double s = r->seconds();
        r->unit = (se::Unit) index;
        r->show (toUnit (s, r->unit, r->rate, r->bps));
        r->field->grabFocus();
    };
    row->show (toUnit (seconds, unit, doc.rateHz, doc.bps));
    return row;
}

/** A radio with its label (Radiobutton 12 × 12, the text 4 px after it), at the
    design's x 16 in a 20 px row. */
Os8Toggle* addRadio (Os8Popup& popup, const juce::String& text, int y)
{
    auto t = std::make_unique<Os8Toggle> (text, Os8Toggle::Shape::radio);
    t->setWantsKeyboardFocus (false);
    t->setMouseClickGrabsKeyboardFocus (false);
    auto* raw = popup.addContent (std::move (t), { 16, y, 0, os8::kToggleSize });
    raw->setSize (raw->preferredWidth(), os8::kToggleSize);
    return raw;
}

/** Two radios that exclude each other. */
void pairRadios (Os8Toggle* a, Os8Toggle* b)
{
    a->onClick = [a, b] { a->setToggleState (true, juce::dontSendNotification); b->setToggleState (false, juce::dontSendNotification); };
    b->onClick = [a, b] { b->setToggleState (true, juce::dontSendNotification); a->setToggleState (false, juce::dontSendNotification); };
}

/** Return in the field presses the primary button, Escape the secondary. */
void wireField (Os8Popup& popup, Os8TextField& field)
{
    auto* p = &popup;
    field.onReturn = [p] { p->dismiss (1); };
    field.onEscape = [p] { p->dismiss (0); };
}

/** Remember what was applied, for the next of these dialogs. */
void remember (const DurationRow& row)
{
    memory().seconds = row.seconds();
    memory().unit = row.unit;
}

} // namespace

void resetMemory()
{
    memory() = Memory();
}

Built leaveSilence (const se::SoundDocument& doc, std::function<void (size_t, bool)> apply)
{
    // 543:8056: rows from y 33 of the variant; everything below is relative to that.
    Built b;
    b.popup = std::make_unique<Os8Popup> ("Leave Silence", 83);
    auto& p = *b.popup;
    p.addLabel ("Leave", 0.0f, 8.0f);
    auto row = addDurationRow (p, doc, 39, 4, memory().unit, memory().seconds);
    p.addLabel ("of silence at the beginning of", 0.0f, 29.0f);
    auto* sound = addRadio (p, "the sound", 47);
    auto* chans = addRadio (p, "the selected channel(s)", 67);
    pairRadios (sound, chans);

    // DITL 142 as CODE 12 sets it up: with every track already selected, "the selected
    // channels" would say the same thing, so it is greyed and "the sound" is picked.
    const bool allTracks = doc.firstSelTrack == 1 && doc.lastSelTrack == doc.numTracks();
    chans->setEnabled (! allTracks);
    (allTracks ? sound : chans)->setToggleState (true, juce::dontSendNotification);

    p.setButtons ("Cancel", "Apply");
    wireField (p, *row->field);
    b.field = row->field;
    p.onDismiss = [row, sound, apply] (int result)
    {
        if (result != 1) return;
        row->field->commitNow();
        remember (*row);
        if (apply) apply (row->bytes(), sound->getToggleState());
    };
    return b;
}

Built shiftSound (const se::SoundDocument& doc, std::function<void (size_t, bool)> apply)
{
    // 543:9335. The sentence names the scope (STR# 139), which the selection decides.
    Built b;
    b.popup = std::make_unique<Os8Popup> ("Shift Sound", 83);
    auto& p = *b.popup;
    p.addLabel (juce::String (se::shiftCaption (se::shiftScope (doc))), 0.0f, 4.0f);
    auto row = addDurationRow (p, doc, 0, 22, memory().unit, memory().seconds);
    auto* right = addRadio (p, "Shift Right", 47);
    auto* left = addRadio (p, "Shift Left", 67);
    pairRadios (right, left);
    right->setToggleState (true, juce::dontSendNotification);

    p.setButtons ("Cancel", "Apply");
    wireField (p, *row->field);
    b.field = row->field;
    p.onDismiss = [row, right, apply] (int result)
    {
        if (result != 1) return;
        row->field->commitNow();
        remember (*row);
        if (apply) apply (row->bytes(), right->getToggleState());
    };
    return b;
}

} // namespace editpopups
