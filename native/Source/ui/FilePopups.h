// FilePopups — the File menu's Export Sound... and Import Sound... dialogs, on the
// Os8Popup frame.
//
// NOT in the design system (spec: "improvise and we'll iterate"), so these are a
// first draft built from the vocabulary of the popups around them: Small Label rows,
// the checkbox of the Select Audio Source popup, Cancel / primary at the foot.

#pragma once

#include "Os8Popup.h"
#include "engine/EngineState.h"

#include <functional>
#include <memory>
#include <vector>

namespace filepopups {

/** "2 tracks · 44.1 kHz · 16 bit · 3.20 s" */
juce::String summaryOf (const se::WindowState&);

/** Export Sound: names the Sound and what will be written. The primary button
    ("Export…") answers 1; the caller then asks for the file. */
std::unique_ptr<Os8Popup> exportSound (const se::WindowState& sound);

/** Import Sound: the Sounds found in the file, each with a checkbox. `import` gets the
    indices ticked (in file order). A file holding a single Sound opens with it ticked;
    Import stays greyed while nothing is. */
std::unique_ptr<Os8Popup> importSound (const juce::String& fileName,
                                       const std::vector<se::WindowState>& sounds,
                                       std::function<void (std::vector<size_t>)> doImport);

} // namespace filepopups
