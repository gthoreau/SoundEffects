#include "EffectLayouts.h"

#include <map>

namespace fxlayout {

namespace {

using std::string;

Cell label (float x, float y, string t, bool small = false)
{
    Cell c; c.kind = Kind::label; c.x = x; c.y = y; c.text = std::move (t); c.small = small;
    return c;
}

Cell field (string key, float x, float y, int w, string unitsKey = {}, int flags = 0, string gate = {})
{
    Cell c; c.kind = Kind::field; c.x = x; c.y = y; c.w = w; c.key = std::move (key);
    c.unitsKey = std::move (unitsKey); c.flags = flags; c.gate = std::move (gate);
    return c;
}

Cell units (string key, float x, float y, int w = 100, string gate = {})
{
    Cell c; c.kind = Kind::units; c.x = x; c.y = y; c.w = w; c.key = std::move (key); c.gate = std::move (gate);
    return c;
}

Cell choice (string key, float x, float y, int w, string items)
{
    Cell c; c.kind = Kind::choice; c.x = x; c.y = y; c.w = w; c.key = std::move (key); c.text = std::move (items);
    return c;
}

Cell setup (float x = 44, float y = 4)
{
    Cell c; c.kind = Kind::setup; c.x = x; c.y = y; c.w = 100;
    return c;
}

Cell check (string key, float x, float y, string caption, string gate = {})
{
    Cell c; c.kind = Kind::check; c.x = x; c.y = y; c.key = std::move (key); c.text = std::move (caption);
    c.gate = std::move (gate);
    return c;
}

Cell radio (string key, int value, float x, float y, string caption, string gate = {})
{
    Cell c; c.kind = Kind::radio; c.x = x; c.y = y; c.key = std::move (key); c.value = value;
    c.text = std::move (caption); c.gate = std::move (gate);
    return c;
}

Cell curve (string key, float x, float y)
{
    Cell c; c.kind = Kind::curvePreset; c.x = x; c.y = y; c.w = 100; c.key = std::move (key);
    return c;
}

Cell legend (Kind k, float x, float y)
{
    Cell c; c.kind = k; c.x = x; c.y = y;
    return c;
}

Row row (int h, std::vector<Cell> cells = {})
{
    return { h, std::move (cells) };
}

Row gap (int h) { return row (h); }

// The "Setup:" row every effect with setups starts with, and the spacer under it.
std::vector<Row> setupRows (int spacer = 14)
{
    return { row (26, { label (0, 7.5f, "Setup:"), setup() }), gap (spacer) };
}

Layout make (string title, std::vector<Row> rows)
{
    Layout l; l.title = std::move (title); l.rows = std::move (rows);
    return l;
}

Layout with (std::vector<Row> head, string title, std::vector<Row> rows)
{
    for (auto& r : rows) head.push_back (std::move (r));
    return make (std::move (title), std::move (head));
}

// "Fading time:" / "Length:" over a field and its units popup.
Layout spanLayout (string title, string caption, string key)
{
    return make (std::move (title), {
        gap (14),
        row (18, { label (0, 4, std::move (caption)) }),
        row (25, { field (std::move (key), 0, 4.5f, 68, "units"), units ("units", 72, 4) }),
    });
}

Layout iirLayout (string title)
{
    return with (setupRows(), std::move (title), {
        row (24, { label (0, 7, "Coefficients:"), label (90, 7, "A:"),
                   field ("a", 104, 4, 60, {}, decimals4), label (182, 7, "B:"),
                   field ("b", 196, 4, 60, {}, decimals4) }),
    });
}

// One of Sample Hose's five parameter groups: the enabling checkbox, then the
// value, variance and curvature rows (251 px content, positions per 415:4184).
void hoseGroup (std::vector<Row>& rows, const string& key, const string& caption, const string& hint,
                float hintX, const string& valueLabel, bool inUnits)
{
    const int fw = inUnits ? 65 : 40;
    const float finalX = inUnits ? 177.0f : 178.0f;
    const float endIconX = inUnits ? 163.0f : 164.0f;
    const string gate = key + ".on";

    rows.push_back (row (20, { check (gate, 0, 4, caption), label (hintX, 5.5f, hint, true) }));

    const auto pair = [&] (const string& what, const string& first, const string& second, const string& l)
    {
        std::vector<Cell> cells {
            label (0, 7, l), legend (Kind::legendStart, 74, 6.5f),
            field (key + "." + first, 88, 4, fw, inUnits ? "units" : "", 0, gate),
            legend (Kind::legendEnd, endIconX, 6.5f),
            field (key + "." + second, finalX, 4, fw, inUnits ? "units" : "", 0, gate),
        };
        if (! inUnits)
        {
            cells.push_back (label (132, 7, "%"));
            cells.push_back (label (222, 7, "%"));
        }
        (void) what;
        rows.push_back (row (24, std::move (cells)));
    };

    pair ("value", "initialValue", "finalValue", valueLabel);
    pair ("variance", "initialVariance", "finalVariance", "Variance \xC2\xB1:");

    Cell preset = curve (key + ".curvature", 74, 4);
    preset.gate = gate;
    rows.push_back (row (25, { label (0, 7.5f, "Curvature:"), preset,
                               field (key + ".curvature", 178, 4.5f, 30, {}, 0, gate),
                               label (212, 7.5f, "%") }));
}

Layout sampleHose()
{
    std::vector<Row> rows = setupRows();
    rows.push_back (row (25, { label (0, 7.5f, "Time Unit:"), units ("units", 79, 4) }));
    rows.push_back (row (24, { label (0, 7, "New Length:"), field ("newLength", 79, 4, 65, "units"),
                               check ("stereo", 162, 6, "Stereo") }));
    rows.push_back (gap (14));
    rows.push_back (row (19, { legend (Kind::legendStart, 74, 4), label (88, 4.5f, "Initial"),
                               legend (Kind::legendEnd, 165, 4), label (179, 4.5f, "Final") }));
    hoseGroup (rows, "rate", "Rate", "Sample pitch (% of actual)", 46, "Rate (Pitch):", false);
    rows.push_back (gap (14));
    hoseGroup (rows, "gain", "Gain", "Sample volume (% of actual)", 44, "Gain:", false);
    rows.push_back (gap (14));
    hoseGroup (rows, "pan", "Pan", "L/R distribution (% of actual)", 41, "Pan:", false);
    rows.push_back (gap (14));
    hoseGroup (rows, "bite", "Bite", "Sample crop (time)", 42, "Crop size:", true);
    rows.push_back (gap (14));
    hoseGroup (rows, "period", "Period", "Intervals between samples (time)", 56, "Interval:", true);
    return make ("Sample Hose", std::move (rows));
}

std::map<string, Layout> build()
{
    std::map<string, Layout> m;

    m["Amplify"] = make ("Amplify", {
        gap (14),
        row (26, { label (0, 8, "Amplify by:"), [] { Cell c; c.kind = Kind::slider; c.x = 71; c.y = 4; c.w = 117;
                                                       c.key = "gain"; return c; }(),
                   field ("gain", 197, 5, 41), label (240, 8, "%") }),
    });

    m["Delay"] = with (setupRows(), "Delay", {
        row (25, { label (0, 7.5f, "Delay:"), field ("delay", 59, 4.5f, 60, "units"), units ("units", 123, 4) }),
        row (24, { label (0, 7, "Strength:"), field ("strength", 59, 4, 60), label (123, 7, "%") }),
        gap (8),
        row (20, { check ("@extend", 0, 4, "Allow the sound to get longer") }),
    });

    m["Echo"] = with (setupRows(), "Echo", {
        row (25, { label (0, 7.5f, "Delay:"), field ("delay", 59, 4.5f, 60, "units"), units ("units", 123, 4) }),
        row (24, { label (0, 7, "Strength:"), field ("strength", 59, 4, 60), label (123, 7, "%") }),
        row (24, { check ("onlyOn", 0, 6, "Only"), field ("onlyCount", 59, 4, 60), label (123, 7, "Echoes") }),
        gap (8),
        row (20, { check ("@extend", 0, 4, "Allow the sound to get longer") }),
    });

    m["Difference"] = make ("Difference", {
        gap (14),
        row (18, { label (0, 4, "Store the difference in a:") }),
        row (20, { radio ("destination", 1, 0, 4, "New Channel") }),
        row (20, { radio ("destination", 2, 0, 4, "New Document") }),
        gap (8),
        row (20, { check ("normalize", 0, 4, "Normalize the difference") }),
    });

    m["Dither"] = make ("Dither", {
        gap (14),
        row (24, { label (0, 7, "Add:"), radio ("mode", 1, 44, 6, ""), field ("percent", 60, 4, 30),
                   label (94, 7, "%"), radio ("mode", 2, 118, 6, ""), field ("points", 134, 4, 30),
                   label (168, 7, "points") }),
        row (18, { label (0, 4, "of random noise") }),
    });

    m["Downsample Bits"] = make ("Downsample", {
        gap (14),
        row (24, { label (0, 7, "Downsample to:"), field ("newBps", 94, 4, 30), label (128, 7, "bits") }),
        gap (8),
        row (20, { check ("expert", 0, 4, "Expert features") }),
        row (24, { radio ("expertMode", 1, 16, 6, "Dither", "expert"),
                   field ("ditherLSB", 71, 4, 37, {}, 0, "expert"), label (112, 7, "LSB in amplitude") }),
        row (20, { radio ("expertMode", 2, 16, 4, "Noise shaping", "expert") }),
    });

    m["Fade In"] = spanLayout ("Fade In", "Fading time:", "time");
    m["Fade Out"] = spanLayout ("Fade Out", "Fading time:", "time");
    // Not in the design: the same two controls the fades have, over Draw Line's key.
    m["Draw Line"] = spanLayout ("Draw Line", "Length:", "length");

    m["Bandpass IIR"] = iirLayout ("Bandpass IIR");
    m["High Pass IIR"] = iirLayout ("High-Pass IIR");
    m["Low Pass IIR"] = iirLayout ("Low-Pass IIR");
    m["Shelving"] = iirLayout ("Shelving");

    m["Flanger"] = with (setupRows(), "Flanger", {
        row (34, { label (0, 12, "Flange percentage:"), field ("flangePercent", 112, 9, 43), label (159, 12, "%"),
                   check ("feedback", 189, 11, "Feedback") }),
        gap (8),
        row (25, { label (0, 7.5f, "Delay:"), field ("delay", 44, 4.5f, 47, "delayUnits"), units ("delayUnits", 95, 4) }),
        row (25, { label (0, 7.5f, "Width:"), field ("width", 44, 4.5f, 47, "delayUnits"), units ("delayUnits", 95, 4) }),
        row (25, { label (0, 7.5f, "Period:"), field ("period", 44, 4.5f, 47, "delayUnits"), units ("delayUnits", 95, 4) }),
        gap (8),
        row (25, { label (0, 7.5f, "Modulation:"), choice ("modulation", 95, 4, 100, "Sinusoidal|Triangular|Sawtooth") }),
        gap (8),
        row (20, { check ("envelopeFollower", 0, 4, "Envelope Follower") }),
        row (25, { label (16, 7.5f, "Depth:"), field ("depth", 56, 4.5f, 58, "depthUnits", 0, "envelopeFollower"),
                   units ("depthUnits", 118, 4, 100, "envelopeFollower") }),
        row (20, { label (16, 5, "Louder means:"), radio ("louderMeans", 1, 102, 4, "Longer", "envelopeFollower"),
                   radio ("louderMeans", 2, 161, 4, "Shorter", "envelopeFollower"), label (224, 5, "delays") }),
    });

    m["Granular Synthesis"] = with (setupRows(), "Granular Synthesis", {
        row (25, { label (0, 7.5f, "Time Unit:"), units ("units", 79, 4) }),
        row (24, { label (0, 7, "New Length:"), field ("newLength", 79, 4, 60, "units") }),
        gap (8),
        row (24, { label (0, 7, "Length:"), label (79, 7, "Initial"), field ("initialGrainLength", 115, 4, 50, "units"),
                   label (169, 7, "Final"), field ("finalGrainLength", 200, 4, 50, "units") }),
        row (24, { label (0, 7, "Density:"), label (79, 7, "Initial"), field ("initialDensity", 115, 4, 30),
                   label (149, 7, "%"), label (170, 7, "Final"), field ("finalDensity", 201, 4, 30), label (235, 7, "%") }),
        row (24, { label (0, 7, "Variance \xC2\xB1:"), field ("variance", 79, 4, 50, "units") }),
        row (25, { label (0, 7.5f, "Curvature:"), curve ("curvature", 79, 4), field ("curvature", 183, 4.5f, 30),
                   label (217, 7.5f, "%") }),
        row (24, { label (0, 7, "Fade Level:"), field ("fadeLevel", 79, 4, 50), label (133, 7, "%") }),
    });

    m["Iterative Crossfade"] = with (setupRows (8), "Iterative Crossfade", {
        row (24, { label (0, 7, "# of Iterations:"), field ("iterations", 93, 4, 55) }),
        row (25, { label (0, 7.5f, "Silence Length:"), field ("silenceLength", 92, 4.5f, 55, "units"),
                   units ("units", 151, 4) }),
    });

    m["Quick Mix"] = make ("Quick Mix", {
        gap (14),
        row (18, { label (0, 4, "Mix to:") }),
        row (20, { radio ("destination", 1, 16, 4, "New Channel") }),
        row (20, { radio ("destination", 2, 16, 4, "New Document") }),
        gap (8),
        row (18, { label (0, 4, "Use:") }),
        row (20, { radio ("use", 2, 16, 4, "Stereo") }),
        row (20, { radio ("use", 1, 16, 4, "Mono") }),
        gap (8),
        row (20, { check ("usePan", 0, 4, "Use each channel\xE2\x80\x99s pan") }),
    });

    m["Replicate"] = make ("Replicate", {
        gap (14),
        row (24, { label (0, 7, "Number of copies:"), field ("copies", 108, 4, 30) }),
        gap (8),
        row (20, { radio ("gapMode", 1, 0, 4, "No Gaps") }),
        row (24, { radio ("gapMode", 2, 0, 6, "Gap = selection divided by"), field ("gapDivisor", 196, 4, 30) }),
        row (24, { radio ("gapMode", 3, 0, 6, "Gap = selection multiplied by"), field ("gapMultiplier", 199, 4, 30) }),
        row (25, { radio ("gapMode", 4, 0, 6.5f, "Gap ="), field ("gapLength", 51, 4.5f, 63, "units"),
                   units ("units", 118, 4) }),
    });

    m["Resample Rate"] = make ("Resample Rate", {
        gap (14),
        row (18, { label (0, 4, "Resample the sound at (Hz):") }),
        row (25, { field ("newRate", 0, 4.5f, 63, {}, fixed16),
                   [] { Cell c; c.kind = Kind::ratePreset; c.x = 67; c.y = 4; c.w = 100; c.key = "newRate"; return c; }() }),
    });

    m["Sample Hose"] = sampleHose();

    m["Upsample Bits"] = make ("Upsample Bits", {
        gap (14),
        row (24, { label (0, 7, "Upsample to"), field ("newBps", 73, 4, 63), label (140, 7, "bits") }),
    });

    m["Waveform Generator"] = with (setupRows(), "Waveform Generator", {
        row (25, { label (0, 7.5f, "Length:"), field ("length", 64, 4.5f, 70, "units"), units ("units", 138, 4) }),
        row (25, { label (0, 7.5f, "Freq.:"),
                   [] { Cell c; c.kind = Kind::notePick; c.x = 64; c.y = 4; c.w = 48; c.key = "frequency"; return c; }(),
                   [] { Cell c; c.kind = Kind::octavePick; c.x = 116; c.y = 4; c.w = 60; c.key = "frequency"; return c; }(),
                   field ("frequency", 180, 4.5f, 47, {}, kilo), label (231, 7.5f, "kHz") }),
        row (24, { label (0, 7, "Amplitude:"), field ("amplitude", 64, 4, 70), label (138, 7, "%") }),
        row (20, { label (0, 5, "Wave:"), radio ("waveType", 1, 64, 4, "Sine") }),
        row (20, { radio ("waveType", 2, 64, 4, "Square") }),
        row (20, { radio ("waveType", 3, 64, 4, "Sawtooth") }),
    });

    m["Zero Crosser"] = make ("Zero Crosser", {
        gap (14),
        row (20, { radio ("action", 2, 0, 4, "Shrink") }),
        row (20, { radio ("action", 1, 0, 4, "Expand") }),
        row (28, { label (0, 4, "the selection to the closest"), label (0, 14, "zero-crossing on") }),
        row (20, { radio ("side", 3, 0, 4, "Both sides") }),
        row (20, { radio ("side", 2, 0, 4, "The right") }),
        row (20, { radio ("side", 1, 0, 4, "The left") }),
    });

    return m;
}

} // namespace

int Layout::contentHeight() const
{
    int h = kTitleH;
    for (const auto& r : rows) h += r.h;
    return h + kSpacerH + kButtonsH;
}

const Layout* layoutFor (const std::string& moduleName)
{
    static const std::map<std::string, Layout> layouts = build();
    const auto it = layouts.find (moduleName);
    return it == layouts.end() ? nullptr : &it->second;
}

Layout oneClickLayout (const std::string& effectName, int warningLines)
{
    Layout l = make (effectName, { gap (14), row (18, { label (0, 4, "Apply " + effectName + ".") }) });
    // Weird Tempo's own apology, a line at a time: the tray is the one place it can be
    // read BEFORE the irreversible click.
    for (int i = 0; i < warningLines; ++i) l.rows.push_back (row (12));
    return l;
}

} // namespace fxlayout
