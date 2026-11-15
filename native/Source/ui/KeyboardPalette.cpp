#include "KeyboardPalette.h"

#include "effects/Registry.h"

#include <cmath>

static_assert (kbd::kLowNote == se::KeyboardVoice::kLowNote && kbd::kHighNote == se::KeyboardVoice::kHighNote,
               "the voice and the keyboard picture agree on the keys");

using juce::Graphics;
using juce::Point;
using juce::Rectangle;

namespace {

/** The module this palette is a face for. Looked up once; the registry is static. */
const se::EffectModule* keyboardModule()
{
    static const se::EffectModule* m = se::findEffect ("Keyboard");
    return m;
}

} // namespace

KeyboardPalette::KeyboardPalette (se::Engine& e, se::KeyboardVoice& v)
    : engine (e), voice (v), runner (e)
{
    setSize (kWidth, kHeight);

    // The palette must never take the keyboard away from the canvas: Space, the
    // arrow keys and the Windows' own ⌘-keys all go through whatever holds focus,
    // and reaching for a piano key is not a reason to lose them. (In the modal this
    // was load-bearing for a different reason — a focused Button steals Return.)
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);

    applyButton.setWantsKeyboardFocus (false);
    applyButton.setMouseClickGrabsKeyboardFocus (false);
    applyButton.setEnabled (false);
    applyButton.onClick = [this] { applyPitch(); };
    addAndMakeVisible (applyButton);
}

// --- the target --------------------------------------------------------------

const se::SoundDocument* KeyboardPalette::doc() const
{
    if (targetId == 0) return nullptr;
    auto* w = const_cast<se::Engine&> (engine).windowById (targetId);
    return w != nullptr ? w->uiDoc.get() : nullptr;
}

int KeyboardPalette::baseNote() const { return voice.baseNote(); }

bool KeyboardPalette::hasTarget() const { return voice.hasTarget(); }

juce::Colour KeyboardPalette::targetTint() const
{
    auto* w = const_cast<se::Engine&> (engine).windowById (targetId);
    // Through the one access point, as the mixer: macos8-gray with the Color
    // Theme hidden, the chrome grey for a Window the canvas has not painted yet.
    return os8::themeColourFor (w);
}

void KeyboardPalette::setTargetWindow (uint64_t windowId)
{
    if (targetId == windowId) return;

    // A note the keyboard is holding does not carry over to a new target — it was
    // played on the old one. Anything else the old Window was doing is left alone.
    voice.setTarget (windowId);
    targetId = windowId;
    updateApplyState();
    repaint();
}

void KeyboardPalette::refresh()
{
    // MIDI moves the voice without the palette's mouse path: look at the voice.
    if (seenHeld != voice.heldNote() || seenLast != voice.lastNote() || std::abs (seenSlide - voice.slide()) > 1e-9)
    {
        seenHeld = voice.heldNote();
        seenLast = voice.lastNote();
        seenSlide = voice.slide();
        repaint();
    }

    updateApplyState();

    // The canvas assigns a Window its tint the first time it lays it out, so a
    // brand-new target can be tintless for a frame or two after it is selected.
    // The mixer's strips re-read it on the tick for the same reason.
    // The colour, not the theme index: Hide Color Theme changes it too.
    const auto tint = targetTint().getARGB();
    if (tint != lastTint)
    {
        lastTint = tint;
        repaint();
    }
}

// --- layout ------------------------------------------------------------------

Point<int> KeyboardPalette::keysOrigin() const
{
    return { kPad, kPad + 2 };   // the keys sit 2 px into the design's 41 px keybed
}

void KeyboardPalette::resized()
{
    // Figma 636:21379: the keybed's note box at 457 and Apply at 504 within the 557 px
    // content, which starts 9 px in.
    badgeRect = { kPad + 457, kPad + 2, kBadgeW, kBadgeH };
    applyButton.setBounds (kPad + 504, kPad + 9, kApplyW, kApplyH);
}

// --- paint -------------------------------------------------------------------

