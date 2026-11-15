#include "MidiKeyboardInput.h"

#include "engine/KeyboardVoice.h"

namespace {
constexpr double kPlainBendRange = 2.0;   // semitones either way, plain pitch wheel
}

MidiKeyboardInput::MidiKeyboardInput (se::KeyboardVoice& target)
    : palette (target)
{
    juce::MPEZoneLayout layout;
    layout.setLowerZone (15);          // channels 2..16; per-note range 48, master 2
    mpe.setZoneLayout (layout);
    mpe.addListener (this);
}

MidiKeyboardInput::~MidiKeyboardInput() = default;

void MidiKeyboardInput::handle (const se::MidiEvent& e)
{
    switch (e.type)
    {
        case se::MidiEvent::noteOn:  palette.midiNoteOn (e.note, e.note, 0.0);   break;
        case se::MidiEvent::noteOff: palette.midiNoteOff (e.note);               break;
        case se::MidiEvent::bend:
            palette.midiPlainBend (se::midibend::semitones ((int) e.value, kPlainBendRange));
            break;
        case se::MidiEvent::allOff:
            mpe.releaseAllNotes();
            palette.midiAllOff();
            break;
        case se::MidiEvent::raw:
        {
            const uint8_t bytes[3] { e.status, e.note, e.d2 };
            const int n = (e.status & 0xf0) == 0xc0 || (e.status & 0xf0) == 0xd0 ? 2 : 3;
            mpe.processNextMidiEvent (juce::MidiMessage (bytes, n));
            break;
        }
    }
}

void MidiKeyboardInput::noteAdded (juce::MPENote n)
{
    palette.midiNoteOn (kMpeIdBase + (int) n.noteID, n.initialNote, n.totalPitchbendInSemitones);
}

void MidiKeyboardInput::notePitchbendChanged (juce::MPENote n)
{
    palette.midiBend (kMpeIdBase + (int) n.noteID, n.totalPitchbendInSemitones);
}

void MidiKeyboardInput::noteReleased (juce::MPENote n)
{
    palette.midiNoteOff (kMpeIdBase + (int) n.noteID);
}
