// Declicking (0.6.0 step 3): no playback event may jump the output.
//
// Every scenario renders a signal that makes a click obvious (a DC offset, a
// full-scale sine caught on its peak) through one event, and reports the largest
// difference between two neighbouring output samples from just before the event on.
// The sine's own slope is 0.0131 per sample (100 Hz at 48 kHz), so anything far
// above that is the event's doing, and every scenario must stay under kThreshold.

#include "run_declick.h"

#include "engine/Engine.h"
#include "engine/KeyboardVoice.h"
#include "engine/SampleFormat.h"
#include "engine/Tape.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr double kRate = 48000.0;
constexpr int    kBlock = 256;
constexpr size_t kPeriod = 480;     // 100 Hz
constexpr size_t kPeak = 120;       // the sine's top within a period
constexpr int    kAfter = 4096;     // rendered past the event: many times any ramp

// Above the sine's own step at FF or an octave up (0.026) plus a ramp's (0.008);
// without declicking these events jumped 0.5 to 1.
constexpr float kThreshold = 0.04f;

enum class Signal { dc, sine };

const char* nameOf (Signal s) { return s == Signal::dc ? "DC  " : "sine"; }

/** 16-bit mono, document rate == device rate: one output frame per sound frame.
    DC sits at half scale; the sine is full scale and starts at zero. */
std::unique_ptr<se::SoundDocument> document (Signal s, std::initializer_list<size_t> trackFrames)
{
    se::DocumentInit init;
    init.numTracks = (int) trackFrames.size();
    init.bps = 16;
    init.rateHz = kRate;
    init.sizeBytes = 2;
    auto doc = std::make_unique<se::SoundDocument> (init);
    int t = 0;
    for (size_t len : trackFrames)
    {
        auto& data = doc->tracks[(size_t) t].data;
        data.assign (len * 2, 0);
        for (size_t i = 0; i < len; ++i)
            se::setSample (data.data(), i * 2,
                           s == Signal::dc ? 16384
                                           : (int) std::lround (32767.0 * std::sin (6.283185307179586 * (double) i / (double) kPeriod)),
                           16);
        doc->tracks[(size_t) t].outLeft = doc->tracks[(size_t) t].outRight = true;
        ++t;
    }
    return doc;
}

struct Rig
{
    se::Engine engine;
    std::vector<float> left, right;

    Rig() { engine.prepare (kRate, kBlock); }

    uint64_t add (std::unique_ptr<se::SoundDocument> d) { return engine.addWindow (std::move (d)); }

    void run (int frames)
    {
        std::vector<float> l ((size_t) kBlock), r ((size_t) kBlock);
        while (frames > 0)
        {
            const int n = std::min (frames, kBlock);
            engine.render (l.data(), r.data(), n);
            left.insert (left.end(), l.begin(), l.begin() + n);
            right.insert (right.end(), r.begin(), r.begin() + n);
            frames -= n;
        }
    }

    /** The largest step, either side, among the samples from `from` on. */
    float jumpFrom (size_t from) const
    {
        float worst = 0.0f;
        for (size_t i = std::max<size_t> (from, 1); i < left.size(); ++i)
            worst = std::max ({ worst, std::fabs (left[i] - left[i - 1]), std::fabs (right[i] - right[i - 1]) });
        return worst;
    }

    /** Render `before` frames, do `event`, render on: the jump from the join. */
    float across (int before, const std::function<void()>& event, int after = kAfter)
    {
        run (before);
        const size_t join = left.size();
        event();
        run (after);
        return jumpFrom (join);
    }
};

se::PlayOptions looping()
{
    se::PlayOptions o;
    o.loop = true;
    o.useLoopRegion = true;
    return o;
}

// Frames rendered before an event, so the next sample due is the sine's peak.
constexpr int kToPeak = (int) (3 * kPeriod + kPeak);
// A sound that ends on its peak and starts at zero: the worst place to wrap.
constexpr size_t kOffZero = 4 * kPeriod + kPeak + 1;

// ---- the scenarios -------------------------------------------------------------

float stopIt (Signal s)
{
    Rig r; const auto id = r.add (document (s, { 48000 }));
    r.engine.play (id, looping());
    return r.across (kToPeak, [&] { r.engine.stop (id); });
}

float pauseIt (Signal s)
{
    Rig r; const auto id = r.add (document (s, { 48000 }));
    r.engine.play (id, looping());
    return r.across (kToPeak, [&] { r.engine.pause (id); });
}

