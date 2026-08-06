#include "EffectsPanel.h"

#include "effects/Units.h"

#include <algorithm>
#include <cmath>

using juce::Graphics;
using juce::Rectangle;
using sys7::kDim;
using sys7::kInk;
using sys7::kPanel;
using sys7::kWhite;

namespace {

/** The selection highlight — the same inverted-fill idea as a pressed button. */
const juce::Colour kOpenFill { 0xff2a2a2a };
// A shade off the panel ground, not a bar across it: hover says "the pointer is
// here", and on a near-white list anything heavier reads as a second selection
// competing with the open effect's black row.
const juce::Colour kHoverFill { 0xffe6e6e6 };

// ModSetups.h pairs almost every item type with a "NoSave" twin that behaves
// identically and is simply left out of a saved setup. Asking "is this a radio
// group?" rather than switching over all sixteen codes keeps that pairing in one
// place — and keeps the item-type switches from being fifteen-way.
bool isRadio (const se::SchemaItem& i)
{
    return i.type == se::ItemType::RadioButton || i.type == se::ItemType::RadioButtonNoSave;
}

bool isCheckBox (const se::SchemaItem& i)
{
    return i.type == se::ItemType::CheckBox || i.type == se::ItemType::CheckBoxNoSave;
}

bool isDurationField (const se::SchemaItem& i)
{
    return i.type == se::ItemType::EditFieldUnitDependent
        || i.type == se::ItemType::EditFieldUnitDependentNoSave;
}

bool isDecimalField (const se::SchemaItem& i)
{
    return i.type == se::ItemType::EditFieldNumDecimal
        || i.type == se::ItemType::EditFieldNumDecimalNoSave;
}

bool isTextField (const se::SchemaItem& i)
{
    return i.type == se::ItemType::EditField || i.type == se::ItemType::EditFieldNoSave;
}

bool isNumberField (const se::SchemaItem& i)
{
    return i.type == se::ItemType::EditFieldNumerical
        || i.type == se::ItemType::EditFieldNumericalNoSave;
}

bool hasEditor (const se::SchemaItem& i)
{
    return isNumberField (i) || isDecimalField (i) || isDurationField (i) || isTextField (i);
}

const char* unitName (se::Unit u)
{
    switch (u)
    {
        case se::Unit::Samples:      return "samples";
        case se::Unit::Bytes:        return "bytes";
        case se::Unit::Seconds:      return "sec";
        case se::Unit::Milliseconds: return "ms";
        case se::Unit::Meters:       return "m";
        case se::Unit::Feet:         return "ft";
    }
    return "?";
}

/** A duration, said the way a person would. */
juce::String sayDuration (size_t bytes, const se::SoundDocument& doc)
{
    const double seconds = se::bytesToUnit (bytes, se::Unit::Seconds, doc.rateHz, doc.bps);
    if (seconds < 1.0)
        return juce::String (juce::roundToInt (seconds * 1000.0)) + " ms";
    if (seconds < 60.0)
        return juce::String (seconds, seconds < 10.0 ? 2 : 1) + " s";

    const int mins = (int) (seconds / 60.0);
    const int secs = juce::roundToInt (seconds - mins * 60.0);
    return juce::String (mins) + ":" + juce::String (secs).paddedLeft ('0', 2);
}

} // namespace

// ============================================================================
// ParamRow
// ============================================================================

int ParamRow::heightFor (const se::SchemaItem& item)
{
    // Stacked vertically, one option per line: the labels are sentences ("Gap =
    // selection divided by"), and side by side they would be unreadable in a
    // 250px column.
    if (isRadio (item))
        return 2 + (int) item.options.size() * kOptionH + (item.label.empty() ? 0 : kOptionH);

    // An Ignore item with no text is a DITL spacer — it draws nothing, so it
    // should take no room either.
    if (item.type == se::ItemType::Ignore)
        return item.label.empty() ? 0 : kOptionH;

    return kFieldH + 4;
}

