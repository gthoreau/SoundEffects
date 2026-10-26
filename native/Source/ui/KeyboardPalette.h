// KeyboardPalette — the Keyboard module as a floating widget, SPEC §8 #18.
//
// The original opened a modal window (WIND 128) over the document you had picked,
// auditioned it, and closed. This is the same instrument, rebuilt as a palette that
// stays up: it floats under the View palette, it is shown and hidden from a button
// there, and at any moment it plays and pitches THE WINDOW THAT IS SELECTED NOW.
//
// That turns a one-shot dialog into a live pitch control for the canvas — click a
// key and hear the selected Window at that pitch, click another Window and the same
// keyboard is now playing that one. Apply commits.
//
// ## A key does one of two things, and the difference is the whole feel of it
//
// If the Window is STOPPED, a key starts it looping at that pitch and letting go
// stops it: the note is the playback.
//
// If the Window is ALREADY ROLLING, a key re-pitches the transport WHERE IT STANDS
// and letting go puts the rate back, leaving it running. Nothing restarts, the
// playhead never jumps, the loop keeps its phase against every other Window on the
// canvas, and the loop region is not re-snapshotted from a selection that may have
// moved since. That is what makes this an interactive pitch control over a live
// canvas rather than a dialog that interrupts one — you can ride a loop up and down
// the keyboard and hand it back exactly as you found it.
//
// The alternative — stop, then restore afterwards — was considered and is worse in
// every case that matters: it gaps the loop for the length of the note, and
// "restore" would have to reconstruct a play state (region, phase, Advance, pause)
// the transport does not publish. Re-pitching in place needs none of it.
//
// ## What is recovered and what is not
//
// The keyboard itself is exact — PICT 128, redrawn from measurements (see
// KeyboardArt.h), and the dot under the document's base note is the original's.
// Everything around it is deliberately NOT the original:
//
//   - no modal window, no title bar, no System 7 dialog chrome: it is a palette in
//     the ViewPalette idiom (rounded body, hairline frame, the solid drop shadow
//     System 7 gave things that float above a window),
//   - the keyboard draws at 1:1 — half the size the modal showed it at,
//   - "Use selection for looping" is gone and looping is ALWAYS on. It was the
//     switch that turned a one-shot into an instrument, and an instrument that has
//     to be switched on is a checkbox in front of the point,
//   - Cancel is gone: nothing is pending, so there is nothing to cancel. Closing
//     the palette IS cancelling,
//   - "Change Pitch" is "Apply", the word the rest of this app uses for the
//     irreversible thing (§1),
//   - and the ONE-CHANNEL LIMITATION IS GONE. The original could only play the
//     first selected channel and apologised for it in STR# 1000 #3; this plays
//     every selected track. The apology is still in the module's strings — it is
//     recovered ABI data, indexed by result code — but nothing raises it any more.
//
// ## What Apply does, and does not do
//
// It runs the Keyboard module through EffectRunner, exactly as the effects panel's
// Apply runs any other module — so it is the module's own effect(): the document's
// playback rate is edited and NOT ONE BYTE of audio moves. The header now lies
// about the sound. Resample Rate is how you make the lie true, and nothing here
// will do it for you.

#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "KeyboardArt.h"
#include "Os8.h"
#include "Os8Widgets.h"
#include "effects/EffectRunner.h"
#include "engine/Engine.h"

#include <functional>

class KeyboardPalette : public juce::Component
{
public:
    explicit KeyboardPalette (se::Engine& engine);

    /** The frame is the design's 575 x 60 Canvas-palette (Figma 636:21379), with its
        soft shadow 2 px below it inside our bounds. */
    static constexpr int kShadow = os8::kPaletteShadow;
    static constexpr int kPad = 9;
    static constexpr int kFrameW = 575;
    static constexpr int kFrameH = 60;
    /**
     * The readout badge ("note-st-deviation"): the note name over its offset in
     * semitones, on a block wearing the TARGET WINDOW'S THEME colour.
     *
     * The theme is this app's way of saying which Window a thing belongs to — the
     * mixer's strips wear it for exactly that reason, so the eye pairs strip to
     * Window without reading a name. The keyboard is aimed at one Window at a time
     * and re-aims as the canvas selection moves, so the badge is where that has to
     * show; the design leaves its fill to the Placeholder-gray-100 slot for this.
     */
    static constexpr int kBadgeW = 39;
    static constexpr int kBadgeH = 39;
    static constexpr int kApplyW = 53;
    static constexpr int kApplyH = 25;