float resumeIt (Signal s)
{
    Rig r; const auto id = r.add (document (s, { 48000 }));
    r.engine.play (id, looping());
    r.run (kToPeak);
    r.engine.pause (id);
    return r.across (kAfter, [&] { r.engine.pause (id); });
}

float playFromCaret (Signal s)
{
    Rig r; auto d = document (s, { 48000 });
    d->selSt = d->selEnd = kPeak * 2;
    const auto id = r.add (std::move (d));
    return r.across (kBlock, [&] { se::PlayOptions o; o.fromCaret = true; r.engine.play (id, o); });
}

float restartWhilePlaying (Signal s)
{
    Rig r; const auto id = r.add (document (s, { 48000 }));
    r.engine.play (id, looping());
    return r.across (kToPeak, [&] { r.engine.play (id, looping()); });
}

float onceRunsOut (Signal s)
{
    Rig r; const auto id = r.add (document (s, { kOffZero }));
    r.engine.play (id, {});
    return r.across (kBlock, [] {});
}

float closeSound (Signal s)
{
    Rig r; const auto id = r.add (document (s, { 48000 }));
    r.engine.play (id, looping());
    return r.across (kToPeak, [&] { r.engine.removeWindow (id); });
}

/** A loop heard for several laps; the first block (the start) is not what is asked about. */
float laps (std::unique_ptr<se::SoundDocument> d, se::PlayDirection dir = se::PlayDirection::forward,
            bool fast = false, bool fromCaret = false)
{
    Rig r; const auto id = r.add (std::move (d));
    r.engine.setDirection (id, dir);
    se::PlayOptions o = looping();
    o.fromCaret = fromCaret;
    r.engine.play (id, o);
    if (fast) r.engine.advance (id);
    r.run (kBlock);
    const size_t from = r.left.size();
    r.run (6 * (int) kOffZero);
    return r.jumpFrom (from);
}

float wrapWhole (Signal s)    { return laps (document (s, { kOffZero })); }
float wrapBackward (Signal s) { return laps (document (s, { kOffZero }), se::PlayDirection::backward); }
float wrapFast (Signal s)     { return laps (document (s, { kOffZero }), se::PlayDirection::forward, true); }
float wrapOwnLengths (Signal s)
{
    auto d = document (s, { kOffZero, 3 * kPeriod + 3 * kPeak + 1 });   // ends on the trough
    d->tracks[0].outRight = false;
    d->tracks[1].outLeft = false;
    return laps (std::move (d));
}
float wrapSelection (Signal s)
{
    auto d = document (s, { 48000 });
    d->selSt = 2 * kPeriod * 2;
    d->selEnd = d->selSt + (2 * kPeriod + kPeak + 1) * 2;
    return laps (std::move (d));
}
float wrapRegion (Signal s)
{
    auto d = document (s, { 48000 });
    d->loopSt = 0;
    d->loopEnd = (2 * kPeriod + kPeak + 1) * 2;
    return laps (std::move (d));
}
/** A track that ends on its peak inside the looped selection, and comes back. */
float wrapShortTrack (Signal s)
{
    auto d = document (s, { 48000, 2 * kPeriod + kPeak + 1 });
    d->tracks[0].outRight = false;
    d->tracks[1].outLeft = false;
    d->firstSelTrack = 1; d->lastSelTrack = 2;
    d->selSt = kPeriod * 2;
    d->selEnd = 4 * kPeriod * 2;
    return laps (std::move (d));
}
/** From a caret past track 2's end: it comes in at its next lap. */
float caretLateTrack (Signal s)
{
    auto d = document (s, { 48000, kOffZero });
    d->tracks[0].outRight = false;
    d->tracks[1].outLeft = false;
    d->selSt = d->selEnd = (kOffZero + 500) * 2;
    return laps (std::move (d), se::PlayDirection::forward, false, true);
}

// The Keyboard: the voice drives the engine as the mouse and MIDI do.
struct Keys
{
    Rig r;
    se::KeyboardVoice voice { r.engine };
    uint64_t id;
    int note;

    explicit Keys (Signal s) : id (r.add (document (s, { 48000 }))), note (voice.baseNote()) {}
};

float keyStrike (Signal s)
{
    Keys k (s);
    // The Keyboard starts at the sound's start, where the sine is zero: strike a
    // sound whose selection begins on the peak.
    auto* d = k.r.engine.editDoc (k.id);
    d->selSt = kPeak * 2; d->selEnd = d->selSt + 100 * kPeriod * 2;
    k.r.engine.commitEdit (k.id);
    return k.r.across (kBlock, [&] { k.voice.strike (k.note); });
}

