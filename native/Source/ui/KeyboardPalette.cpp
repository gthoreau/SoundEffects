#include "KeyboardPalette.h"

#include "effects/Registry.h"

#include <cmath>

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

KeyboardPalette::KeyboardPalette (se::Engine& e)
    : engine (e), runner (e)
{
    setSize (kWidth, kHeight);

    // The palette must never take the keyboard away from the canvas: Space, the
    // arrow keys and the Windows' own ⌘-keys all go through whatever holds focus,
    // and reaching for a piano key is not a reason to lose them. (In the modal this
    // was load-bearing for a different reason — a focused Button steals Return.)
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);

    applyButton.setWantsKeyboardFocus (false);
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

int KeyboardPalette::baseNote() const
{
    const auto* d = doc();
    return d != nullptr ? d->baseFreq : 60;
}

bool KeyboardPalette::hasTarget() const
{
    const auto* d = doc();
    return d != nullptr && d->maxTrackSize() > 0;
}

juce::Colour KeyboardPalette::targetTint() const
{
    auto* w = const_cast<se::Engine&> (engine).windowById (targetId);
    // A Window the canvas has not painted yet has no tint, and the chrome grey is
    // what everything untinted in this UI is made of. Same fallback the mixer uses.
    return w != nullptr && w->tint >= 0 ? sys7::tintFromPalette (w->tint) : sys7::kFace;
}

bool KeyboardPalette::isRolling() const
{
    auto* w = const_cast<se::Engine&> (engine).windowById (targetId);
    return w != nullptr && w->transport != nullptr
        && w->transport->uiState() == se::TransportState::playing;
}

void KeyboardPalette::setTargetWindow (uint64_t windowId)
{
    if (targetId == windowId) return;

    // A note the keyboard is holding does not carry over to a new target — it was
    // played on the old one. Anything else the old Window was doing is left alone.
    allNotesOff();
    targetId = windowId;
    updateApplyState();
    repaint();
}

void KeyboardPalette::refresh()
{
    updateApplyState();

    // The canvas assigns a Window its tint the first time it lays it out, so a
    // brand-new target can be tintless for a frame or two after it is selected.
    // The mixer's strips re-read it on the tick for the same reason.
    auto* w = engine.windowById (targetId);
    const int tint = w != nullptr ? w->tint : -1;
    if (tint != lastTint)
    {
        lastTint = tint;
        repaint();
    }
}

// --- layout ------------------------------------------------------------------

Point<int> KeyboardPalette::keysOrigin() const
{
    return { kPad, kPad };
}

void KeyboardPalette::resized()
{
    auto body = getLocalBounds().withTrimmedRight (kShadow).withTrimmedBottom (kShadow);
    auto tail = body.reduced (kPad, 0).withTrimmedLeft (kbd::kPicW + kGap);

    // The badge is aligned to the KEYBOARD's top rather than centred on the strip:
    // the row of keys is what the eye tracks along, and a readout that floats
    // half a line below it reads as belonging to the strip instead of to the note.
    badgeRect = tail.removeFromLeft (kBadgeW).withHeight (kBadgeH).withY (kPad);
    tail.removeFromLeft (kGap);
    applyButton.setBounds (tail.removeFromLeft (kApplyW)
                               .withSizeKeepingCentre (kApplyW, kApplyH)
                               .withY (kPad + (kbd::kPicH - kApplyH) / 2));
}

// --- paint -------------------------------------------------------------------

void KeyboardPalette::paint (Graphics& g)
{
    const auto body = getLocalBounds().withTrimmedRight (kShadow)
                                      .withTrimmedBottom (kShadow).toFloat();

    // System 7 drew a solid black shadow under what floats above a window and
    // nothing under what is docked in one. It is what says this strip is over the
    // canvas rather than part of it — and it is the ViewPalette's own shadow, so
    // the two read as one stack.
    g.setColour (sys7::kInk.withAlpha (0.35f));
    g.fillRoundedRectangle (body.translated ((float) kShadow, (float) kShadow), 5.0f);

    g.setColour (sys7::kPanel);
    g.fillRoundedRectangle (body, 5.0f);
    g.setColour (sys7::kInk);
    g.drawRoundedRectangle (body.reduced (0.5f), 5.0f, 1.0f);

    // The instrument. With no Window to play, the dot has no base note to sit under
    // either — the keyboard is drawn plain, which is the honest picture of a
    // keyboard wired to nothing.
    kbd::draw (g, keysOrigin(), heldNote, hasTarget() ? baseNote() : -1);

    // The readout: the note the last key played, over how far that is from the
    // sound's own base note — which is exactly what Apply will write.
    //
    // The badge wears the target Window's tint the moment one is selected — before
    // any key is played. That is the point of it: with no room for a title, the
    // tint is what says WHICH Window the keys are about to move, and it changes
    // under the pointer as the canvas selection does.
    const bool aimed = doc() != nullptr;
    const bool live  = hasTarget() && lastNote >= 0;
    auto badge = badgeRect;

    if (aimed)
    {
        g.setColour (targetTint());
        g.fillRoundedRectangle (badge.toFloat(), 4.0f);
    }
    g.setColour (aimed ? sys7::kInk : sys7::kDim);
    g.drawRoundedRectangle (badge.toFloat().reduced (0.5f), 4.0f, 1.0f);

    if (! live)
    {
        // Aimed at nothing, or at nothing playable: a dash. An empty tinted block
        // would read as a readout that had broken rather than one waiting. WHY it
        // is idle (no Window, or a Window with no audio) is already on screen in
        // the effects panel's header, which is up whenever this strip is.
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93")), badge,
                    juce::Justification::centred, false);
        return;
    }

    const int semitones = lastNote - baseNote();

    // Ink on the tint, not white on black: the twelve tints are pastels (Sys7.cpp),
    // and they carry dark text exactly as the Windows they came from do.
    g.setColour (sys7::kInk);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText (kbd::noteName (lastNote), badge.removeFromTop (kBadgeH * 3 / 5).withTrimmedTop (2),
                juce::Justification::centred, false);

    // "st" rather than "semitones": the badge is the size of the number it carries,
    // and the unit is standard enough not to need the room.
    juce::String offset;
    if (semitones < 0)      offset << juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92"));
    else if (semitones > 0) offset << "+";   // no sign on zero: "+0" is not a number
    offset << std::abs (semitones) << "st";

    // A shade lighter than the note it qualifies — the size difference alone is not
    // much of a hierarchy at 10pt, and on a pastel there is room to lift the note
    // without the offset going faint.
    g.setColour (sys7::kInk.withAlpha (0.7f));
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    // Fitted, not drawn: a document whose base note is not the usual 60 can put
    // three digits in here, and a clipped offset beside an irreversible button is
    // the one thing this readout must never do.
    g.drawFittedText (offset, badge.withTrimmedBottom (2), juce::Justification::centred, 1);
}