ParamRow::ParamRow (const se::SchemaItem& i, se::Settings& s, const se::SoundDocument* d)
    : item (i), settings (s), doc (d)
{
    if (hasEditor (item))
    {
        editor = std::make_unique<juce::TextEditor>();
        editor->setFont (juce::FontOptions (11.0f));
        editor->setColour (juce::TextEditor::backgroundColourId, kWhite);
        editor->setColour (juce::TextEditor::textColourId, kInk);
        editor->setColour (juce::TextEditor::outlineColourId, kInk);
        editor->setColour (juce::TextEditor::focusedOutlineColourId, kInk);
        editor->setColour (juce::TextEditor::highlightColourId, kInk.withAlpha (0.2f));
        editor->setColour (juce::TextEditor::highlightedTextColourId, kInk);
        editor->setColour (juce::TextEditor::shadowColourId, juce::Colours::transparentBlack);
        editor->setIndents (3, 2);
        editor->setJustification (juce::Justification::centredLeft);
        // Return in a field commits it and then runs the effect — the field is part
        // of the form, not a thing of its own, so the key that ends the form ends it
        // from wherever the caret happens to be. Commit first: Apply must use the
        // number the person can see.
        editor->onReturnKey = [this]
        {
            commitText();
            if (onReturnPressed) onReturnPressed();
        };
        editor->onFocusLost = [this] { commitText(); };
        addAndMakeVisible (*editor);
    }

    refreshFromSettings();
}

bool ParamRow::canFitSuffix (const std::string& text) const
{
    if (editor == nullptr || text.empty()) return false;

    // The row is laid out at the panel's width, so the space left of the right
    // edge is knowable before anything is positioned. 16 = the panel's own
    // padding either side of the form.
    const int fieldRight = (item.label.empty() ? 0 : kLabelW) + 78;
    const int available = EffectsPanelComponent::kMaxWidth - 16 - fieldRight - 6;

    const juce::Font font (juce::FontOptions (11.0f));
    return juce::GlyphArrangement::getStringWidthInt (font, juce::String (text)) <= available;
}

void ParamRow::setSuffix (const std::string& text)
{
    suffix = juce::String (text);
    repaint();
}

void ParamRow::refreshFromSettings()
{
    if (editor == nullptr) return;

    juce::String text;

    if (isDurationField (item))
    {
        // Stored in BYTES, always — the unit is a display choice (§2.3). Show it
        // in whatever the schema's units popup currently says.
        const auto unit = static_cast<se::Unit> (se::getInt (settings, "units",
                                                             (int64_t) se::Unit::Milliseconds));
        const double shown = doc != nullptr
            ? se::bytesToUnit ((size_t) se::getInt (settings, item.key), unit, doc->rateHz, doc->bps)
            : (double) se::getInt (settings, item.key);

        text = juce::String (shown, shown == std::floor (shown) ? 0 : 3);
    }
    else if (isDecimalField (item))
    {
        text = juce::String (se::getDouble (settings, item.key), 2);
    }
    else if (isTextField (item))
    {
        const auto it = settings.find (item.key);
        text = it == settings.end() ? juce::String() : juce::String (it->second.asString());
    }
    else
    {
        text = juce::String (se::getInt (settings, item.key));
    }

    editor->setText (text, false);
    shownText = text;
}

void ParamRow::commitText()
{
    if (editor == nullptr) return;
    const juce::String text = editor->getText();

    // Unchanged text is not a commit. Converting the display back through the
    // unit can nudge the stored bytes by a rounding step, and a click into and
    // out of the field must not count as the user overriding a derived default.
    if (text == shownText) return;
    shownText = text;

    if (isDurationField (item))
    {
        // The field shows a unit; the module only ever sees bytes (§2.3).
        const auto unit = static_cast<se::Unit> (se::getInt (settings, "units",
                                                             (int64_t) se::Unit::Milliseconds));
        const size_t bytes = doc != nullptr
            ? se::unitToBytes (text.getDoubleValue(), unit, doc->rateHz, doc->bps)
            : (size_t) text.getLargeIntValue();
        settings[item.key] = se::Value ((int64_t) bytes);
    }
    else if (isDecimalField (item))
    {
        settings[item.key] = se::Value (text.getDoubleValue());
    }
    else if (isTextField (item))
    {
        settings[item.key] = se::Value (text.toStdString());
    }
    else
    {
        int64_t v = text.getLargeIntValue();
        // The schema's own bounds, applied where the original's settings() routine
        // clamps (Gain caps its gain at a short's 32767).
        if (item.hasMin) v = std::max (v, (int64_t) item.min);
        if (item.hasMax) v = std::min (v, (int64_t) item.max);
        settings[item.key] = se::Value (v);
    }

    if (onUserEdit) onUserEdit (item.key);
    if (onChanged) onChanged();
}

Rectangle<int> ParamRow::controlBounds() const
{
    auto r = getLocalBounds().reduced (0, 2);
    // A units popup carries no schema label — the caption is ours ("Units:") — but
    // it still sits in the label column, because the field it drives is directly
    // above it and a popup indented differently from that field reads as belonging
    // to something else.
    r.removeFromLeft (hasLabelColumn() ? kLabelW : 0);
    return r;
}