float keyRelease (Signal s)
{
    Keys k (s);
    k.voice.strike (k.note);
    return k.r.across (kToPeak, [&] { k.voice.release(); });
}

/** Let go and struck again before the audio thread has seen the release. */
float keyRetrigger (Signal s)
{
    Keys k (s);
    k.voice.strike (k.note);
    return k.r.across (kToPeak, [&] { k.voice.release(); k.voice.strike (k.note); });
}

/** The same through MIDI: note off and note on in one pump of the queue. */
float midiRetrigger (Signal s)
{
    Keys k (s);
    k.voice.midiNoteOn (k.note, k.note, 0.0);
    return k.r.across (kToPeak, [&] { k.voice.midiNoteOff (k.note); k.voice.midiNoteOn (k.note, k.note, 0.0); });
}

float midiGlide (Signal s)
{
    Keys k (s);
    k.voice.midiNoteOn (k.note, k.note, 0.0);
    return k.r.across (kToPeak, [&] { k.voice.midiNoteOn (k.note + 12, k.note + 12, 0.0); });
}

/** An MPE slide: a re-rate every few milliseconds, over loop wraps. */
float mpeSlide (Signal s)
{
    Keys k (s);
    auto* d = k.r.engine.editDoc (k.id);
    d->selSt = 0; d->selEnd = (2 * kPeriod + kPeak + 1) * 2;
    k.r.engine.commitEdit (k.id);
    k.voice.midiNoteOn (1000, k.note, 0.0);
    k.r.run (kBlock);
    const size_t from = k.r.left.size();
    for (int i = 0; i < 600; ++i)
    {
        k.voice.midiBend (1000, 12.0 * std::sin (i * 0.05));
        k.r.run (128);
    }
    return k.r.jumpFrom (from);
}

// Gain switches, each caught on the peak of a looping Sound.
float gainEvent (Signal s, const std::function<void (Rig&, se::Window&, se::Window&)>& setup,
                 const std::function<void (Rig&, se::Window&, se::Window&)>& event)
{
    Rig r;
    const auto id = r.add (document (s, { 48000 }));
    const auto other = r.add (document (Signal::dc, { 0 }));   // silent: only its solo matters
    auto& w = *r.engine.windowById (id);
    auto& o = *r.engine.windowById (other);
    setup (r, w, o);
    r.engine.play (id, looping());
    return r.across (kToPeak, [&] { event (r, w, o); });
}

void nothing (Rig&, se::Window&, se::Window&) {}

float muteOn (Signal s)      { return gainEvent (s, nothing, [] (Rig&, se::Window& w, se::Window&) { w.muted = true; }); }
float muteOff (Signal s)     { return gainEvent (s, [] (Rig&, se::Window& w, se::Window&) { w.muted = true; },
                                                    [] (Rig&, se::Window& w, se::Window&) { w.muted = false; }); }
float soloOn (Signal s)      { return gainEvent (s, nothing, [] (Rig&, se::Window&, se::Window& o) { o.soloed = true; }); }
float soloOff (Signal s)     { return gainEvent (s, [] (Rig&, se::Window&, se::Window& o) { o.soloed = true; },
                                                    [] (Rig&, se::Window&, se::Window& o) { o.soloed = false; }); }
float mainMuteOn (Signal s)  { return gainEvent (s, nothing, [] (Rig& r, se::Window&, se::Window&) { r.engine.setMasterMuted (true); }); }
float mainMuteOff (Signal s) { return gainEvent (s, [] (Rig& r, se::Window&, se::Window&) { r.engine.setMasterMuted (true); },
                                                    [] (Rig& r, se::Window&, se::Window&) { r.engine.setMasterMuted (false); }); }
float faderJump (Signal s)   { return gainEvent (s, nothing, [] (Rig&, se::Window& w, se::Window&) { w.gain = 0.0f; }); }
float balanceJump (Signal s) { return gainEvent (s, nothing, [] (Rig&, se::Window& w, se::Window&) { w.balance = 1.0f; }); }
float mainJump (Signal s)    { return gainEvent (s, nothing, [] (Rig& r, se::Window&, se::Window&) { r.engine.setMasterGain (0.0f); }); }

// The Magnetophon: a tape recorded from the input, then played.
struct Reel
{
    Rig r;
    se::TapeDeck deck { r.engine };

