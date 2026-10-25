#include "Os8Icons.h"

namespace os8::icon {

namespace {
juce::Path fromSvg (std::initializer_list<const char*> pathData)
{
    juce::Path p;
    for (auto* d : pathData)
        p.addPath (juce::Drawable::parseSVGPath (d));
    return p;
}
} // namespace

const juce::Path& dropdownDoubleArrow()
{
    static const auto p = fromSvg ({ "M5 2H6V3H7V4H0V3H1V2H2V1H3V0H4V1H5V2Z",
                                     "M2 8L1 8L1 7L0 7L0 6L7 6L7 7L6 7L6 8L5 8L5 9L4 9L4 10L3 10L3 9L2 9L2 8Z" });
    return p;
}

const juce::Path& dropdownSingleArrow()
{
    static const auto p = fromSvg ({ "M2 2L1 2L1 1L0 1L0 0L7 0L7 1L6 1L6 2L5 2L5 3L4 3L4 4L3 4L3 3L2 3L2 2Z" });
    return p;
}

const juce::Path& menuArrow()
{
    static const auto p = fromSvg ({ "M1 1H2V2H3V3H4V4H5V5H6V6H5V7H4V8H3V9H2V10H1V11H0V0H1V1Z" });
    return p;
}

const juce::Path& menuCheckmark()
{
    static const auto p = fromSvg ({ "M9 2H8V3H7V4H6V5H5V6H4V7H3V8H2V7H1V6H0V4H2V5H4V4H5V3H6V2H7V1H8V0H9V2Z" });
    return p;
}

} // namespace os8::icon