bool ParamRow::hasLabelColumn() const
{
    return ! item.label.empty() || item.type == se::ItemType::UnitsPopup;
}

Rectangle<int> ParamRow::optionBounds (int index) const
{
    auto r = getLocalBounds();
    if (! item.label.empty()) r.removeFromTop (kOptionH);
    r.removeFromTop (2);
    return r.removeFromTop (kOptionH * (index + 1)).removeFromBottom (kOptionH);
}

void ParamRow::resized()
{
    if (editor != nullptr)
    {
        auto r = controlBounds();
        // Leave room for the trailing unit/percent text the schema supplies as a
        // separate Ignore item; a field that fills the column looks like it owns
        // the whole line when it does not.
        editor->setBounds (r.removeFromLeft (std::min (r.getWidth(), 78)).withHeight (kFieldH));
    }
}

void ParamRow::paint (Graphics& g)
{
    g.setFont (juce::FontOptions (11.0f));
    g.setColour (kInk);

    // Static text: the schema uses it for the trailing "%" and for group captions
    // like "Coefficients".
    if (item.type == se::ItemType::Ignore)
    {
        if (! item.label.empty())
        {
            g.setColour (kDim);
            g.drawText (item.label, getLocalBounds(), juce::Justification::centredLeft, false);
        }
        return;
    }

    if (isCheckBox (item))
    {
        auto box = getLocalBounds().reduced (0, 3).removeFromLeft (13).withHeight (13);
        const bool on = se::getBool (settings, item.key);

        g.setColour (on ? kInk : kWhite);
        g.fillRect (box);
        g.setColour (kInk);
        g.drawRect (box, 1);

        if (on)
        {
            g.setColour (kWhite);
            g.drawLine ((float) box.getX() + 3, (float) box.getCentreY(),
                        (float) box.getCentreX(), (float) box.getBottom() - 3, 1.6f);
            g.drawLine ((float) box.getCentreX(), (float) box.getBottom() - 3,
                        (float) box.getRight() - 2, (float) box.getY() + 3, 1.6f);
        }

        g.setColour (kInk);
        g.drawText (item.label, getLocalBounds().withTrimmedLeft (18),
                    juce::Justification::centredLeft, false);
        return;
    }

    if (isRadio (item))
    {
        if (! item.label.empty())
        {
            g.setColour (kDim);
            g.drawText (item.label, getLocalBounds().removeFromTop (kOptionH),
                        juce::Justification::centredLeft, false);
        }

        // 1-BASED: options[0] is the value 1, as every radio group in
        // ModSetups.h is.
        const int64_t current = se::getInt (settings, item.key, 1);

        for (int i = 0; i < (int) item.options.size(); ++i)
        {
            auto row = optionBounds (i);
            auto dot = row.reduced (0, 2).removeFromLeft (11).withHeight (11);

            g.setColour (kWhite);
            g.fillEllipse (dot.toFloat());
            g.setColour (kInk);
            g.drawEllipse (dot.toFloat().reduced (0.5f), 1.0f);

            if (current == i + 1)
            {
                g.setColour (kInk);
                g.fillEllipse (dot.toFloat().reduced (3.0f));
            }

            g.setColour (kInk);
            g.drawText (item.options[(size_t) i], row.withTrimmedLeft (16),
                        juce::Justification::centredLeft, false);
        }
        return;
    }

    if (item.type == se::ItemType::UnitsPopup)
    {
        // Captioned exactly like a field label — same column, same right edge, same
        // ink — so "Delay:" and "Units:" line up and the popup lines up under the
        // field it rescales.
        g.setColour (kInk);
        g.drawText ("Units:", getLocalBounds().removeFromLeft (kLabelW - 4),
                    juce::Justification::centredRight, false);

        auto cell = controlBounds().removeFromLeft (78).withHeight (kFieldH);
        g.setColour (kWhite);
        g.fillRect (cell);
        g.setColour (kInk);
        g.drawRect (cell, 1);
        g.drawText (unitName (static_cast<se::Unit> (se::getInt (settings, item.key))),
                    cell.reduced (4, 0), juce::Justification::centredLeft, false);

        // The System 7 popup marker: a small solid triangle at the right edge.
        juce::Path tri;
        const float cx = (float) cell.getRight() - 9.0f;
        const float cy = (float) cell.getCentreY();
        tri.addTriangle (cx - 3.5f, cy - 2.0f, cx + 3.5f, cy - 2.0f, cx, cy + 3.0f);
        g.fillPath (tri);
        return;
    }

    // Everything else is a field, and the editor draws itself — all that is left
    // is its label, set against the field's left edge, and any trailing unit.
    if (! item.label.empty())
    {
        g.setColour (kInk);
        g.drawText (item.label, getLocalBounds().removeFromLeft (kLabelW - 4),
                    juce::Justification::centredRight, false);
    }

    if (suffix.isNotEmpty() && editor != nullptr)
    {
        g.setColour (kDim);
        g.drawText (suffix, getLocalBounds().withTrimmedLeft (editor->getRight() + 4),
                    juce::Justification::centredLeft, false);
    }
}