    static constexpr int kWidth  = kFrameW;
    static constexpr int kHeight = kFrameH + kShadow;

    /**
     * The Window the keyboard is aimed at — the canvas's selection, pushed in on
     * every change. 0 for none, which greys Apply and stops the keys sounding.
     *
     * This is the whole difference from the original: the target is not captured
     * when the window opens, it is whatever is selected at the moment you press a
     * key. Playing note-by-note across two Windows is a matter of clicking between
     * them.
     */
    void setTargetWindow (uint64_t windowId);

    /** Re-derive what the target looks like now. Driven by the editor's timer, so
        Apply greys the moment the audio it would eat goes away. */
    void refresh();

    /** Apply rewrote `windowId`'s document — the canvas must redraw it. Fires with
        the outcome in the module's own voice, for the effects panel to show. */
    std::function<void (uint64_t windowId)> onDocumentChanged;
    std::function<void (const juce::String& message)> onResult;

    /**
     * End whatever this palette is driving, and touch nothing else.
     *
     * Playback here is global and additive — every open Window sounds at once, and
     * most of them are playing because someone pressed their own transport. So a
     * Window the keyboard STARTED is stopped, a Window it only BORROWED is handed
     * back at its own pitch and left running, and no other Window is touched at
     * all. See `voiceId` and `startedVoice`.
     */
    void allNotesOff();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void visibilityChanged() override;

private:
    /** The document the keyboard is aimed at, or null. */
    const se::SoundDocument* doc() const;
    /** The base note the dot marks: the target's, or the default 60. */
    int baseNote() const;
    /** Is there a Window with audio in it to play? */
    bool hasTarget() const;
    /** Is the target's own transport already rolling? Then a key pitches it in
        place rather than restarting it — and letting go must not stop it. */
    bool isRolling() const;
    /** The target Window's tint, or the chrome grey for one the canvas has not
        painted yet. Meaningless unless a Window is actually selected. */
    juce::Colour targetTint() const;

    /** Where the keyboard picture sits inside the palette. */
    juce::Point<int> keysOrigin() const;

    /** Play `note` on every selected track of the current target. */
    void strike (int note);
    void release();
    void applyPitch();
    /** Apply is live only when a key has been played on a real target, and the key
        was not the base note — committing unison is a no-op the module itself
        refuses, and a button that does nothing should say so before it is pressed. */
    void updateApplyState();

    se::Engine& engine;
    se::EffectRunner runner;

    uint64_t targetId = 0;

    Os8Button applyButton { "Apply", os8::ButtonKind::primary };

    /** The last key CLICKED, which is what Apply commits. −1 until one is played. */
    int lastNote = -1;
    /** The key currently under the pointer, drawn inverted. −1 when none. */
    int heldNote = -1;

    /**
     * The Window this palette is currently driving, or 0 when it is idle.
     *
     * Held separately from `targetId` because the two come apart exactly when it
     * matters: the moment the canvas selection moves, the Window the keyboard is
     * sounding is the one it is no longer aimed at. Acting on "the target" at that
     * point disturbed whatever the user had left playing in the Window they had
     * just clicked away from — every open Window sounds at once here, and almost
     * none of them are the keyboard's to touch.
     */
    uint64_t voiceId = 0;

    /**
     * Did the keyboard START that Window, or only BORROW a rolling one's pitch?
     *
     * The two have opposite endings, and getting it wrong is audible either way:
     *
     *   - started (the Window was stopped): the note IS its playback, so letting go
     *     stops it;
     *   - borrowed (the Window was already rolling): the note is that playback,
     *     pitched. Letting go must put the rate back and leave it running — this is
     *     the Window being interactively pitched, not interrupted, and stopping it
     *     would end a loop the keyboard never started.
     */
    bool startedVoice = false;

    /** The tint the badge last painted, so the 30 Hz tick can notice a change
        without repainting the strip every frame. −2 = never sampled. */
    int lastTint = -2;

    juce::Rectangle<int> badgeRect;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeyboardPalette)
};
