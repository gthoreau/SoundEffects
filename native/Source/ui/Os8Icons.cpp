#include "Os8Icons.h"

#include <array>
#include <memory>

namespace os8::icon {

namespace {

struct Source { int width, height; const char* body; };

// The Figma SVG export of each icon, minus its outer <svg> element.
const Source kSources[] = {
    /* record */ { 10, 10, R"svg(<path d="M7 1H9V3H10V7H9V9H7V10H3V9H1V7H0V3H1V1H3V0H7V1Z" fill="#FD0000"/>)svg" },
    /* stop */ { 9, 9, R"svg(<rect width="9" height="9" fill="#FD0000"/>)svg" },
    /* pause */ { 11, 10, R"svg(<path d="M4 10H0V0H4V10Z" fill="#FD0000"/><path d="M11 10H7V0H11V10Z" fill="#FD0000"/>)svg" },
    /* playForward */ { 7, 12, R"svg(<path d="M6 8H5V9H4V10H3V11H2V12H0V0H2V1H3V2H4V3H5V4H6V5H7V7H6V8Z" fill="#FD0000"/>)svg" },
    /* playBackward */ { 7, 12, R"svg(<path d="M1 8H2V9H3V10H4V11H5V12H7V0H5V1H4V2H3V3H2V4H1V5H0V7H1V8Z" fill="#FD0000"/>)svg" },
    /* playPingPong */ { 15, 12, R"svg(<path d="M6 8H5V9H4V10H3V11H2V12H0V0H2V1H3V2H4V3H5V4H6V5H7V7H6V8Z" fill="#FD0000"/><path d="M9 8H10V9H11V10H12V11H13V12H15V0H13V1H12V2H11V3H10V4H9V5H8V7H9V8Z" fill="#FD0000"/>)svg" },
    /* fastForward */ { 13, 11, R"svg(<path d="M12 7H11V8H10V9H9V10H8V11H6V7H7V6H8V5H7V6H6V7H5V8H4V9H3V10H2V11H0V0H2V1H3V2H4V3H5V4H6V0H8V1H9V2H10V3H11V4H12V5H13V6H12V7Z" fill="#FD0000"/>)svg" },
    /* loop */ { 12, 10, R"svg(<path d="M11 2H12V8H11V9H10V10H4V9H3V8H9V7H10V3H9V2H5V3H4V4H6V5H5V6H4V7H2V6H1V5H0V4H2V2H3V1H4V0H10V1H11V2Z" fill="#FD0000"/>)svg" },
    /* next */ { 9, 12, R"svg(<path d="M6 4H5V3H4V2H3V1H2V0H0V12H2V11H3V10H4V9H5V8H6V7H7V5H6V4Z" fill="#FD0000"/><path d="M9 12H8V0H9V12Z" fill="#FD0000"/>)svg" },
    /* previous */ { 9, 12, R"svg(<path d="M3 8H4V9H5V10H6V11H7V12H9V0H7V1H6V2H5V3H4V4H3V5H2V7H3V8Z" fill="#FD0000"/><path d="M0 0H1V12H0V0Z" fill="#FD0000"/>)svg" },
    /* sendToCanvas */ { 11, 11, R"svg(<path d="M8 3V7H7V5H6V6H5V7H4V8H3V9H2V10H1V11H0V9H1V8H2V7H3V6H4V5H5V4H3V3H8Z" fill="#FD0000"/><path d="M11 10H5V8H9V2H2V6H0V0H11V10Z" fill="#FD0000"/><path d="M9 8H5V9H3V8H4V7H5V6H6V5H7V7H8V3H3V4H5V5H4V6H3V7H2V8H1V6H2V2H9V8Z" fill="white"/>)svg" },
    /* speakerLeftOn */ { 11, 11, R"svg(<path d="M6 10H5V9H6V10Z" fill="black"/><path d="M7 9H6V8H7V9Z" fill="black"/><path d="M8 8H7V7H8V8Z" fill="black"/><path d="M10 7H8V6H9V5H8V4H10V7Z" fill="black"/><path d="M8 4H7V3H8V4Z" fill="black"/><path d="M7 3H6V2H7V3Z" fill="black"/><path d="M6 2H5V1H6V2Z" fill="black"/><path d="M7 8H6V9H5V7H7V8Z" fill="#545487"/><path d="M8 7H7V6H8V7Z" fill="#545487"/><path d="M7 7H5V6H7V7Z" fill="#8787DA"/><path d="M9 6H7V5H9V6Z" fill="#8787DA"/><path d="M7 6H5V4H6V5H7V6Z" fill="#B3B3FF"/><path d="M8 5H6V4H5V3H7V4H8V5Z" fill="#DADAFF"/><rect x="5" y="2" width="1" height="1" fill="#B3B3FF"/><path d="M4 10H3V9H4V10Z" fill="#3F3F3F"/><path d="M3 9H2V7H3V9Z" fill="#3F3F3F"/><path d="M2 7H1V4H2V7Z" fill="#3F3F3F"/><path d="M4 7H3V4H4V7Z" fill="#3F3F3F"/><path d="M3 4H2V2H3V4Z" fill="#3F3F3F"/><path d="M4 2H3V1H4V2Z" fill="#3F3F3F"/>)svg" },
    /* speakerLeftOff */ { 11, 11, R"svg(<path d="M6 10H5V9H6V10Z" fill="black"/><path d="M7 9H6V8H7V9Z" fill="black"/><path d="M8 8H7V7H8V8Z" fill="black"/><path d="M10 7H8V6H9V5H8V4H10V7Z" fill="black"/><path d="M8 4H7V3H8V4Z" fill="black"/><path d="M7 3H6V2H7V3Z" fill="black"/><path d="M6 2H5V1H6V2Z" fill="black"/><path d="M7 8H6V9H5V7H7V8Z" fill="#545487"/><path d="M8 7H7V6H8V7Z" fill="#545487"/><path d="M7 7H5V6H7V7Z" fill="#8787DA"/><path d="M9 6H7V5H9V6Z" fill="#8787DA"/><path d="M7 6H5V4H6V5H7V6Z" fill="#B3B3FF"/><path d="M8 5H6V4H5V3H7V4H8V5Z" fill="#DADAFF"/><rect x="5" y="2" width="1" height="1" fill="#B3B3FF"/>)svg" },
    /* speakerRightOn */ { 11, 11, R"svg(<path d="M5 10H6V9H5V10Z" fill="black"/><path d="M4 9H5V8H4V9Z" fill="black"/><path d="M3 8H4V7H3V8Z" fill="black"/><path d="M1 7H3V6H2V5H3V4H1V7Z" fill="black"/><path d="M3 4H4V3H3V4Z" fill="black"/><path d="M4 3H5V2H4V3Z" fill="black"/><path d="M5 2H6V1H5V2Z" fill="black"/><path d="M4 8H5V9H6V7H4V8Z" fill="#545487"/><path d="M3 7H4V6H3V7Z" fill="#545487"/><path d="M4 7H6V6H4V7Z" fill="#8787DA"/><path d="M2 6H4V5H2V6Z" fill="#8787DA"/><path d="M4 6H6V4H5V5H4V6Z" fill="#B3B3FF"/><path d="M3 5H5V4H6V3H4V4H3V5Z" fill="#DADAFF"/><rect x="5" y="2" width="1" height="1" fill="#B3B3FF"/><path d="M7 10H8V9H7V10Z" fill="#3F3F3F"/><path d="M8 9H9V7H8V9Z" fill="#3F3F3F"/><path d="M9 7H10V4H9V7Z" fill="#3F3F3F"/><path d="M7 7H8V4H7V7Z" fill="#3F3F3F"/><path d="M8 4H9V2H8V4Z" fill="#3F3F3F"/><path d="M7 2H8V1H7V2Z" fill="#3F3F3F"/>)svg" },
    /* speakerRightOff */ { 11, 11, R"svg(<path d="M5 10H6V9H5V10Z" fill="black"/><path d="M4 9H5V8H4V9Z" fill="black"/><path d="M3 8H4V7H3V8Z" fill="black"/><path d="M1 7H3V6H2V5H3V4H1V7Z" fill="black"/><path d="M3 4H4V3H3V4Z" fill="black"/><path d="M4 3H5V2H4V3Z" fill="black"/><path d="M5 2H6V1H5V2Z" fill="black"/><path d="M4 8H5V9H6V7H4V8Z" fill="#545487"/><path d="M3 7H4V6H3V7Z" fill="#545487"/><path d="M4 7H6V6H4V7Z" fill="#8787DA"/><path d="M2 6H4V5H2V6Z" fill="#8787DA"/><path d="M4 6H6V4H5V5H4V6Z" fill="#B3B3FF"/><path d="M3 5H5V4H6V3H4V4H3V5Z" fill="#DADAFF"/><rect x="5" y="2" width="1" height="1" fill="#B3B3FF"/>)svg" },
    /* zoomOut */ { 12, 12, R"svg(<path d="M5 10H4V9H3V10H1V9H2V8H3V7H4V8H5V7H6V6H7V7H8V8H9V9H10V10H8V9H7V8H6V9H5V10Z" fill="black"/><rect x="3" y="9" width="1" height="1" fill="#DAB487"/><rect x="5" y="9" width="1" height="1" fill="#DAB487"/><rect x="6" y="9" width="1" height="1" fill="#04CD00"/><rect x="6" y="8" width="1" height="1" fill="white"/><rect x="7" y="9" width="1" height="1" fill="#DADADA"/>)svg" },
    /* zoomIn */ { 12, 12, R"svg(<path d="M2 11H1V10H2V11Z" fill="#DAB487"/><path d="M4 11H3V10H4V11Z" fill="#DAB487"/><path d="M6 11H5V10H6V11Z" fill="#DAB487"/><path d="M10 11H9V10H10V11Z" fill="#DAB487"/><path d="M3 10H2V9H3V10Z" fill="#DAB487"/><path d="M11 10H10V9H11V10Z" fill="#DAB487"/><path d="M4 9H3V8H4V9Z" fill="#DAB487"/><path d="M8 9H7V8H8V9Z" fill="#DAB487"/><path d="M10 9H9V8H10V9Z" fill="#DAB487"/><path d="M12 9H11V8H12V9Z" fill="#DAB487"/><path d="M9 8H8V7H9V8Z" fill="#DAB487"/><path d="M11 8H10V7H11V8Z" fill="#DAB487"/><path d="M2 10H1V9H2V10Z" fill="#54B387"/><path d="M5 10H3V9H4V8H5V10Z" fill="#54B387"/><path d="M9 10H8V8H9V10Z" fill="#54B387"/><path d="M3 9H2V8H3V9Z" fill="#54B387"/><path d="M8 8H7V9H6V7H8V8Z" fill="#54B387"/><path d="M11 9H10V8H11V9Z" fill="#54B387"/><path d="M12 8H11V7H10V8H9V6H12V8Z" fill="#54B387"/><path d="M2 9H1V8H2V9Z" fill="black"/><path d="M6 7V9H5V8H4V7H6Z" fill="black"/><path d="M3 8H2V7H3V8Z" fill="black"/><path d="M4 7H3V6H4V7Z" fill="black"/><path d="M7 7H6V6H7V7Z" fill="black"/><path d="M8 6H7V5H8V6Z" fill="black"/><path d="M12 6H11V5H12V6Z" fill="black"/><path d="M9 5H8V4H9V5Z" fill="black"/><path d="M11 5H10V4H11V5Z" fill="black"/><path d="M10 4H9V3H10V4Z" fill="black"/><path d="M2 8H1V7H2V8Z" fill="#3F3F3F"/><path d="M3 7H2V6H3V7Z" fill="#3F3F3F"/><path d="M6 7H4V6H6V7Z" fill="#3F3F3F"/><path d="M4 6H3V5H4V6Z" fill="#3F3F3F"/><path d="M7 6H6V5H7V6Z" fill="#3F3F3F"/><path d="M8 5H7V4H8V5Z" fill="#3F3F3F"/><path d="M12 5H11V4H12V5Z" fill="#3F3F3F"/><path d="M9 4H8V3H9V4Z" fill="#3F3F3F"/><path d="M11 4H10V3H11V4Z" fill="#3F3F3F"/><path d="M10 3H9V2H10V3Z" fill="#3F3F3F"/><path d="M4 8H3V7H4V8Z" fill="white"/><path d="M9 7H7V6H8V5H9V4H10V5H11V6H9V7Z" fill="white"/><rect x="7" y="9" width="1" height="1" fill="#C0C0C0"/>)svg" },
    /* zoomReset */ { 12, 12, R"svg(<path d="M9 9H4V8H8V7H9V9Z" fill="black"/><path d="M3 4L8 4L8 5L4 5L4 6L3 6L3 4Z" fill="#3F3F3F"/><path d="M9 6H4V5H8V4H9V6Z" fill="black"/><path d="M3 7L8 7L8 8L4 8L4 9L3 9L3 7Z" fill="#3F3F3F"/>)svg" },
    /* zoomArrowLeft */ { 12, 12, R"svg(<path d="M10 9H6V11H5V10H4V9H5V8H9V4H5V3H4V2H5V1H6V3H10V9Z" fill="#888888"/><path d="M4 9H3V8H4V9Z" fill="#888888"/><path d="M3 8H2V7H3V8Z" fill="#888888"/><path d="M2 7H1V5H2V7Z" fill="#888888"/><path d="M3 5H2V4H3V5Z" fill="#888888"/><path d="M4 4H3V3H4V4Z" fill="#888888"/>)svg" },
    /* zoomArrowRight */ { 12, 12, R"svg(<path d="M2 9H6V11H7V10H8V9H7V8H3V4H7V3H8V2H7V1H6V3H2V9Z" fill="#888888"/><path d="M8 9H9V8H8V9Z" fill="#888888"/><path d="M9 8H10V7H9V8Z" fill="#888888"/><path d="M10 7H11V5H10V7Z" fill="#888888"/><path d="M9 5H10V4H9V5Z" fill="#888888"/><path d="M8 4H9V3H8V4Z" fill="#888888"/>)svg" },
    /* trashcan */ { 14, 16, R"svg(<path d="M12 16H2V15H12V16Z" fill="black"/><path d="M1 3H13V2H14V4H13V15H12V4H2V15H1V4H0V2H1V3Z" fill="black"/><path d="M13 2H1V1H5V0H9V1H13V2Z" fill="black"/><path d="M12 4V15H11V5H10V4H12Z" fill="#777777"/><rect x="10" y="5" width="1" height="9" fill="#666666"/><rect x="2" y="4" width="1" height="11" fill="#DADADA"/><rect x="9" y="6" width="1" height="7" fill="#777777"/><rect x="9" y="13" width="1" height="2" fill="#A5A5A5"/><rect x="5" y="5" width="1" height="10" fill="#C0C0C0"/><rect x="7" y="6" width="1" height="7" fill="#A5A5A5"/><rect x="4" y="6" width="1" height="7" fill="#C0C0C0"/><rect x="6" y="6" width="1" height="7" fill="#777777"/><rect x="3" y="6" width="1" height="7" fill="#777777"/><rect x="10" y="14" width="1" height="1" fill="#969696"/><rect x="9" y="5" width="1" height="1" fill="#A5A5A5"/><rect x="9" y="4" width="1" height="1" fill="#969696"/><rect x="8" y="4" width="1" height="1" fill="#A5A5A5"/><rect x="6" y="4" width="2" height="1" fill="#C0C0C0"/><rect x="5" y="4" width="1" height="1" fill="#CDCDCD"/><rect x="7" y="14" width="1" height="1" fill="#CDCDCD"/><rect x="4" y="4" width="1" height="1" fill="#DADADA"/><rect x="6" y="5" width="1" height="1" fill="#DADADA"/><rect x="6" y="13" width="1" height="2" fill="#DADADA"/><rect x="3" y="13" width="1" height="2" fill="#E7E7E7"/><rect x="11" y="2" width="2" height="1" fill="#666666"/><rect x="10" y="2" width="1" height="1" fill="#777777"/><rect x="9" y="2" width="1" height="1" fill="#969696"/><rect x="8" y="2" width="1" height="1" fill="#A5A5A5"/><rect x="1" y="2" width="1" height="1" fill="#C0C0C0"/><rect x="2" y="2" width="1" height="1" fill="#CDCDCD"/><rect x="3" y="2" width="1" height="1" fill="#DADADA"/><rect x="4" y="2" width="1" height="1" fill="#E7E7E7"/><rect x="5" y="2" width="1" height="1" fill="#DADADA"/><rect x="6" y="2" width="1" height="1" fill="#CDCDCD"/><rect x="7" y="2" width="1" height="1" fill="#C0C0C0"/><rect x="4" y="5" width="1" height="1" fill="#969696"/><rect x="4" y="13" width="1" height="1" fill="#969696"/><rect x="4" y="14" width="1" height="1" fill="#F3F3F3"/><rect x="7" y="13" width="1" height="1" fill="#F3F3F3"/><rect x="3" y="4" width="1" height="2" fill="#E7E7E7"/><rect x="5" y="5" width="1" height="10" fill="#E7E7E7"/><rect x="8" y="5" width="1" height="10" fill="#C0C0C0"/><rect x="7" y="13" width="1" height="1" fill="#969696"/><rect x="7" y="5" width="1" height="1" fill="#969696"/>)svg" },
    /* activeTrack */ { 16, 11, R"svg(<path fill-rule="evenodd" d="M16 11H0V0H16V11ZM1 10H15V6H13V8H12V7H10V6H9V8H8V6H7V7H6V9H5V6H4V7H3V6H1V10ZM1 5H3V4H4V5H5V2H6V4H7V5H8V3H9V5H10V3H11V4H12V5H13V4H14V5H15V1H1V5Z" fill="black"/>)svg" },
    /* newTrack */ { 16, 11, R"svg(<path fill-rule="evenodd" d="M16 11H0V0H16V11ZM1 6V10H15V6H1ZM1 5H15V1H1V5Z" fill="black"/>)svg" },
    /* dropdownDoubleArrow */ { 7, 10, R"svg(<path d="M5 2H6V3H7V4H0V3H1V2H2V1H3V0H4V1H5V2Z" fill="black"/><path d="M2 8L1 8L1 7L0 7L0 6L7 6L7 7L6 7L6 8L5 8L5 9L4 9L4 10L3 10L3 9L2 9L2 8Z" fill="black"/>)svg" },
    /* dropdownSingleArrow */ { 7, 4, R"svg(<path d="M2 2L1 2L1 1L0 1L0 0L7 0L7 1L6 1L6 2L5 2L5 3L4 3L4 4L3 4L3 3L2 3L2 2Z" fill="black"/>)svg" },
    /* selectionStart */ { 10, 11, R"svg(<path d="M2 5H10V6H2Z M0 0H1V11H0Z M3 4H4V7H3Z M4 3H5V8H4Z M5 2H6V9H5Z" fill="black"/>)svg" },
    /* selectionEnd */ { 10, 11, R"svg(<path d="M0 5H8V6H0Z M9 0H10V11H9Z M6 4H7V7H6Z M5 3H6V8H5Z M4 2H5V9H4Z" fill="black"/>)svg" },
    /* selectionLength */ { 15, 11, R"svg(<path d="M2 5H13V6H2Z M14 0H15V11H14Z M11 4H12V7H11Z M3 4H4V7H3Z M10 3H11V8H10Z M4 3H5V8H4Z M9 2H10V9H9Z M5 2H6V9H5Z M0 0H1V11H0Z" fill="black"/>)svg" },
    /* canvasGrid */ { 13, 13, R"svg(<path fill-rule="evenodd" d="M0 0H6V6H0Z M1 1V5H5V1Z M7 0H13V6H7Z M8 1V5H12V1Z M7 7H13V13H7Z M8 8V12H12V8Z M0 7H6V13H0Z M1 8V12H5V8Z" fill="black"/>)svg" },
    /* canvasFree */ { 13, 14, R"svg(<path fill-rule="evenodd" d="M0 0H6V6H0Z M1 1V5H5V1Z M7 3H13V9H7Z M8 4V8H12V4Z M0 9H7V14H0Z M1 10V13H6V10Z" fill="black"/>)svg" },
    /* keyboard */ { 20, 12, R"svg(<path fill-rule="evenodd" d="M20 12H1V11H0V0H19V1H20V12ZM2 6H1V10H3V1H2V6ZM5 6H4V10H6V1H5V6ZM7 10H9V1H7V10ZM11 6H10V10H12V1H11V6ZM14 6H13V10H15V1H14V6ZM16 10H18V1H16V10Z" fill="black"/>)svg" },
    /* menuArrow */ { 6, 11, R"svg(<path d="M1 1H2V2H3V3H4V4H5V5H6V6H5V7H4V8H3V9H2V10H1V11H0V0H1V1Z" fill="black"/>)svg" },
    /* menuCheckmark */ { 9, 8, R"svg(<path d="M9 2H8V3H7V4H6V5H5V6H4V7H3V8H2V7H1V6H0V4H2V5H4V4H5V3H6V2H7V1H8V0H9V2Z" fill="black"/>)svg" },
};

constexpr size_t kCount = sizeof (kSources) / sizeof (kSources[0]);

const Source& source (Id id) { return kSources[(size_t) id]; }

// Parsed icons, freed at shutdown by JUCE (a function static would outlive the leak
// detector and trip it).
struct Cache : juce::DeletedAtShutdown
{
    std::array<std::unique_ptr<juce::Drawable>, kCount> drawables;
    std::array<std::unique_ptr<juce::Path>, kCount> outlines;
    ~Cache() override { clearSingletonInstance(); }
    JUCE_DECLARE_SINGLETON_INLINE (Cache, true)
};

const juce::Drawable& drawable (Id id)
{
    auto& slot = Cache::getInstance()->drawables[(size_t) id];
    if (slot == nullptr)
    {
        const auto& s = source (id);
        const auto svg = juce::String ("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"") + juce::String (s.width)
                       + "\" height=\"" + juce::String (s.height) + "\" viewBox=\"0 0 " + juce::String (s.width) + " "
                       + juce::String (s.height) + "\">" + s.body + "</svg>";
        if (auto xml = juce::parseXML (svg))
            slot = juce::Drawable::createFromSVG (*xml);
        jassert (slot != nullptr);
    }
    return *slot;
}

} // namespace

juce::Point<int> sizeOf (Id id)
{
    return { source (id).width, source (id).height };
}

void draw (juce::Graphics& g, Id id, juce::Point<float> origin, float opacity)
{
    drawable (id).draw (g, opacity, juce::AffineTransform::translation (origin));
}

const juce::Path& outline (Id id)
{
    auto& slot = Cache::getInstance()->outlines[(size_t) id];
    if (slot == nullptr)
        slot = std::make_unique<juce::Path> (drawable (id).getOutlineAsPath());
    return *slot;
}

} // namespace os8::icon