void ParamRow::mouseDown (const juce::MouseEvent& e)
{
    if (item.type == se::ItemType::CheckBox || item.type == se::ItemType::CheckBoxNoSave)
    {
        settings[item.key] = se::Value (! se::getBool (settings, item.key));
        repaint();
        if (onChanged) onChanged();
        return;
    }

    if (item.type == se::ItemType::RadioButton || item.type == se::ItemType::RadioButtonNoSave)
    {
        for (int i = 0; i < (int) item.options.size(); ++i)
        {
            if (optionBounds (i).contains (e.getPosition()))
            {
                settings[item.key] = se::Value ((int64_t) (i + 1));
                repaint();
                if (onChanged) onChanged();
                return;
            }
        }
        return;
    }

    if (item.type == se::ItemType::UnitsPopup)
    {
        juce::PopupMenu menu;
        menu.setLookAndFeel (&getLookAndFeel());
        for (int u = 0; u <= 5; ++u)
            menu.addItem (u + 1, unitName (static_cast<se::Unit> (u)), true,
                          se::getInt (settings, item.key) == u);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                            [this] (int choice)
        {
            if (choice <= 0) return;
            settings[item.key] = se::Value ((int64_t) (choice - 1));
            repaint();
            // The duration fields show bytes converted through this unit, so they
            // all have to be re-read — not just this control.
            if (onChanged) onChanged();
        });
    }
}

// ============================================================================
// EffectsPanelComponent
// ============================================================================