    Reel (Signal s, size_t frames)
    {
        se::TapeSource in;
        in.kind = se::AudioSourceKind::external;
        in.stereo = false;
        deck.setSource (in);
        deck.record();
        std::vector<float> x ((size_t) kBlock), l ((size_t) kBlock), rr ((size_t) kBlock);
        for (size_t done = 0; done < frames;)
        {
            const size_t n = std::min ((size_t) kBlock, frames - done);
            for (size_t i = 0; i < n; ++i)
                x[i] = s == Signal::dc ? 0.5f
                     : (float) std::sin (6.283185307179586 * (double) (done + i) / (double) kPeriod);
            const float* ins[1] = { x.data() };
            r.engine.render (l.data(), rr.data(), (int) n, ins, 1);
            deck.service();
            done += n;
        }
        deck.stop();
        for (int i = 0; i < 100 && deck.finishing(); ++i) { r.run (kBlock); deck.service(); }
        deck.tape().seek (0);
        r.left.clear(); r.right.clear();
        r.run (kBlock);
    }
};

float tapeStart (Signal s)
{
    Reel t (s, 48000);
    t.deck.seek (kPeak);
    return t.r.across (kBlock, [&] { t.deck.play(); });
}

float tapeHalt (Signal s)
{
    Reel t (s, 48000);
    t.deck.play();
    return t.r.across (kToPeak, [&] { t.deck.stop(); });
}

float tapeJump (Signal s)
{
    Reel t (s, 48000);
    t.deck.play();
    return t.r.across (kToPeak, [&] { t.deck.seek (10 * kPeriod + 3 * kPeak); });
}

float tapeRunsOut (Signal s)
{
    Reel t (s, kOffZero);
    return t.r.across (kBlock, [&] { t.deck.play(); });
}

float tapeCleared (Signal s)
{
    Reel t (s, 48000);
    t.deck.play();
    return t.r.across (kToPeak, [&] { t.deck.clear(); });
}

float tapePunchIn (Signal s)
{
    Reel t (s, 48000);
    t.deck.play();
    return t.r.across (kToPeak, [&] { t.deck.record(); });
}

float tapeGrabbed (Signal s)
{
    Reel t (s, 48000);
    t.deck.play();
    return t.r.across (kToPeak, [&] { t.deck.scrubBegin(); });
}

// A take deleted under the playing tape: the one being played, or one before it.
float deleteEvent (Signal s, size_t playFrom, int take)
{
    Reel t (s, 48000);
    auto& model = t.deck.tape();
    for (size_t at : { (size_t) 20000, (size_t) 20000 + 5 * kPeriod + kPeak })   // three takes
    {
        model.seek (at);
        model.record (48000.0);
        model.recorded (1);
        model.stop();
    }
    model.seek (playFrom);
    t.deck.play();
    return t.r.across (kToPeak, [&] { t.deck.deleteTake (take); t.r.run (kBlock); t.deck.service(); });
}
float tapeTakeDeleted (Signal s)    { return deleteEvent (s, 100, 0); }
float tapeEarlierDeleted (Signal s) { return deleteEvent (s, 30000, 0); }

// The tape playing, silenced and let back by a Sound's solo, and by its own.
float tapeSoloEvent (Signal s, bool soloedFirst)
{
    Reel t (s, 48000);
    const auto id = t.r.add (document (Signal::dc, { 48000 }));   // stopped: silent
    if (soloedFirst) t.r.engine.clickSolo (id, false);
    t.deck.play();
    return t.r.across (kToPeak, [&] { t.r.engine.clickSolo (id, false); });
}
float tapeSoloElsewhereOn (Signal s)  { return tapeSoloEvent (s, false); }
float tapeSoloElsewhereOff (Signal s) { return tapeSoloEvent (s, true); }

// A track's L / R speakers, clicked while it plays (as the Window posts them).
float routeEvent (Signal s, bool startOn, bool left, bool right)
{
    Rig r;
    auto d = document (s, { 48000 });
    d->tracks[0].outLeft = d->tracks[0].outRight = startOn;
    const auto id = r.add (std::move (d));
    r.engine.play (id, looping());
    return r.across (kToPeak, [&]
    {
        auto* doc = r.engine.editDoc (id);
        doc->tracks[0].outLeft = left;
        doc->tracks[0].outRight = right;
        r.engine.commitMeta (id);
    });
}

float routeOff (Signal s)  { return routeEvent (s, true, false, false); }
float routeOn (Signal s)   { return routeEvent (s, false, true, true); }
float routeSide (Signal s) { return routeEvent (s, true, true, false); }

struct Scenario
{
    const char* name;
    float (*run) (Signal);
};