// --- playing -----------------------------------------------------------------

void KeyboardPalette::strike (int note)
{
    if (note < kbd::kLowNote || note > kbd::kHighNote) return;
    if (! hasTarget()) return;

    heldNote = note;
    lastNote = note;

    // The entire synthesis engine: twelve-tone equal temperament out of one pow(),
    // and a "sampler" that is a multiply. Auditioning never touches the document —
    // the scale rides along with the transport. Only Apply commits.
    const double rateScale = std::pow (2.0, (note - baseNote()) / 12.0);

    // ORDER MATTERS. Our own note is tested first, because once the keyboard has
    // started one the transport is rolling too — and asking "is it rolling?" first
    // would reclassify our own note as borrowed, so letting go would leave it
    // playing forever. It also sidesteps the lag between posting play() and the
    // audio thread publishing the new state.
    if (voiceId != 0 && voiceId == targetId)
    {
        // Sliding across the keys is a glissando, not a stutter — and how this
        // voice ends was decided when it began.
        engine.setRateScale (targetId, rateScale);
    }
    // Already rolling under its own steam? Then BORROW it: re-pitch the transport
    // where it stands. The Window is playing because the user set it playing, and
    // what they asked for is that playback PITCHED, not replaced. Re-issuing play()
    // here would restart the playhead, drop the loop's phase against every other
    // Window, and re-snapshot the loop region from the CURRENT selection — silently
    // re-aiming a loop the user had aimed somewhere else.
    else if (isRolling())
    {
        engine.setRateScale (targetId, rateScale);
        voiceId = targetId;
        startedVoice = false;
    }
    else
    {
        se::PlayOptions opts;
        opts.rateScale = rateScale;
        // ALWAYS looping, and NO soloTrack. The first is what makes a held key
        // sustain — the thing the original's checkbox switched on. The second is the
        // original's one-channel limitation, deliberately dropped: with soloTrack
        // left at −1 the transport plays the whole selected track range, so the
        // keyboard plays every selected track rather than apologising for the first.
        opts.loop = true;
        engine.play (targetId, opts);
        voiceId = targetId;
        startedVoice = true;
    }

    updateApplyState();
    repaint();
}

void KeyboardPalette::release()
{
    if (heldNote < 0) return;

    heldNote = -1;
    // Looping is always on now, so a note sustains until the key comes up. Nothing
    // would ever stop on its own.
    allNotesOff();
    repaint();
}

void KeyboardPalette::allNotesOff()
{
    // Two endings, and the flag decides which. Only what the keyboard STARTED is
    // stopped; a Window that was already rolling is handed back at its own pitch,
    // still rolling. Nothing else on the canvas is touched at all — every Window
    // sounds at once here, and almost none of them are the keyboard's business.
    if (voiceId != 0)
    {
        if (startedVoice) engine.stop (voiceId);
        else              engine.setRateScale (voiceId, 1.0);

        voiceId = 0;
        startedVoice = false;
    }

    heldNote = -1;
}

void KeyboardPalette::mouseDown (const juce::MouseEvent& e)
{
    strike (kbd::noteAt (e.getPosition() - keysOrigin()));
}

void KeyboardPalette::mouseDrag (const juce::MouseEvent& e)
{
    if (heldNote < 0) return;   // the drag did not start on a key

    // Sliding across the keys retriggers, which is the only way this plays a
    // glissando — and the only forgiveness an 8px key gets from a pointer that
    // moves while the button is going down.
    const int note = kbd::noteAt (e.getPosition() - keysOrigin());
    if (note >= 0 && note != heldNote) strike (note);
}

void KeyboardPalette::mouseUp (const juce::MouseEvent&)
{
    release();
}

void KeyboardPalette::visibilityChanged()
{
    // Hiding the palette is how you cancel it, and a cancelled instrument is silent.
    if (! isVisible()) allNotesOff();
}

// --- applying ----------------------------------------------------------------

void KeyboardPalette::updateApplyState()
{
    const bool live = hasTarget() && lastNote >= 0 && lastNote != baseNote();
    if (applyButton.isEnabled() != live)
    {
        applyButton.setEnabled (live);
        repaint();
    }
}

void KeyboardPalette::applyPitch()
{
    const se::EffectModule* module = keyboardModule();
    if (module == nullptr || ! hasTarget() || lastNote < 0) return;

    // Committing while a key is down would leave a note looping the document the
    // commit is about to re-rate underneath it.
    allNotesOff();

    se::Settings settings = module->defaults ? module->defaults() : se::Settings{};
    settings["semitones"] = se::Value ((int64_t) (lastNote - baseNote()));

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
