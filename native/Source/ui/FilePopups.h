// FilePopups — the File menu's Export Sound... (1447:9154) and Import Sound...
// (1447:9162) popups, on the Os8Popup frame.

#pragma once

#include "Os8Popup.h"
#include "engine/EngineState.h"

#include <functional>
#include <memory>
#include <vector>

namespace filepopups {

/** What Export Sound writes, in the format menu's order (1447:9002). */
enum class ExportFormat { sefx, mixdown, stems };

/** Export Sound: the format menu, opened on the stereo mixdown every time. When the
    primary button ("Export...") is pressed `doExport` gets the format picked; the
    caller then asks where to write. */
std::unique_ptr<Os8Popup> exportSound (std::function<void (ExportFormat)> doExport);

/** Import Sound (1447:9162): the Sounds found in the file, ticked in a list. `import`
    gets the indices ticked (in file order). A file holding a single Sound opens with
    it ticked and no "Select all"; Import stays greyed while nothing is ticked. */
std::unique_ptr<Os8Popup> importSound (const juce::String& fileName,
                                       const std::vector<se::WindowState>& sounds,
                                       std::function<void (std::vector<size_t>)> doImport);

} // namespace filepopups