const Scenario kScenarios[] = {
    { "stop",                         stopIt },
    { "pause",                        pauseIt },
    { "resume",                       resumeIt },
    { "play from the caret",          playFromCaret },
    { "play again while playing",     restartWhilePlaying },
    { "play once runs out",           onceRunsOut },
    { "close a playing Sound",        closeSound },
    { "loop wrap, whole sound",       wrapWhole },
    { "loop wrap, backward",          wrapBackward },
    { "loop wrap, FF",                wrapFast },
    { "loop wrap, two own lengths",   wrapOwnLengths },
    { "loop wrap, selection",         wrapSelection },
    { "loop wrap, Set Loop region",   wrapRegion },
    { "loop wrap, short track",       wrapShortTrack },
    { "caret loop, late track",       caretLateTrack },
    { "keyboard strike",              keyStrike },
    { "keyboard release",             keyRelease },
    { "keyboard retrigger",           keyRetrigger },
    { "MIDI retrigger",               midiRetrigger },
    { "MIDI glide",                   midiGlide },
    { "MPE slide over wraps",         mpeSlide },
    { "mute on",                      muteOn },
    { "mute off",                     muteOff },
    { "solo elsewhere on",            soloOn },
    { "solo elsewhere off",           soloOff },
    { "Main Mute on",                 mainMuteOn },
    { "Main Mute off",                mainMuteOff },
    { "fader jump",                   faderJump },
    { "balance jump",                 balanceJump },
    { "Main Level jump",              mainJump },
    { "track L and R off",            routeOff },
    { "track L and R on",             routeOn },
    { "track R off",                  routeSide },
    { "tape play",                    tapeStart },
    { "tape stop",                    tapeHalt },
    { "tape jump while playing",      tapeJump },
    { "tape runs out",                tapeRunsOut },
    { "tape cleared while playing",   tapeCleared },
    { "tape record over playback",    tapePunchIn },
    { "tape grabbed while playing",   tapeGrabbed },
    { "tape solo elsewhere on",       tapeSoloElsewhereOn },
    { "tape solo elsewhere off",      tapeSoloElsewhereOff },
    { "tape take deleted under head", tapeTakeDeleted },
    { "tape earlier take deleted",    tapeEarlierDeleted },
};

// ---- what the fades do ----------------------------------------------------------

int fadeFailures = 0;

void expect (bool ok, const char* what)
{
    std::printf ("%-4s declick: %s\n", ok ? "ok" : "FAIL", what);
    if (! ok) ++fadeFailures;
}

constexpr size_t kRamp5ms = 240;     // se::kDeclickMs at 48 kHz

