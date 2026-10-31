// Reorder — the shared machinery behind dragging things into a new order: the mixer's
// channel strips and the canvas's Windows in grid mode.
//
// The pattern is the usual one (Finder, the Dock, iOS lists, Trello):
//   - a press only LIFTS the item once the pointer has really travelled, so a click
//     never moves anything;
//   - the lifted item floats under the pointer, a little see-through and on a soft
//     shadow, and its old place opens as a gap;
//   - the gap moves to wherever the item would land; the neighbours slide out of its
//     way, and a slide already under way is re-aimed from where it has got to, never
//     snapped;
//   - letting go glides the item into the gap and only then commits the new order;
//     Escape, or a release outside, glides everything back with no change.
//
// Slides move components and nothing else: no size changes, no re-layout of their
// children. The timer runs only while something is moving (or a drag is held, for
// the auto-scroll), at 60 Hz.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

namespace reorder
{
/** How far the pointer travels before a press becomes a drag. */
constexpr int kLiftThreshold = 4;
/** A neighbour sliding out of the way, and the dropped item settling into its gap. */
constexpr double kSlideMs = 150.0, kDropMs = 150.0;
/** How much nearer a new gap has to be than the current one before the gap moves:
    without it the order flickers while the pointer hovers on a boundary. */
constexpr int kHysteresis = 8;
/** Auto-scroll: the band along an edge that scrolls, and the fastest it goes (px a
    frame, reached at the very edge). */
constexpr int kEdgeBand = 32;
constexpr float kMaxScrollPerFrame = 18.0f;
/** The lifted item's opacity. */
constexpr float kLiftedAlpha = 0.85f;

/** How fast to scroll for a pointer at `pos` along an axis that is visible from
    `lo` to `hi`: negative near `lo`, positive near `hi`, deeper = faster. */
inline float edgeSpeed (float pos, float lo, float hi) noexcept
{
    const float band = (float) kEdgeBand;
    if (pos < lo + band) return -kMaxScrollPerFrame * juce::jmin (1.0f, (lo + band - pos) / band);
    if (pos > hi - band) return  kMaxScrollPerFrame * juce::jmin (1.0f, (pos - (hi - band)) / band);
    return 0.0f;
}

/** The closed hand while something is carried. AppKit has one; JUCE only names the
    open hand, so on the Mac it is borrowed from AppKit (MacWindowChrome.mm). */
juce::MouseCursor grabbingCursor();

/** Slides components to where they belong, easing out, and re-aims a slide that is
    under way from wherever it has got to. */
class Slider : private juce::Timer
{
public:
    ~Slider() override { stopTimer(); }

    /** Slide `c` to `target` (its parent's coordinates). Already going there: left
        alone, so asking again every frame does not restart the slide. */
    void moveTo (juce::Component& c, juce::Point<int> target, double ms = kSlideMs)
    {
        if (auto* m = find (c))
        {
            if (m->to == target) return;
            m->from = c.getPosition().toFloat();
            m->to = target;
            m->start = now();
            m->ms = ms;
            return;
        }
        if (c.getPosition() == target) return;
        moves.push_back ({ &c, c.getPosition().toFloat(), target, now(), ms });
        run();
    }

    /** Put `c` there at once, dropping any slide it was on. */
    void jumpTo (juce::Component& c, juce::Point<int> target)
    {
        forget (c);
        c.setTopLeftPosition (target);
    }

    void forget (juce::Component& c)
    {
        moves.erase (std::remove_if (moves.begin(), moves.end(),
                                     [&c] (const Move& m) { return m.comp.getComponent() == &c; }),
                     moves.end());
    }

    void clear() { moves.clear(); run(); }

    bool isMoving (const juce::Component& c) const
    {
        for (auto& m : moves) if (m.comp.getComponent() == &c) return true;
        return false;
    }
    bool anyMoving() const noexcept { return ! moves.empty(); }

    /** Keep the clock running with nothing to slide: a held drag auto-scrolls and
        follows the pointer every frame, moved or not. */
    void setTicking (bool shouldTick) { ticking = shouldTick; run(); }

    /** Called once a frame, after the slides have stepped. */
    std::function<void()> onFrame;
    /** A slide reached its end. May start new slides, or clear(). */
    std::function<void (juce::Component&)> onArrived;

private:
    struct Move
    {
        juce::Component::SafePointer<juce::Component> comp;
        juce::Point<float> from;
        juce::Point<int> to;
        double start = 0.0, ms = kSlideMs;
    };

    static double now() { return juce::Time::getMillisecondCounterHiRes(); }

    Move* find (const juce::Component& c)
    {
        for (auto& m : moves) if (m.comp.getComponent() == &c) return &m;
        return nullptr;
    }

    void run()
    {
        if (moves.empty() && ! ticking) stopTimer();
        else if (! isTimerRunning()) startTimerHz (60);
    }

    void timerCallback() override
    {
        const double t = now();
        std::vector<juce::Component*> arrived;
        for (auto it = moves.begin(); it != moves.end();)
        {
            auto* c = it->comp.getComponent();
            if (c == nullptr) { it = moves.erase (it); continue; }   // gone mid-slide

            const double k = juce::jlimit (0.0, 1.0, (t - it->start) / juce::jmax (1.0, it->ms));
            const float e = (float) (1.0 - std::pow (1.0 - k, 3.0));   // ease-out cubic
            const auto p = it->from + (it->to.toFloat() - it->from) * e;
            c->setTopLeftPosition (p.roundToInt());

            if (k >= 1.0) { arrived.push_back (c); it = moves.erase (it); }
            else ++it;
        }

        if (onFrame) onFrame();
        for (auto* c : arrived)
            if (onArrived) onArrived (*c);
        run();
    }

    std::vector<Move> moves;
    bool ticking = false;
};

/** The lifted look: see-through, cached to an image while it floats (so moving it
    repaints nothing inside it), on a soft shadow, above its siblings. */
class Lift
{
public:
    void apply (juce::Component& c)
    {
        restore();
        target = &c;
        c.setBufferedToImage (true);
        c.setAlpha (kLiftedAlpha);
        c.toFront (false);
        shadow = std::make_unique<juce::DropShadower> (
            juce::DropShadow (juce::Colours::black.withAlpha (0.3f), 10, { 2, 4 }));
        shadow->setOwner (&c);
    }

    void restore()
    {
        shadow.reset();
        if (auto* c = target.getComponent())
        {
            c->setAlpha (1.0f);
            c->setBufferedToImage (false);
        }
        target = nullptr;
    }

    ~Lift() { restore(); }

private:
    juce::Component::SafePointer<juce::Component> target;
    std::unique_ptr<juce::DropShadower> shadow;
};
} // namespace reorder

#if ! JUCE_MAC
inline juce::MouseCursor reorder::grabbingCursor() { return juce::MouseCursor::DraggingHandCursor; }
#endif
