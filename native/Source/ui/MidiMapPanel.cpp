#include "MidiMapPanel.h"

namespace {
const char* const kNames[se::MidiMap::kPads] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
constexpr uint32_t kFlashMs = 150;   // a note shorter than a tick still shows
}

MidiMapPanel::MidiMapPanel (se::Engine& e, se::MidiMapRouter& r) : engine (e), router (r)
{
    addAndMakeVisible (sound);
    sound.onChange = [this] (int i)
    {
        engine.midiMap().setSlot (selected, i - 1);   // item 0 is no Sound
        refresh();
    };

    const struct { Os8Toggle* button; bool start; se::MapAction action; juce::Point<int> at; } radios[] = {
        { &startPlay,  true,  se::MapAction::play,  { 95, 194 } },
        { &startStop,  true,  se::MapAction::stop,  { 144, 194 } },
        { &startPause, true,  se::MapAction::pause, { 201, 194 } },
        { &endStop,    false, se::MapAction::stop,  { 95, 214 } },
        { &endPause,   false, se::MapAction::pause, { 144, 214 } },
        { &endPlay,    false, se::MapAction::play,  { 201, 214 } },
    };
    for (const auto& radio : radios)
    {
        auto& b = *radio.button;
        addAndMakeVisible (b);
        b.setWantsKeyboardFocus (false);
        b.setMouseClickGrabsKeyboardFocus (false);
        b.setBounds (radio.at.x, radio.at.y, b.preferredWidth(), os8::kToggleSize);
        b.onClick = [this, start = radio.start, action = radio.action]
        {
            if (start) engine.midiMap().setStart (selected, action);
            else       engine.midiMap().setEnd (selected, action);
            showSelected();
        };
    }
    sound.setBounds (57, 169, 203, os8::kDropdownHeight);
    refresh();
    showSelected();
}

void MidiMapPanel::select (int pad)
{
    if (selected == pad) return;
    selected = pad;
    refresh();
    showSelected();
    repaint();
}

void MidiMapPanel::showSelected()
{
    const auto p = engine.midiMap().pad (selected);
    sound.setSelectedIndex (p.slot + 1);
    const auto set = [] (Os8Toggle& b, bool on, bool enabled)
    {
        b.setToggleState (on, juce::dontSendNotification);
        b.setEnabled (enabled);
    };
    set (startPlay,  p.start == se::MapAction::play,  true);
    set (startStop,  p.start == se::MapAction::stop,  true);
    set (startPause, p.start == se::MapAction::pause, true);
    set (endStop,  p.end == se::MapAction::stop,  se::MidiMap::endAllowed (p.start, se::MapAction::stop));
    set (endPause, p.end == se::MapAction::pause, se::MidiMap::endAllowed (p.start, se::MapAction::pause));
    set (endPlay,  p.end == se::MapAction::play,  se::MidiMap::endAllowed (p.start, se::MapAction::play));
}

void MidiMapPanel::refresh()
{
    // The Sounds, by mixer slot. A pad may name a slot nobody sits in any more.
    juce::StringArray now { "No sound assigned" };
    const int top = juce::jmax (engine.highestSlot(), engine.midiMap().pad (selected).slot);
    for (int slot = 0; slot <= top; ++slot)
    {
        auto* w = engine.windowById (engine.windowInSlot (slot));
        const auto name = w != nullptr && w->uiDoc != nullptr ? juce::String::fromUTF8 (w->uiDoc->name.c_str())
                                                              : juce::String ("(no sound)");
        now.add (juce::String (slot + 1) + " - " + name);
    }

    bool changed = false, mapChanged = false;
    if (now != items)
    {
        items = now;
        sound.setItems (items);
        mapChanged = true;
    }
    const auto time = juce::Time::getMillisecondCounter();
    for (int i = 0; i < se::MidiMap::kPads; ++i)
    {
        const auto n = static_cast<size_t> (i);
        if (const auto& p = engine.midiMap().pad (i); p != shown[n])
        {
            shown[n] = p;
            changed = mapChanged = true;
        }
        if (const auto s = router.strikes (i); s != seenStrikes[n])
        {
            seenStrikes[n] = s;
            flashFrom[n] = time;
        }
        const bool on = router.isHeld (se::MidiMap::kLowNote + i)
                     || (seenStrikes[n] != 0 && time - flashFrom[n] < kFlashMs);
        if (on != lit[n]) { lit[n] = on; changed = true; }
    }
    if (mapChanged) showSelected();   // a loaded canvas, a renamed or moved Sound
    if (changed) repaint();
}

