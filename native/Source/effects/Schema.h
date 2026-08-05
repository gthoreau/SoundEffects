// The declarative settings schema — SPEC §7.5, [SOURCE: ModSetups.h].
// Ported from the TS reference `src/runtime/schema.ts`.
//
// A module never draws its own dialog: it hands the host an item list (one entry
// per DITL item) and the host runs the whole thing — fields, radio groups, unit
// conversion, the named-setups popup. That indirection is why ONE panel can render
// all 36 dialogs, and it is the reason Settings is a keyed map rather than 36
// structs.
//
// This header is DATA AND VALIDATION ONLY. It does not render anything; the
// effects panel consumes it. Dither is the reminder that a few modules drew their
// own dialogs and bypass this entirely.

#pragma once

#include "Units.h"

#include <string>
#include <vector>

namespace se {

/** ModSetups.h's enum, in order — the values are positional, so keep them. */
enum class ItemType : int
{
    Ignore = 0,                        // static text / unused DITL item
    Button = 1,
    CheckBox = 2,
    CheckBoxNoSave = 3,
    RadioButton = 4,
    RadioButtonNoSave = 5,
    UnitsPopup = 6,
    EditFieldUnitDependent = 7,        // bound to a byte count, always
    EditFieldUnitDependentNoSave = 8,
    EditFieldNumerical = 9,            // bound to a long
    EditFieldNumericalNoSave = 10,
    EditFieldNumDecimal = 11,          // bound to a double
    EditFieldNumDecimalNoSave = 12,
    EditField = 13,                    // bound to a string
    EditFieldNoSave = 14,
    CustomItem = 15,
};

/**
 * One dialog item.
 *
 * Modelled as a single struct rather than a variant: the original is an array of
 * identical `ItemInfoRec`s where the type tag decides which fields mean anything,
 * and a panel that walks it wants exactly that shape. `options` is only read for
 * radio groups, `min`/`max` only for numeric fields.
 */
struct SchemaItem
{
    ItemType    type = ItemType::Ignore;
    /** Key into the module's Settings. Empty for Ignore and Button. */
    std::string key;
    std::string label;

    /** Radio groups: 1-BASED, so `options[0]` is the value 1. Every button in a
        group shares one settings key — one item carries them all. */
    std::vector<std::string> options;

    /** Numeric bounds, applied by the host as the original's `settings` routine
        clamps (Gain caps gain at a short's 32767). */
    bool hasMin = false, hasMax = false;
    double min = 0.0, max = 0.0;

    /**
     * This Ignore item labels what FOLLOWS it, not what precedes it.
     *
     * The DITL places every item by rectangle, so the original never had to say
     * this — you can see that "Gain" sits above its group and "%" sits beside its
     * field. A flat item list cannot see it, and the panel's rule for pulling a
     * short trailing Ignore onto the previous field's line ("Amplify by: [__] %")
     * has no way to tell the two apart: it read Sample Hose's five group headings
     * as units and glued each one to the last field of the PRECEDING group, so
     * "Rate" was the only group that appeared to have a name at all.
     *
     * Measuring whether the text fits — the step 6 fix — cannot decide this, since
     * "Gain" fits perfectly well. It is a question of what the label MEANS, so the
     * schema has to say.
     */
    bool heading = false;

