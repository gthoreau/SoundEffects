// Os8Alert — "alerts" (1340:8550): the question asked before something is thrown
// away, and the word that something failed.
//
// The popups' frame in the dark grey of an alert, "Alert" in white on its title bar,
// 350 px wide: a heading, the message under it, then the buttons row with "Don't show
// again" at its left when the alert may be hidden. Cancel is the primary button, so
// Return and Escape both leave everything as it is.

#pragma once

#include "Os8Popup.h"

#include <functional>
#include <memory>

namespace os8alert {

struct Question
{
    juce::String heading, message;
    juce::String action;            // the button that goes ahead
    juce::String hiddenKey {};      // AppPrefs::alertHidden; empty: it cannot be hidden
};

/** Ask `q` over `over`'s window and run `go` if the action button is pressed. While
    the question is hidden, `go` runs at once and nothing is shown (nullptr). "Don't
    show again" is only kept when the action is taken: ticking it and then cancelling
    changes nothing. */
Os8Popup* ask (juce::Component& over, const Question& q, std::function<void()> go);

/** Say that something failed: the heading, the reason and one OK button. */
Os8Popup* tell (juce::Component& over, const juce::String& heading, const juce::String& message);

/** The alert, built and not shown (ask, tell and the snapshots). An empty action
    gives the one-button form. */
std::unique_ptr<Os8Popup> make (const Question&, std::function<void()> go);

} // namespace os8alert