void fadeShapes()
{
    {
        Rig r; const auto id = r.add (document (Signal::dc, { 48000 }));
        r.engine.play (id, looping());
        r.run (1000);
        expect (r.left[0] == 0.0f && std::fabs (r.left[kRamp5ms / 2] - 0.25f) < 0.002f
                && std::fabs (r.left[kRamp5ms] - 0.5f) < 1.0e-4f, "a start fades in over 5 ms");
        r.engine.stop (id);
        r.run (kBlock);
        auto* w = r.engine.windowById (id);
        size_t p = 0;
        expect (w->transport->uiState() == se::TransportState::stopped && w->transport->uiPlayhead() == 0
                && ! w->transport->uiTrackPosition (0, p), "a stop reads as stopped at once, playheads cleared");
        expect (std::fabs (r.left[1000 + kRamp5ms / 2] - 0.25f) < 0.002f && std::fabs (r.left[1000 + kRamp5ms]) < 1.0e-4f && r.left[1000 + kRamp5ms + 8] == 0.0f
                && r.left[1000 + kRamp5ms - 1] > 0.0f, "...and fades out over 5 ms");
        r.run (kBlock);
        expect (! w->transport->audible(), "then the transport is silent");
    }
    {
        Rig r; const auto id = r.add (document (Signal::dc, { 48000 }));
        r.engine.play (id, looping());
        r.run (1000);
        r.engine.pause (id);
        r.run (1024);
        auto* w = r.engine.windowById (id);
        size_t p = 0;
        expect (w->transport->uiState() == se::TransportState::paused && w->transport->uiTrackPosition (0, p)
                && p == (1000 + kRamp5ms) * 2, "a pause: the playhead lands where the fade-out ends");
    }
    {
        se::Transport t (kRate);
        auto d = document (Signal::dc, { 48000 });
        t.play (*d, looping());
        std::vector<float> l (1000), rr (1000);
        t.render (*d, l.data(), rr.data(), 1000);
        t.pause (true);
        t.render (*d, l.data(), rr.data(), 1000);
        size_t p = 0;
        expect (t.uiTrackPosition (0, p) && p == 1000 * 2, "the Recorder's pause keeps its place, to stay in step");
    }
    {
        Rig r; const auto id = r.add (document (Signal::dc, { 48000 }));
        r.engine.play (id, looping());
        r.run (1000);
        r.engine.removeWindow (id);
        r.run (128);
        expect (r.left.back() > 0.0f && r.engine.numWindows() == 0, "a closed Sound is gone for the UI while it fades out");
        r.run (1024);
        r.engine.collectRetired();
        expect (r.left.back() == 0.0f, "...then silent, and handed back");
    }
    {
        // More closing Sounds than the live set holds: the new ones still get in.
        Rig r;
        std::vector<uint64_t> ids;
        for (size_t i = 0; i < se::Engine::kMaxWindows; ++i) ids.push_back (r.add (document (Signal::dc, { 64 })));
        r.run (kBlock);
        for (auto id : ids) r.engine.play (id, looping());
        r.run (kBlock);
        for (auto id : ids) r.engine.removeWindow (id);
        const auto fresh = r.add (document (Signal::dc, { 48000 }));
        r.engine.play (fresh, looping());
        r.run (2048);
        r.engine.collectRetired();
        expect (std::fabs (r.left.back() - 0.5f) < 1.0e-4f, "a canvas replaced while playing: the new Sound sounds");
    }
    {
        // Events faster than the tails can finish, in tiny blocks: still no click.
        Rig r; const auto id = r.add (document (Signal::sine, { 48000 }));
        r.engine.play (id, looping());
        r.run (1000);
        const size_t from = r.left.size();
        for (int i = 0; i < 400; ++i)
        {
            switch (i % 5)
            {
                case 0: r.engine.pause (id); break;
                case 1: r.engine.pause (id); break;
                case 2: r.engine.stop (id); break;
                case 3: r.engine.play (id, looping()); break;
                default: r.engine.seek (id, (size_t) (i * 37 % 4000) * 2); break;
            }
            r.run (17 + i % 91);
        }
        const float j = r.jumpFrom (from);
        std::printf ("     declick: hammered transport, worst jump %.4f\n", j);
        expect (j < 0.04f, "pause / resume / stop / play / seek hammered every few ms: no click");
    }
}

// ---- what a loop wrap changes ----------------------------------------------------

/** The left output of `frames` frames of a Transport looping `d`. */
std::vector<float> loopOutput (const se::SoundDocument& d, int frames, double declickMs, double rate = 1.0,
                               se::PlayDirection dir = se::PlayDirection::forward)
{
    se::Transport t (kRate, declickMs);
    t.setDirection (dir, d);
    se::PlayOptions o = looping();
    o.rateScale = rate;
    t.play (d, o);
    std::vector<float> l ((size_t) frames), r ((size_t) frames);
    t.render (d, l.data(), r.data(), frames);
    return l;
}

float largestDifference (const std::vector<float>& a, const std::vector<float>& b, size_t from, size_t to)
{
    float worst = 0.0f;
    for (size_t i = from; i < to; ++i) worst = std::max (worst, std::fabs (a[i] - b[i]));
    return worst;
}

float largestStep (const std::vector<float>& a, size_t from)
{
    float worst = 0.0f;
    for (size_t i = from; i < a.size(); ++i) worst = std::max (worst, std::fabs (a[i] - a[i - 1]));
    return worst;
}