    /**
     * Prefill this duration from what the effect is about to act on: the
     * selection's length, or the longest track's when nothing is selected.
     *
     * Only for fields that MEAN "how much of the target to touch" — the ones
     * whose kernels read 0 as "the whole selection" (the fades' time, Waveform's
     * length). It changes what the field SHOWS, never what an unedited Apply
     * does, so the golden vectors are untouched. Native-panel extension, like
     * `heading`; not recovered behaviour.
     */
    bool scopeDefault = false;
};

using SettingsSchema = std::vector<SchemaItem>;

/** Items that bind to a settings key — everything but static text and buttons. */
inline bool isBound (const SchemaItem& item) noexcept
{
    return ! item.key.empty();
}

/** NoSave items work identically but are not remembered in a saved setup. */
inline bool isNoSave (const SchemaItem& item) noexcept
{
    switch (item.type)
    {
        case ItemType::CheckBoxNoSave:
        case ItemType::RadioButtonNoSave:
        case ItemType::EditFieldUnitDependentNoSave:
        case ItemType::EditFieldNumericalNoSave:
        case ItemType::EditFieldNumDecimalNoSave:
        case ItemType::EditFieldNoSave:
            return true;

        case ItemType::Ignore:
        case ItemType::Button:
        case ItemType::CheckBox:
        case ItemType::RadioButton:
        case ItemType::UnitsPopup:
        case ItemType::EditFieldUnitDependent:
        case ItemType::EditFieldNumerical:
        case ItemType::EditFieldNumDecimal:
        case ItemType::EditField:
        case ItemType::CustomItem:
            break;
    }
    return false;
}

/** A unit-dependent field needs a units popup in the same schema to drive it —
    ModSetups.h: "There MUST be a type_unitsPopup item if one or more of these is
    present." Cheap to check, and the failure is otherwise silent. */
inline std::vector<std::string> validateSchema (const SettingsSchema& schema)
{
    std::vector<std::string> errors;
    bool hasUnitField = false, hasPopup = false;

    for (const auto& item : schema)
    {
        if (item.type == ItemType::EditFieldUnitDependent
            || item.type == ItemType::EditFieldUnitDependentNoSave)
            hasUnitField = true;
        if (item.type == ItemType::UnitsPopup)
            hasPopup = true;
        if ((item.type == ItemType::RadioButton || item.type == ItemType::RadioButtonNoSave)
            && item.options.empty())
            errors.push_back ("radio group '" + item.key + "' has no options");
    }

    if (hasUnitField && ! hasPopup)
        errors.push_back ("unit-dependent field present with no units popup item");

    return errors;
}

// --- item builders, so a module's schema() reads like a list of controls ------

inline SchemaItem staticText (std::string label)
{
    SchemaItem i;
    i.type = ItemType::Ignore;
    i.label = std::move (label);
    return i;
}

/** Static text that titles the items BELOW it — a group heading, never a unit. */
inline SchemaItem staticHeading (std::string label)
{
    SchemaItem i = staticText (std::move (label));
    i.heading = true;
    return i;
}

inline SchemaItem numberField (std::string key, std::string label)
{
    SchemaItem i;
    i.type = ItemType::EditFieldNumerical;
    i.key = std::move (key);
    i.label = std::move (label);
    return i;
}

inline SchemaItem numberField (std::string key, std::string label, double min, double max)
{
    SchemaItem i = numberField (std::move (key), std::move (label));
    i.hasMin = i.hasMax = true;
    i.min = min;
    i.max = max;
    return i;
}

inline SchemaItem decimalField (std::string key, std::string label)
{
    SchemaItem i;
    i.type = ItemType::EditFieldNumDecimal;
    i.key = std::move (key);
    i.label = std::move (label);
    return i;
}

inline SchemaItem durationField (std::string key, std::string label)
{
    SchemaItem i;
    i.type = ItemType::EditFieldUnitDependent;
    i.key = std::move (key);
    i.label = std::move (label);
    return i;
}

/** A duration meaning "how much audio to touch" — the host prefills it from the
    current scope rather than starting at 0. See SchemaItem::scopeDefault. */
inline SchemaItem spanField (std::string key, std::string label)
{
    SchemaItem i = durationField (std::move (key), std::move (label));
    i.scopeDefault = true;
    return i;
}

inline SchemaItem unitsPopup (std::string key)
{
    SchemaItem i;
    i.type = ItemType::UnitsPopup;
    i.key = std::move (key);
    return i;
}

inline SchemaItem checkBox (std::string key, std::string label)
{
    SchemaItem i;
    i.type = ItemType::CheckBox;
    i.key = std::move (key);
    i.label = std::move (label);
    return i;
}

inline SchemaItem radioGroup (std::string key, std::vector<std::string> options,
                              std::string label = {})
{
    SchemaItem i;
    i.type = ItemType::RadioButton;
    i.key = std::move (key);
    i.label = std::move (label);
    i.options = std::move (options);
    return i;
}

} // namespace se