void KeyboardPalette::paint (Graphics& g)
{
    using namespace os8::colour;
    os8::paintPaletteFrame (g, getLocalBounds().withTrimmedBottom (kShadow));

    // The instrument. With no Window to play, the dot has no base note to sit under
    // either — the keyboard is drawn plain, which is the honest picture of a
    // keyboard wired to nothing.
    kbd::draw (g, keysOrigin(), voice.heldNote(), hasTarget() ? baseNote() : -1);

    // The readout: the note the last key played, over how far that is from the
    // sound's own base note — which is exactly what Apply will write.
    //
    // The badge wears the target Window's theme the moment one is selected — before
    // any key is played, so it says WHICH Window the keys are about to move.
    const bool aimed = doc() != nullptr;
    const bool live  = hasTarget() && voice.lastNote() >= 0;
    const auto badge = badgeRect;

    {
        juce::Path shape;
        shape.addRectangle (badge.toFloat());
        g.setColour (aimed ? targetTint() : gray100);
        g.fillRect (badge);
        os8::innerShadow (g, shape, 2.0f, 2.0f, 0, gray400.withAlpha (0.75f));
        os8::innerShadow (g, shape, -2.0f, -2.0f, 0, white.withAlpha (0.5f));
        g.setColour (black);
        g.drawRect (badge, 1);
    }

    g.setColour (black);

    if (! live)
    {
        // Aimed at nothing, or at nothing playable: a dash. An empty block would read
        // as a readout that had broken rather than one waiting.
        os8::drawText (g, "-", os8::TextStyle::largeLabel,
                       { (float) badge.getX() + 5.0f, (float) badge.getY() + 6.0f, 29.0f, 14.0f },
                       juce::Justification::horizontallyCentred);
        return;
    }

    // A pitch slide (MPE, or the wheel) shows in the offset while the key is down.
    const int semitones = voice.lastNote() - baseNote() + (voice.heldNote() >= 0 ? (int) std::lround (voice.slide()) : 0);
    os8::drawText (g, kbd::noteName (voice.lastNote()), os8::TextStyle::largeLabel,
                   { (float) badge.getX() + 5.0f, (float) badge.getY() + 6.0f, 29.0f, 14.0f },
                   juce::Justification::horizontallyCentred);

    // "st" rather than "semitones": the badge is the size of the number it carries.
    juce::String offset;
    if (semitones < 0)      offset << "-";
    else if (semitones > 0) offset << "+";   // no sign on zero: "+0" is not a number
    offset << std::abs (semitones) << "st";
    os8::drawText (g, offset, os8::TextStyle::xSmallLabel,
                   { (float) badge.getX() + 5.0f, (float) badge.getY() + 24.0f, 29.0f, 9.0f },
                   juce::Justification::horizontallyCentred);
}

// --- playing -----------------------------------------------------------------

void KeyboardPalette::strike (int note)
{
    voice.strike (note);
    updateApplyState();
    repaint();
}

void KeyboardPalette::release()
{
    voice.release();
    repaint();
}

void KeyboardPalette::allNotesOff()
{
    voice.allNotesOff();
}

void KeyboardPalette::mouseDown (const juce::MouseEvent& e)
{
    strike (kbd::noteAt (e.getPosition() - keysOrigin()));
}

void KeyboardPalette::mouseDrag (const juce::MouseEvent& e)
{
    if (voice.heldNote() < 0) return;   // the drag did not start on a key

    // Sliding across the keys retriggers, which is the only way this plays a
    // glissando — and the only forgiveness an 8px key gets from a pointer that
    // moves while the button is going down.
    const int note = kbd::noteAt (e.getPosition() - keysOrigin());
    if (note >= 0 && note != voice.heldNote()) strike (note);
}

void KeyboardPalette::mouseUp (const juce::MouseEvent&)
{
    release();
}

void KeyboardPalette::visibilityChanged()
{
    // Hiding the palette is how you cancel it, and a cancelled instrument is silent.
    if (! isVisible()) voice.midiAllOff();
}

// --- applying ----------------------------------------------------------------

void KeyboardPalette::updateApplyState()
{
    const bool live = hasTarget() && voice.lastNote() >= 0 && voice.lastNote() != baseNote();
    if (applyButton.isEnabled() != live)
    {
        applyButton.setEnabled (live);
        repaint();
    }
}

void KeyboardPalette::applyPitch()
{
    const se::EffectModule* module = keyboardModule();
    if (module == nullptr || ! hasTarget() || voice.lastNote() < 0) return;

    // Committing while a key is down would leave a note looping the document the
    // commit is about to re-rate underneath it.
    allNotesOff();

    se::Settings settings = module->defaults ? module->defaults() : se::Settings{};
    settings["semitones"] = se::Value ((int64_t) (voice.lastNote() - baseNote()));

    // Straight through EffectRunner, exactly as the effects panel's Apply goes —
    // so the edit rides the engine's RCU path and is published to the audio thread
    // the same way, and there is no second story about what applying an effect is.
    const se::EffectRunner::Outcome outcome = runner.apply (targetId, *module, settings, false);

    juce::String message;
    if (! outcome.ran)                        message = "Could not apply";
    else if (! outcome.message.empty())       message = juce::String (outcome.message);
    else if (outcome.result == se::kModCancel) message = "Keyboard: nothing to do";
    else if (se::isError (outcome.result))    message = "Keyboard failed";
    else                                      message = "Keyboard applied";

    if (onResult) onResult (message);
    if (outcome.ran && onDocumentChanged) onDocumentChanged (targetId);

    // The rate moved, so the same key is a different offset from the base note now.
    updateApplyState();
    repaint();
}