void wrapShapes()
{
    const size_t lap = kOffZero;
    const auto off = document (Signal::sine, { lap });
    const auto bare = loopOutput (*off, (int) (4 * lap), 0.0);
    const auto smooth = loopOutput (*off, (int) (4 * lap), se::kDeclickMs);
    bool attack = true;
    for (size_t k = 1; k < 4; ++k)
        attack = attack && largestDifference (bare, smooth, k * lap, (k + 1) * lap - kRamp5ms - 1) == 0.0f;
    expect (attack, "a wrap changes the lap's last 5 ms only: same length, same attack");
    expect (largestDifference (bare, smooth, lap - kRamp5ms - 1, lap) > 0.5f, "...which bend to meet the next lap");

    const auto clean = document (Signal::sine, { 4 * kPeriod });
    for (auto dir : { se::PlayDirection::forward, se::PlayDirection::backward })
    {
        const float d = largestDifference (loopOutput (*clean, 8000, 0.0, 1.0, dir),
                                           loopOutput (*clean, 8000, se::kDeclickMs, 1.0, dir), kRamp5ms, 8000);
        expect (d < 0.001f, dir == se::PlayDirection::forward ? "a loop that joins cleanly is left as it is"
                                                              : "...backward too");
    }
    // A loop shorter than twice the ramp is ramped over half of itself.
    auto tiny = document (Signal::sine, { 48000 });
    tiny->loopSt = 0; tiny->loopEnd = kPeriod / 4 * 2;      // 120 frames: 0 up to the peak
    const auto t = loopOutput (*tiny, 4000, se::kDeclickMs);
    expect (largestStep (t, kRamp5ms) < 0.05f && *std::max_element (t.begin() + kRamp5ms, t.end()) > 0.3f,
            "a 2.5 ms loop: no click, and still heard");

    expect (largestDifference (loopOutput (*off, 8000, 0.0, 1.0, se::PlayDirection::pingPong),
                               loopOutput (*off, 8000, se::kDeclickMs, 1.0, se::PlayDirection::pingPong), kRamp5ms, 8000) == 0.0f,
            "ping-pong turns without a step: untouched");

    for (double rate : { 1.0 / 32.0, 0.25, 0.7071, 3.0 })
    {
        const auto out = loopOutput (*off, (int) (3.0 * (double) lap / rate), se::kDeclickMs, rate);
        // The sound's own step at that speed (held samples below 1x), plus the ramp's.
        const float most = (float) (0.0132 * std::max (1.0, rate) + 0.01);
        const float got = largestStep (out, kRamp5ms);
        std::printf ("     declick: wrap at %.4fx, worst jump %.4f (allowed %.4f)\n", rate, got, most);
        expect (got < most, "a wrap is as smooth at any pitch");
    }
}

// ---- the Keyboard, struck again at once ------------------------------------------

void retriggerShapes()
{
    for (bool midi : { false, true })
    {
        Keys k (Signal::dc);
        const auto on  = [&] { if (midi) k.voice.midiNoteOn (k.note, k.note, 0.0); else k.voice.strike (k.note); };
        const auto off = [&] { if (midi) k.voice.midiNoteOff (k.note); else k.voice.release(); };
        on();
        off();                       // neither seen by the audio thread yet
        on();
        k.r.run (1024);
        bool ok = std::fabs (k.r.left.back() - 0.5f) < 1.0e-4f;
        off(); on(); off(); on();    // again, from a rolling voice
        k.r.run (1024);
        ok = ok && std::fabs (k.r.left.back() - 0.5f) < 1.0e-4f;
        off();
        k.r.run (1024);
        ok = ok && k.r.left.back() == 0.0f && k.r.engine.windowById (k.id)->transport->uiState() == se::TransportState::stopped;
        expect (ok, midi ? "MIDI: a key struck again within a block sounds, and its release stops it"
                         : "mouse: a key struck again within a block sounds, and its release stops it");
    }
    {
        // A Window set playing is still borrowed, even before its Play has landed.
        Keys k (Signal::dc);
        k.r.engine.play (k.id, looping());
        k.voice.strike (k.note + 12);
        k.voice.release();
        k.r.run (1024);
        expect (k.r.engine.windowById (k.id)->transport->uiState() == se::TransportState::playing,
                "a Window just set playing is borrowed, not stopped by the key's release");
    }
}

// ---- gain switches -----------------------------------------------------------

void gainShapes()
{
    Rig r; const auto id = r.add (document (Signal::dc, { 48000 }));
    auto& w = *r.engine.windowById (id);
    w.muted = true;
    r.engine.play (id, looping());
    r.run (1000);
    expect (*std::max_element (r.left.begin(), r.left.end()) == 0.0f, "a Sound muted before it plays is silent from its first sample");

    w.muted = false;
    r.run (1000);
    expect (std::fabs (r.left[1000 + kRamp5ms / 2] - 0.25f) < 0.003f && std::fabs (r.left[1000 + kRamp5ms] - 0.5f) < 1.0e-4f,
            "unmute ramps over 5 ms, whatever the block size");

    // Flipped faster than the ramp, in uneven blocks, with the faders moving too.
    const size_t from = r.left.size();
    for (int i = 0; i < 600; ++i)
    {
        w.muted = (i % 3) == 0;
        w.gain = (i % 5) * 0.2f;
        w.balance = (i % 7) / 3.0f - 1.0f;
        r.engine.setMasterMuted ((i % 11) == 0);
        r.engine.setMasterGain ((i % 4) * 0.4f);
        r.run (1 + i % 97);
    }
    const float j = r.jumpFrom (from);
    expect (j < kThreshold, "mute, fader, balance and Main hammered together: no click");
}