EffectsPanelComponent::EffectsPanelComponent (se::Engine& e)
    : engine (e), runner (e)
{
    setLookAndFeel (&lookAndFeel);

    viewport.setViewedComponent (&rowList, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    // The list takes focus so Return reaches this panel's keyPressed even when the
    // open form has no field to type into (Normalize, Reverse). Unhandled keys bubble
    // to the parents, so the canvas's own shortcuts are not swallowed by holding it.
    rowList.setWantsKeyboardFocus (true);

    rebuildRows();
}

EffectsPanelComponent::~EffectsPanelComponent()
{
    setLookAndFeel (nullptr);
}

const se::SoundDocument* EffectsPanelComponent::targetDoc() const
{
    if (targetId == 0) return nullptr;
    auto* w = const_cast<se::Engine&> (engine).windowById (targetId);
    return w != nullptr ? w->uiDoc.get() : nullptr;
}

void EffectsPanelComponent::setTargetWindow (uint64_t windowId)
{
    if (targetId == windowId) return;
    targetId = windowId;
    resultLine.clear();

    // The open form's values are per-module, not per-Window, so the form stays
    // open across a selection change — but a duration typed in milliseconds was
    // converted to bytes at the OLD document's rate, so the fields must be re-read.
    for (auto& p : params) p->refreshFromSettings();

    rebuildRows();
    updateScope();
    prefillSpanFields();
    repaint();
}

void EffectsPanelComponent::refresh()
{
    const se::SoundDocument* doc = targetDoc();

    // Only recompute when something the readout actually depends on has moved.
    const size_t selSt = doc ? doc->selSt : 0;
    const size_t selEnd = doc ? doc->selEnd : 0;
    const size_t maxSize = doc ? doc->maxTrackSize() : 0;
    const int firstTrack = doc ? doc->firstSelTrack : 0;
    const int lastTrack = doc ? doc->lastSelTrack : 0;
    const int numTracks = doc ? doc->numTracks() : 0;

    if (targetId == lastScopeWindow && selSt == lastSelSt && selEnd == lastSelEnd
        && maxSize == lastMaxSize && firstTrack == lastFirstTrack
        && lastTrack == lastLastTrack && numTracks == lastNumTracks)
        return;

    lastScopeWindow = targetId;
    lastSelSt = selSt;
    lastSelEnd = selEnd;
    lastMaxSize = maxSize;
    lastFirstTrack = firstTrack;
    lastLastTrack = lastTrack;
    lastNumTracks = numTracks;

    // A track added or removed can make a two-track module become applicable, so
    // the whole list's greying is re-derived, not just the scope line.
    rebuildRows();
    updateScope();
    prefillSpanFields();
    repaint();
}

void EffectsPanelComponent::updateScope()
{
    const se::SoundDocument* doc = targetDoc();

    if (doc == nullptr)
    {
        scopeLine = "No window selected";
        statusLine = "Click a sound window";
        return;
    }

    if (doc->maxTrackSize() == 0)
    {
        scopeLine = juce::String (doc->name);
        statusLine = "No audio in this window";
        return;
    }

    const se::Scope s = runner.scope (targetId);

    const int tracks = s.lastTrack - s.firstTrack + 1;
    juce::String where = s.wholeWindow ? "Whole window" : "Selection";

    juce::String detail;
    detail << tracks << (tracks == 1 ? " track" : " tracks");
    if (! s.wholeWindow && doc->numTracks() > 1)
        detail = "tracks " + juce::String (s.firstTrack) + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93"))
               + juce::String (s.lastTrack);

    scopeLine = where + juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  ")) + detail;
    statusLine = sayDuration (s.bytes, *doc) + " of audio";
}

// --- the list ---------------------------------------------------------------

void EffectsPanelComponent::rebuildRows()
{
    rows.clear();

    const se::EffectMenu menu = se::buildEffectMenu();

    const auto addEffect = [this] (const se::EffectModule* m)
    {
        Row r;
        r.kind = Row::Kind::Effect;
        r.module = m;
        r.label = m->name;
        r.applicability = runner.check (targetId, *m);
        rows.push_back (r);
    };

    for (const auto* m : menu.topLevel) addEffect (m);

    for (const auto& group : menu.groups)
    {
        Row header;
        header.kind = Row::Kind::GroupHeader;
        header.label = group.name;
        rows.push_back (header);
        for (const auto* m : group.modules) addEffect (m);
    }

    // The open module may have just become inapplicable — the Window was
    // deselected, a track was deleted, the audio was cleared. Apply has to follow:
    // a button that still looks pressable after its target went away is a button
    // that lies.
    if (openModule != nullptr)
    {
        openApplicability = runner.check (targetId, *openModule);
        if (applyButton != nullptr)
            applyButton->setEnabled (openApplicability == se::Applicability::Ok);
    }

    layoutRows();
}

int EffectsPanelComponent::formHeight() const
{
    if (openModule == nullptr) return 0;

    int h = kPad;
    for (const auto& p : params) h += p->getHeight();
    if (extendToggle != nullptr) h += 20;
    h += kButtonH + kPad * 2;

    // Room for the reason an inapplicable effect cannot run.
    if (openApplicability != se::Applicability::Ok) h += 16;

    return h;
}

void EffectsPanelComponent::layoutRows()
{
    int y = 0;
    for (auto& r : rows)
    {
        r.y = y;
        r.height = (r.kind == Row::Kind::GroupHeader) ? kGroupH : kRowH;
        y += r.height;

        if (r.kind == Row::Kind::Effect && r.module == openModule)
            y += formHeight();
    }

    const int width = std::max (1, viewport.getMaximumVisibleWidth());
    rowList.setSize (width, std::max (y, 1));

    // Position the open module's controls inside the gap just reserved.
    if (openModule == nullptr) return;

    for (const auto& r : rows)
    {
        if (r.kind != Row::Kind::Effect || r.module != openModule) continue;

        int fy = r.y + r.height + kPad;
        if (openApplicability != se::Applicability::Ok) fy += 16;

        for (auto& p : params)
        {
            p->setBounds (kPad * 2, fy, width - kPad * 4, p->getHeight());
            fy += p->getHeight();
        }

        if (extendToggle != nullptr)
        {
            extendToggle->setBounds (kPad * 2, fy, width - kPad * 4, 18);
            fy += 20;
        }

        if (applyButton != nullptr)
            applyButton->setBounds (width - kPad * 2 - 62, fy + kPad, 62, kButtonH);

        break;
    }
}

void EffectsPanelComponent::openEffect (const se::EffectModule* module)
{
    // Clicking the open effect closes it — the column is narrow, and a form you
    // cannot dismiss is a form that has taken the panel hostage.
    if (openModule == module) module = nullptr;

    openModule = module;
    resultLine.clear();
    params.clear();
    applyButton.reset();
    extendToggle.reset();
    working.clear();
    touchedSpanKeys.clear();

    if (openModule != nullptr)
    {
        working = openModule->defaults ? openModule->defaults() : se::Settings{};
        openApplicability = runner.check (targetId, *openModule);
        buildParamControls();
        prefillSpanFields();
    }

    layoutRows();
    rowList.repaint();
}

void EffectsPanelComponent::buildParamControls()
{
    const se::SoundDocument* doc = targetDoc();

    if (openModule->schema)
    {
        const se::SettingsSchema schema = openModule->schema();

        for (size_t i = 0; i < schema.size(); ++i)
        {
            const se::SchemaItem& item = schema[i];

            const int h = ParamRow::heightFor (item);
            if (h == 0) continue;   // an Ignore item with nothing to say

            // A DITL lays a field and its trailing UNIT out side by side — Gain's
            // is literally "Amplify by: [___] %" — so a short trailing Ignore
            // belongs on the field's line, not alone on the next one where it
            // reads as a stray character.
            //
            // But only if it fits. Dither's schema ends with "of random noise",
            // which is prose finishing a sentence, not a unit; jammed beside the
            // field it gets clipped to "of rando". Measuring rather than guessing
            // means the rule stays right whatever a module's schema says.
            // A heading titles what comes AFTER it, so it never joins the row
            // above however well it would fit — see SchemaItem::heading.
            if (item.type == se::ItemType::Ignore && ! item.heading && ! params.empty()
                && params.back()->canFitSuffix (item.label))
            {
                params.back()->setSuffix (item.label);
                continue;
            }

            auto row = std::make_unique<ParamRow> (item, working, doc);
            row->setSize (kMaxWidth, h);
            row->onChanged = [this]
            {
                // A units change rescales every duration field, so they all re-read.
                for (auto& p : params) p->refreshFromSettings();
                rowList.repaint();
            };
            row->onReturnPressed = [this] { applyFromKeyboard(); };
            if (item.scopeDefault)
                row->onUserEdit = [this] (const std::string& key) { touchedSpanKeys.insert (key); };
            rowList.addAndMakeVisible (*row);
            params.push_back (std::move (row));
        }
    }

    // "Ask Each Time" is one of the app's non-negotiables (§12), so a module that
    // can grow the sound asks before it does — here, in advance, rather than with a
    // modal interruption mid-run.
    if (openModule->requires.mayExtend)
    {
        extendToggle = std::make_unique<juce::ToggleButton> ("Allow the sound to get longer");
        extendToggle->setColour (juce::ToggleButton::textColourId, kInk);
        extendToggle->setColour (juce::ToggleButton::tickColourId, kInk);
        extendToggle->setColour (juce::ToggleButton::tickDisabledColourId, kInk);
        extendToggle->setToggleState (allowExtend, juce::dontSendNotification);
        extendToggle->onClick = [this] { allowExtend = extendToggle->getToggleState(); };
        rowList.addAndMakeVisible (*extendToggle);
    }

    applyButton = std::make_unique<Sys7Button> ("Apply");
    applyButton->onClick = [this] { applyEffect(); };
    applyButton->setEnabled (openApplicability == se::Applicability::Ok);
    rowList.addAndMakeVisible (*applyButton);
}

void EffectsPanelComponent::prefillSpanFields()
{
    if (openModule == nullptr) return;

    const se::SoundDocument* doc = targetDoc();
    if (doc == nullptr) return;

    // The selection's length when there is one, the longest track's when there is
    // not — the same span the scope readout reports, so the field and the readout
    // can never disagree. The kernels read 0 as "the whole selection" anyway;
    // this only makes the field SAY what Apply was already going to do.
    //
    // An empty Window has no span to derive, so the generators that can run there
    // (Waveform Generator, Draw Line — Requirements::worksOnEmpty) seed at
    // 1000 ms. The greyed effects show it too, harmlessly: their Apply is dead.
    const se::Scope s = runner.scope (targetId);
    auto span = (int64_t) (s.endByte - s.startByte);
    if (span <= 0)
        span = (int64_t) se::unitToBytes (1000.0, se::Unit::Milliseconds,
                                          doc->rateHz, doc->bps);

    for (auto& p : params)
    {
        const se::SchemaItem& item = p->schemaItem();
        if (! item.scopeDefault || touchedSpanKeys.count (item.key) > 0) continue;
        working[item.key] = se::Value (span);
        p->refreshFromSettings();
    }
}

void EffectsPanelComponent::applyEffect()
{
    if (openModule == nullptr) return;

    // Commit whatever is sitting in a field: pressing Apply with the caret still
    // in the box must use what you can SEE, not the last value that happened to be
    // committed. (Clicking Apply moves focus and commits anyway — but a value that
    // survives only by that accident is a value waiting to be lost.)
    for (auto& p : params) p->commitPendingText();

    const se::EffectRunner::Outcome outcome =
        runner.apply (targetId, *openModule, working, allowExtend);

    if (! outcome.ran)
    {
        resultLine = "Could not apply";
        repaint();
        return;
    }

    // Something the module said while running outranks the outcome: "it applied"
    // is true but useless next to Weird Tempo's own apology for the noise it just
    // introduced. (Properly this is a modal alert raised BEFORE the run, with a
    // Cancel button — that arrives with the alert layer.)
    if (! outcome.notices.empty())
    {
        resultLine = juce::String (outcome.notices.front());
        lastScopeWindow = 0;
        if (onDocumentChanged) onDocumentChanged (targetId);
        repaint();
        return;
    }

    // The module's own STR# 1000 voice wins when it has one (§7.7) — that is the
    // whole point of modules carrying their own error strings.
    if (! outcome.message.empty())
        resultLine = juce::String (outcome.message);
    else if (outcome.result == se::kModCancel)
        resultLine = juce::String (openModule->name) + ": nothing to do";
    else if (se::isError (outcome.result))
        resultLine = juce::String (openModule->name) + " failed";
    else
        resultLine = juce::String (openModule->name) + " applied";

    if (! outcome.spawnedWindows.empty())
        resultLine << "  (" << (int) outcome.spawnedWindows.size() << " new)";

    // Force the scope readout to recompute: the effect may have changed the
    // document's length, which is exactly what the readout reports.
    lastScopeWindow = 0;

    if (onDocumentChanged) onDocumentChanged (targetId);
    repaint();
}

bool EffectsPanelComponent::applyFromKeyboard()
{
    if (openModule == nullptr || openApplicability != se::Applicability::Ok)
        return false;

    applyEffect();
    return true;
}

bool EffectsPanelComponent::keyPressed (const juce::KeyPress& k)
{
    // Bare Return only: ⌘Return and friends belong to whatever else claims them.
    if (k.getKeyCode() == juce::KeyPress::returnKey
        && ! k.getModifiers().isAnyModifierKeyDown())
        return applyFromKeyboard();

    return false;
}

int EffectsPanelComponent::rowAt (int y) const
{
    for (int i = 0; i < (int) rows.size(); ++i)
        if (y >= rows[(size_t) i].y && y < rows[(size_t) i].y + rows[(size_t) i].height)
            return i;
    return -1;
}

// --- RowList ----------------------------------------------------------------

void EffectsPanelComponent::RowList::paint (Graphics& g)
{
    g.fillAll (kPanel);

    // The open effect's form, on hard white: the list is a shelf of 37 names,
    // and the one form standing open on it is where all the reading and typing
    // happens. White is what the fields are already drawn in, so the form is lit as
    // one surface rather than as pale boxes scattered over the shelf.
    if (owner.openModule != nullptr)
    {
        for (const auto& r : owner.rows)
        {
            if (r.kind != Row::Kind::Effect || r.module != owner.openModule) continue;

            const Rectangle<int> form (0, r.y + r.height, getWidth(), owner.formHeight());
            g.setColour (kWhite);
            g.fillRect (form);
            // Closed off at the bottom, the way every other band in this UI is —
            // otherwise the white runs into the next effect's name.
            g.setColour (kInk);
            g.drawHorizontalLine (form.getBottom() - 1, 0.0f, (float) getWidth());
            break;
        }
    }

    for (int i = 0; i < (int) owner.rows.size(); ++i)
    {
        const auto& r = owner.rows[(size_t) i];
        const Rectangle<int> bounds (0, r.y, getWidth(), r.height);

        if (r.kind == Row::Kind::GroupHeader)
        {
            g.setColour (kInk);
            g.drawHorizontalLine (r.y + r.height / 2, 6.0f, 10.0f);
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText (r.label, bounds.withTrimmedLeft (14), juce::Justification::centredLeft, false);
            continue;
        }

        const bool open = (r.module == owner.openModule);
        const bool usable = (r.applicability == se::Applicability::Ok);

        if (open)
        {
            g.setColour (kOpenFill);
            g.fillRect (bounds);
        }
        else if (i == hoverRow && usable)
        {
            g.setColour (kHoverFill);
            g.fillRect (bounds);
        }

        // A disclosure triangle, pointing down while the form is open.
        juce::Path tri;
        const float cx = 10.0f;
        const float cy = (float) bounds.getCentreY();
        if (open) tri.addTriangle (cx - 3.5f, cy - 2.0f, cx + 3.5f, cy - 2.0f, cx, cy + 3.0f);
        else      tri.addTriangle (cx - 2.0f, cy - 3.5f, cx - 2.0f, cy + 3.5f, cx + 3.0f, cy);

        g.setColour (open ? kWhite : (usable ? kInk : kDim));
        g.fillPath (tri);

        g.setFont (juce::FontOptions (11.0f));
        g.drawText (r.label, bounds.withTrimmedLeft (18), juce::Justification::centredLeft, false);
    }

    // The reason an open-but-inapplicable effect cannot run, in the gap reserved
    // for it — said where the person is already looking, not in an alert after
    // they press a button that does nothing.
    if (owner.openModule != nullptr && owner.openApplicability != se::Applicability::Ok)
    {
        for (const auto& r : owner.rows)
        {
            if (r.kind != Row::Kind::Effect || r.module != owner.openModule) continue;
            g.setColour (kDim);
            g.setFont (juce::FontOptions (10.0f));
            g.drawText (se::applicabilityText (owner.openApplicability, owner.openModule->requires),
                        Rectangle<int> (owner.kPad * 2, r.y + r.height + 2, getWidth(), 14),
                        juce::Justification::centredLeft, false);
            break;
        }
    }
}

void EffectsPanelComponent::RowList::mouseDown (const juce::MouseEvent& e)
{
    const int i = owner.rowAt (e.y);
    if (i < 0) return;

    const auto& r = owner.rows[(size_t) i];
    if (r.kind != Row::Kind::Effect) return;

    // Clicking a row is already a click into the panel — taking focus with it is what
    // makes Return land here rather than on whatever was focused before.
    grabKeyboardFocus();

    // An effect that cannot run on the current scope still opens, so its reason is
    // readable — it just cannot be applied.
    owner.openEffect (r.module);
}

void EffectsPanelComponent::RowList::mouseMove (const juce::MouseEvent& e)
{
    const int i = owner.rowAt (e.y);
    if (i == hoverRow) return;
    hoverRow = i;
    repaint();
}

void EffectsPanelComponent::RowList::mouseExit (const juce::MouseEvent&)
{
    if (hoverRow < 0) return;
    hoverRow = -1;
    repaint();
}

// --- panel chrome -----------------------------------------------------------

void EffectsPanelComponent::paint (Graphics& g)
{
    g.fillAll (kPanel);

    auto inner = getLocalBounds().reduced (1);

    auto header = inner.removeFromTop (kHeaderH);
    g.setColour (kInk);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("Effects", header.reduced (kPad, 0), juce::Justification::centredLeft, false);

    // How many of the 37 are actually here. Honest about the port's progress
    // rather than pretending the folder is full.
    g.setFont (juce::FontOptions (10.0f));
    g.setColour (kDim);
    g.drawText (juce::String ((int) se::effectRegistry().size()) + " of 37",
                header.reduced (kPad, 0), juce::Justification::centredRight, false);
    g.setColour (kInk);
    g.drawHorizontalLine (header.getBottom() - 1, (float) header.getX(), (float) header.getRight());

    // The scope readout: what Apply would eat. Apply is irreversible (§1), so this
    // sits above the list rather than being something you go looking for.
    auto scope = inner.removeFromTop (kScopeH);
    g.setColour (targetDoc() != nullptr ? kInk : kDim);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText (scopeLine, scope.reduced (kPad, 0).removeFromTop (14),
                juce::Justification::centredLeft, false);
    // The outcome of the last Apply displaces the derived status: having just done
    // something irreversible, what it did matters more than how much is selected.
    g.setColour (resultLine.isNotEmpty() ? kInk : kDim);
    g.setFont (juce::FontOptions (10.0f));
    // Ellipsised, unlike the scope line above it: a derived readout is short by
    // construction, but a module speaking in its own voice is not. Weird Tempo's
    // warning runs to 145 characters and a plain clip ends it mid-sentence, on a
    // word that reads like the end of one ("…which may introduce"). A visible "…"
    // is the difference between a truncated warning and a false one.
    g.drawText (resultLine.isNotEmpty() ? resultLine : statusLine,
                scope.reduced (kPad, 0).removeFromBottom (14),
                juce::Justification::centredLeft, true);
    g.setColour (kInk);
    g.drawHorizontalLine (scope.getBottom() - 1, (float) scope.getX(), (float) scope.getRight());

    g.drawRect (getLocalBounds(), 1);
}

void EffectsPanelComponent::resized()
{
    auto inner = getLocalBounds().reduced (1);
    inner.removeFromTop (kHeaderH);   // painted, no component
    inner.removeFromTop (kScopeH);
    viewport.setBounds (inner);
    layoutRows();
}
