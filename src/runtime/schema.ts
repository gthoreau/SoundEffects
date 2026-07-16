/**
 * The declarative settings schema — SPEC §7.5, `[SOURCE: ModSetups.h]`.
 *
 * A module never draws its own dialog: it hands the host an `ItemInfoRec[]` (one
 * entry per DITL item) and the host runs the whole modal dialog — fields, radio
 * groups, unit conversion, and the named-setups popup. That indirection is why
 * the same schema can drive an HTML form now and an OLED + encoder page on a
 * Daisy later (§12b).
 *
 * This module is **data and validation only**. It does not render. Step 6 (UI
 * shell) consumes it; the Dither module is the reminder that a few modules drew
 * their own dialogs and bypass this entirely.
 */

import type { UnitCode } from "../units.js";
import { kModCancel } from "./results.js";

/** `ModSetups.h`'s enum, in order — the values are positional, so keep them. */
export const ItemType = {
  ignore: 0,
  button: 1,
  checkBox: 2,
  checkBoxNoSave: 3,
  radioButton: 4,
  radioButtonNoSave: 5,
  unitsPopup: 6,
  editFieldUnitDependent: 7,
  editFieldUnitDependentNoSave: 8,
  editFieldNumerical: 9,
  editFieldNumericalNoSave: 10,
  editFieldNumDecimal: 11,
  editFieldNumDecimalNoSave: 12,
  editField: 13,
  editFieldNoSave: 14,
  customItem: 15,
} as const;

export type ItemTypeCode = (typeof ItemType)[keyof typeof ItemType];

/**
 * What a button/custom proc returns. `[SOURCE: ModSetups.h]`
 *
 * Note `valuesChanged` (−1): it means "the values now differ from the selected
 * setup", which deselects the setups popup. It is not an error despite being
 * negative — the one place in this ABI where a negative isn't trouble.
 */
export const ButtonResult = {
  ok: 1,
  dismissAsOK: 0,
  dismissAsCancel: kModCancel,
  valuesChanged: -1,
} as const;

/** Bound to a `short` holding 0/1. */
export interface CheckBoxItem {
  type: typeof ItemType.checkBox | typeof ItemType.checkBoxNoSave;
  /** Key into the module's settings object. */
  key: string;
  label?: string;
}

/**
 * Bound to a `short` = the active index, **1-based**. Every button in a group
 * shares one pointer — modelled here as one item carrying all its options.
 */
export interface RadioGroupItem {
  type: typeof ItemType.radioButton | typeof ItemType.radioButtonNoSave;
  key: string;
  /** 1-based; `options[0]` is index 1. */
  options: string[];
  label?: string;
}

/** Bound to a `short` holding a `ModUnits.h` code. */
export interface UnitsPopupItem {
  type: typeof ItemType.unitsPopup;
  key: string;
}

/**
 * Bound to an `unsigned long` **in bytes, always**. The host converts to and
 * from the units popup's current unit; the module never sees anything but bytes.
 * Requires a `unitsPopup` item to be present in the same schema.
 */
export interface UnitDependentFieldItem {
  type:
    | typeof ItemType.editFieldUnitDependent
    | typeof ItemType.editFieldUnitDependentNoSave;
  key: string;
  label?: string;
}

/** Bound to a `long`. */
export interface NumericalFieldItem {
  type: typeof ItemType.editFieldNumerical | typeof ItemType.editFieldNumericalNoSave;
  key: string;
  label?: string;
  min?: number;
  max?: number;
}

/** Bound to a `double`. */
export interface DecimalFieldItem {
  type: typeof ItemType.editFieldNumDecimal | typeof ItemType.editFieldNumDecimalNoSave;
  key: string;
  label?: string;
}

/** Bound to a StringPtr. */
export interface TextFieldItem {
  type: typeof ItemType.editField | typeof ItemType.editFieldNoSave;
  key: string;
  label?: string;
}

/** Static text / unused DITL item. */
export interface IgnoreItem {
  type: typeof ItemType.ignore;
  label?: string;
}

export interface ButtonItem {
  type: typeof ItemType.button;
  label: string;
  /** Returns a `ButtonResult`, or any other value to cancel and report it. */
  action: (settings: Record<string, unknown>) => number;
}

export type SchemaItem =
  | IgnoreItem
  | ButtonItem
  | CheckBoxItem
  | RadioGroupItem
  | UnitsPopupItem
  | UnitDependentFieldItem
  | NumericalFieldItem
  | DecimalFieldItem
  | TextFieldItem;

export type SettingsSchema = SchemaItem[];

const NO_SAVE = new Set<number>([
  ItemType.checkBoxNoSave,
  ItemType.radioButtonNoSave,
  ItemType.editFieldUnitDependentNoSave,
  ItemType.editFieldNumericalNoSave,
  ItemType.editFieldNumDecimalNoSave,
  ItemType.editFieldNoSave,
]);

/** `NoSave` items work identically but are not remembered in a saved setup. */
export function isNoSave(item: SchemaItem): boolean {
  return NO_SAVE.has(item.type);
}

/** Items that bind to a settings key (i.e. everything but static text/buttons). */
export function boundItems(schema: SettingsSchema): Array<SchemaItem & { key: string }> {
  return schema.filter((i): i is SchemaItem & { key: string } => "key" in i);
}

/** The unit code currently governing the schema's unit-dependent fields. */
export function unitsOf(
  schema: SettingsSchema,
  settings: Record<string, unknown>,
): UnitCode | null {
  const popup = schema.find((i) => i.type === ItemType.unitsPopup);
  if (!popup || !("key" in popup)) return null;
  return settings[popup.key] as UnitCode;
}

/**
 * A schema with a unit-dependent field but no units popup is malformed —
 * ModSetups.h: "There MUST be a type_unitsPopup item if one or more of these is
 * present." Cheap to check, and the failure is otherwise silent.
 */
export function validateSchema(schema: SettingsSchema): string[] {
  const errors: string[] = [];
  const hasUnitField = schema.some(
    (i) =>
      i.type === ItemType.editFieldUnitDependent ||
      i.type === ItemType.editFieldUnitDependentNoSave,
  );
  const hasPopup = schema.some((i) => i.type === ItemType.unitsPopup);
  if (hasUnitField && !hasPopup) {
    errors.push("unit-dependent field present with no type_unitsPopup item");
  }
  for (const item of schema) {
    if (item.type === ItemType.radioButton || item.type === ItemType.radioButtonNoSave) {
      if (item.options.length === 0) errors.push(`radio group '${item.key}' has no options`);
    }
  }
  return errors;
}

/**
 * A saved setup: the values of every non-`NoSave` bound item.
 *
 * Host-managed, per §7.5 — the "Setup:" popup with Save setup as… / Delete
 * setup… The Flanger ships 9 of these as `Setp` resources.
 */
export interface NamedSetup {
  name: string;
  values: Record<string, unknown>;
}

export function captureSetup(
  schema: SettingsSchema,
  settings: Record<string, unknown>,
  name: string,
): NamedSetup {
  const values: Record<string, unknown> = {};
  for (const item of schema) {
    if (!("key" in item) || isNoSave(item)) continue;
    values[item.key] = settings[item.key];
  }
  return { name, values };
}

export function applySetup(
  setup: NamedSetup,
  settings: Record<string, unknown>,
): Record<string, unknown> {
  return { ...settings, ...setup.values };
}
