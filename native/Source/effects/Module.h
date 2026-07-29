// The module contract — SPEC §7.2, [SOURCE: Glue.h/Glue.c].
// Ported from the TS reference `src/runtime/module.ts`.
//
// An effect is a thing that takes a document, a host, and a settings block, and
// rewrites the audio in place. There is no undo and there must never be one (§1),
// so `effect()` is the whole commitment.
//
// The message layer (Mrun / Mset / Msar) is kept because the message is what
// distinguishes "run with last settings" — the option-key / Redo Effect path —
// from "ask, then run". In this rebuild the effects PANEL is the dialog, so
// dispatch() is thinner than Glue.c: the panel collects settings and calls run().

#pragma once

#include "Host.h"
#include "Results.h"
#include "Schema.h"
#include "Settings.h"
#include "engine/SoundDocument.h"

#include <functional>
#include <string>
#include <vector>

namespace se {

/** §7.1 — the recovered Effects Folder grouping. No group = a top-level item. */
enum class EffectGroup : int
{
    None = 0,
    EchoesAndReverbs,
    FirIirFilters,
    Resampling,
    Misc,
    MikesModules,
    Selection,
};

const char* groupName (EffectGroup) noexcept;

/**
 * What a module needs of its input before it can run at all.
 *
 * Not in the original — there, a module that disliked its input said so in an
 * alert after you invoked it. The panel can do better: an effect that cannot apply
 * to the current scope should say why up front rather than fail after Apply, since
 * Apply is irreversible.
 */
struct Requirements
{
    /** A module that is undefined on any other count. Nothing sets this yet — the
        pairwise modules turned out to want a MINIMUM, not an exact count. */
    int exactSelectedTracks = 0;      // 0 = any
    /**
     * The fewest selected tracks the module can work with.
     *
     * Sum, Difference and Iterative Crossfade need TWO — but two is a floor, not a
     * quota: each takes the first two selected and ignores the rest, and
     * Difference documents a "…from the first one?" prompt for exactly that case.
     * Demanding an exact count here would grey out work the original does.
     */
    int minSelectedTracks = 0;        // 0 = any
    /** Downsample Bits and Resample Rate convert the WHOLE document; a partial
        selection is a confirmation prompt, not a scope. */
    bool wholeDocumentOnly = false;
    /** The module grows a track, so refusing extension makes it a no-op. */
    bool mayExtend = false;
    /** The module renders into new tracks or a new document. */
    bool createsTracks = false;
    bool createsDocument = false;
};

/**
 * One effect module. Mirrors what a real EFCT resource carries: its code, its
 * settings defaults, its schema, its STR# 1000 voice, its Grup, its About text.
 */
struct EffectModule
{
    /** Menu name, e.g. "Fade In". This is the identity the fixtures key on. */
    std::string name;
    /** The Pascal-string settings key, e.g. Gain's "SampleEffect". Two modules
        sharing a signature deliberately share a settings blob. */
    std::string signature;
    EffectGroup group = EffectGroup::None;

    std::string version;
    std::string author;
    /** The About text (TEXT 1000). Shipped verbatim, truncated sentences and all
        — Shelving's help really does end mid-sentence (§8 #30). */
    std::string about;
    /** STR# 1000 — indexed by |result| for the module's own error voice (§7.7). */
    std::vector<std::string> strings;
    /** STR ' 128 — the progress window text, e.g. "Amplifying by ". */
    std::string progressText;

    Requirements requires;

    /** The settings defaults, as baked into the DITL. */
    std::function<Settings()> defaults;
    /** The declarative dialog (§7.5). Empty for a module with no parameters —
        then the panel shows only Apply — or for one that drew its own, as Dither
        does. */
    std::function<SettingsSchema()> schema;
    /** Validation/coercion applied when settings are committed, mirroring the
        original `settings()` routine's own clamping. */
    std::function<Settings (Settings)> validate;

    /** The effect itself. Mirrors `effect(modInfo, glue, &prefs)`. */
    std::function<ModResult (SoundDocument&, Host&, const Settings&)> effect;
};

/** GetModSettings — load-or-default, keyed by signature. "Run with last settings"
    needs no special path: it is just Mrun finding what the dialog left behind. */
Settings getModSettings (const EffectModule& module, const Host& host);

/**
 * Run a module with an explicit settings block: defaults, overlaid with `prefs`,
 * validated, then applied. This is what the effects panel's Apply calls, and what
 * the fixture runner calls.
 */
ModResult runEffect (const EffectModule& module, SoundDocument& doc, Host& host,
                     const Settings& prefs);

} // namespace se