// ---- the tape's fades -----------------------------------------------------------

void tapeShapes()
{
    {
        Reel t (Signal::dc, 48000);
        t.deck.play();
        t.r.run (1000);
        const size_t at = t.r.left.size();
        t.deck.stop();
        t.r.run (1024);
        expect (std::fabs (t.r.left[at + kRamp5ms / 2] - 0.25f) < 0.003f && t.r.left[at + kRamp5ms - 1] > 0.0f
                && t.r.left[at + kRamp5ms] == 0.0f, "a tape stop fades out over 5 ms");
    }
    {
        Reel t (Signal::dc, 48000);
        t.deck.play();
        t.r.run (1000);
        t.deck.clear();
        t.r.run (1024);
        t.deck.service();
        expect (t.r.engine.tapeStore().reservedFrames() == 0 && t.r.engine.tapeStore().length() == 0,
                "a cleared tape is freed once its fade has been copied aside");
    }
    {
        // Jumps and stops faster than the fades, in uneven blocks.
        Reel t (Signal::sine, 48000);
        t.deck.play();
        t.r.run (1000);
        const size_t from = t.r.left.size();
        for (int i = 0; i < 400; ++i)
        {
            if (i % 4 == 2)      t.deck.stop();
            else if (i % 4 == 3) t.deck.play();
            else                 t.deck.seek ((size_t) (i * 331 % 40000));
            t.r.run (11 + i % 83);
        }
        expect (t.r.jumpFrom (from) < kThreshold, "the tape hammered with jumps and stops: no click");
    }
}

// ---- the track toggles ----------------------------------------------------------

void routeShapes()
{
    Rig r;
    auto d = document (Signal::dc, { 48000, 48000 });
    d->tracks[1].outLeft = d->tracks[1].outRight = false;
    const auto id = r.add (std::move (d));
    const auto set = [&] (int t, bool l, bool rr)
    {
        auto* doc = r.engine.editDoc (id);
        doc->tracks[(size_t) t].outLeft = l;
        doc->tracks[(size_t) t].outRight = rr;
        r.engine.commitMeta (id);
    };
    r.engine.play (id, looping());
    r.run (1000);
    set (0, false, true);
    r.run (1000);
    expect (std::fabs (r.left[1000 + kRamp5ms / 2] - 0.25f) < 0.003f && std::fabs (r.left[1000 + kRamp5ms]) < 1.0e-4f && r.left[1000 + kRamp5ms + 8] == 0.0f
            && std::fabs (r.right[1000 + kRamp5ms / 2] - 0.5f) < 1.0e-4f,
            "L off ramps the left over 5 ms and leaves the right alone");

    // Set while stopped: the next play has it from its first sample.
    r.engine.stop (id);
    r.run (1000);
    set (0, true, false);
    const size_t at = r.left.size();
    r.engine.play (id, looping());
    r.run (1000);
    expect (*std::max_element (r.right.begin() + (std::ptrdiff_t) at, r.right.end()) == 0.0f
            && std::fabs (r.left.back() - 0.5f) < 1.0e-4f, "a toggle made while stopped is in force from the start");

    const size_t from = r.left.size();
    for (int i = 0; i < 400; ++i)
    {
        set (i % 2, (i % 3) == 0, (i % 5) < 2);
        if (i % 17 == 0) r.engine.pause (id);
        if (i % 17 == 3) r.engine.pause (id);
        if (i % 29 == 0) r.engine.setDirection (id, (se::PlayDirection) (i % 3));
        r.run (1 + i % 71);
    }
    expect (r.jumpFrom (from) < kThreshold, "toggles hammered through pauses and turns: no click");
}

} // namespace

int declickTests()
{
    std::printf ("declick: largest sample-to-sample jump (full scale = 1)\n");
    fadeShapes();
    wrapShapes();
    retriggerShapes();
    gainShapes();
    tapeShapes();
    routeShapes();
    int failures = 0;
    for (const auto& sc : kScenarios)
        for (Signal s : { Signal::dc, Signal::sine })
        {
            const float got = sc.run (s);
            const bool ok = got < kThreshold;
            std::printf ("%-4s declick: %-28s %s jump %.4f (under %.2f)\n",
                         ok ? "ok" : "FAIL", sc.name, nameOf (s), got, kThreshold);
            if (! ok) ++failures;
        }
    return failures + fadeFailures;
}
