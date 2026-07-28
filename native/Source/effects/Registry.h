// The Effects Folder — SPEC §7.1.
//
// At launch the original scans a folder next to the app; each module contributes
// one menu item, and a module with a `Grup` string goes into that named submenu.
// Here the "folder" is this list — but the grouping data is the recovered one, and
// the ordering rule (groups in their recovered order, each list A–Z) is what the
// effects panel renders down its right-hand column.
//
// Modules are registered as they are ported. Anything absent is simply not
// offered: the fixture runner reports it as pending, and the panel never shows a
// control that cannot do anything.

#pragma once

#include "Module.h"

#include <vector>

namespace se {

/** Every ported module, in registration order. */
const std::vector<EffectModule>& effectRegistry();

/** Look one up by menu name ("Fade In"). Null when it has not been ported yet. */
const EffectModule* findEffect (const std::string& name);

/** Look one up by settings signature, as the prefs store keys them. */
const EffectModule* findEffectBySignature (const std::string& signature);

/** The panel's layout: top-level items A–Z, then each group in its recovered
    order with its own members A–Z. */
struct EffectMenu
{
    std::vector<const EffectModule*> topLevel;
    struct Group
    {
        EffectGroup id = EffectGroup::None;
        std::string name;
        std::vector<const EffectModule*> modules;
    };
    std::vector<Group> groups;
};

EffectMenu buildEffectMenu();

} // namespace se