void MidiMapPanel::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < se::MidiMap::kPads; ++i)
        if (padBounds (i).contains (e.getPosition())) { select (i); return; }
}

void MidiMapPanel::paint (juce::Graphics& g)
{
    using namespace os8::colour;
    const auto text = [&g] (const juce::String& s, os8::TextStyle style, float x, float y, float w, float h,
                            juce::Justification j = juce::Justification::left)
    { os8::drawText (g, s, style, { x, y, w, h }, j); };

    g.setColour (black);
    text ("Assign sounds to MIDI notes:", os8::TextStyle::smallLabel, 9.0f, 8.0f, 250.0f, 10.0f);

    for (int i = 0; i < se::MidiMap::kPads; ++i)
    {
        const auto n = static_cast<size_t> (i);
        const auto r = padBounds (i);
        const bool pressed = i == selected, assigned = shown[n].slot != se::MapPad::kNoSlot, on = lit[n];

        juce::Path face;
        face.addRoundedRectangle (r.toFloat(), 3.0f);
        if (! pressed) os8::dropShadow (g, face, 1.0f, 1.0f, black.withAlpha (0.25f));
        g.setColour (on ? juce::Colour (0xff333333) : assigned ? juce::Colour (0xffdddddd) : juce::Colour (0xffeeeeee));
        g.fillPath (face);
        if (pressed)
        {
            os8::innerShadow (g, face, -3.0f, -3.0f, 0, juce::Colour (0xfffffdfd).withAlpha (on ? 0.25f : 0.6f));
            os8::innerShadow (g, face,  3.0f,  3.0f, 0, juce::Colour (0xff333333).withAlpha (0.5f));
        }
        else
        {
            os8::innerShadow (g, face, -3.0f, -3.0f, 0, juce::Colour (0xff9d9d9d).withAlpha (0.8f));
            os8::innerShadow (g, face,  3.0f,  3.0f, 0, juce::Colour (0xfffffdfd).withAlpha (on ? 0.25f : 0.8f));
        }
        os8::strokeInside (g, r.toFloat(), 3.0f, 1.0f, black);

        const auto ink = on ? white : black;
        const auto x = static_cast<float> (r.getX()), y = static_cast<float> (r.getY());
        g.setColour (ink);
        text (kNames[i], os8::TextStyle::largeLabel, x, y + 5.0f, 40.0f, 14.0f, juce::Justification::horizontallyCentred);
        if (assigned || pressed) g.drawRect (r.getX() + 12, r.getY() + 19, 16, 16, 1);
        text (assigned ? juce::String (shown[n].slot + 1) : juce::String ("--"), os8::TextStyle::xSmallLabel,
              x, y + 22.5f, 40.0f, 9.0f, juce::Justification::horizontallyCentred);
    }

    g.setColour (black);
    text ("Registers MIDI notes from C0 to B0", os8::TextStyle::xSmallLabel, 9.0f, 120.0f, 250.0f, 9.0f);

    // The menus' separator rule, 2 px.
    const juce::uint32 rule[] = { 0xffd9d9d9, 0xff9d9d9d, 0xff666666, 0xffffffff };
    for (int i = 0; i < 4; ++i)
    {
        g.setColour (juce::Colour (rule[i]));
        g.fillRect (juce::Rectangle<float> (0.0f, 137.0f + 0.5f * static_cast<float> (i),
                                            static_cast<float> (getWidth()), 0.5f));
    }

    g.setColour (black);
    text ("Selected note: " + juce::String (kNames[selected]) + "0", os8::TextStyle::smallLabel,
          8.0f, 151.0f, 200.0f, 10.0f);
    text ("Sound:", os8::TextStyle::smallLabel, 8.0f, 172.5f, 45.0f, 10.0f);
    text ("At note start:", os8::TextStyle::smallLabel, 8.0f, 195.0f, 86.0f, 10.0f);
    text ("At note end:", os8::TextStyle::smallLabel, 8.0f, 215.0f, 86.0f, 10.0f);
}
