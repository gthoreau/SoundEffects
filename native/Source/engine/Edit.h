// Edit — selection maths + destructive clipboard editing. Ported from the TS
// reference `src/selection.ts` and `src/edit.ts` (SPEC §2.4, §5).
//
// Everything here rewrites buffers in place. There is no undo and there must
// never be one (§1). Selection is a single rectangle: a contiguous Track range
// [firstSelTrack, lastSelTrack] × a time range [selSt, selEnd).
//
// Core weirdness preserved: Clear/Cut/Copy operate on the SELECTED tracks only,
// each with its own clamped-and-frame-snapped intersection — so editing an
// unequal-length region shortens only the tracks it touches and lets the rest
// drift out of alignment. Pure engine, no JUCE — testable.

#pragma once

#include "SoundDocument.h"

#include <cstddef>
#include <memory>
#include <string>

namespace se {

struct ByteRange { size_t start = 0; size_t end = 0; };

/** The range a given track actually processes: clamp to the track's own length
    first, then snap both ends down to a frame boundary. */
ByteRange trackRange (const SoundDocument& doc, int trackIndex, size_t selSt, size_t selEnd);

/** Set the selection time range, clamped to the longest track and frame-snapped.
    Swaps if inverted. Leaves the track range untouched. */
void setSelection (SoundDocument& doc, size_t start, size_t end);

/** Selection ▸ Set Loop — loop bounds follow the current selection, unclamped, a
    caret included (CODE 4 @0x1f10). */
void setLoop (SoundDocument& doc);

/** Selection ▸ Select Loop — the loop region becomes the selection, across every
    track. False (nothing done) on an empty sound (CODE 4 @0x1f62). */
bool selectLoop (SoundDocument& doc);

/** Selection ▸ Extend to Beginning / End — the selection's start goes to 0, or its
    end to the SELECTED tracks' longest end; the tracks stay. False when the selected
    tracks are empty (CODE 4 @0x584e / 0x592a). */
bool extendToBeginning (SoundDocument& doc);
bool extendToEnd (SoundDocument& doc);

/** Selection ▸ Extend Above / Below (0.4.0) — one more track above or below the
    selected ones, the time range kept. False at the first / last track. */
bool extendAbove (SoundDocument& doc);
bool extendBelow (SoundDocument& doc);

/** Edit ▸ Clear — remove the selection from the selected tracks (shortens them).
    A caret (empty selection) does nothing. Returns true if anything changed. */
bool clear (SoundDocument& doc);

/** Edit ▸ Copy — the selected tracks' clamped ranges as a new (playable) doc.
    Returns nullptr if nothing is selected. */
std::unique_ptr<SoundDocument> copy (const SoundDocument& doc);

/** Edit ▸ Cut — copy, then clear. */
std::unique_ptr<SoundDocument> cut (SoundDocument& doc);

/** Edit ▸ Paste — insert the clipboard's tracks at the caret, one clip track per
    document track from firstSelTrack; a non-empty selection is replaced first.
    Surplus clipboard tracks are dropped. Returns tracks that received audio. */
int paste (SoundDocument& doc, const SoundDocument& clip);

/** Edit ▸ Duplicate (0.4.0, ⌘D) — a copy of the selection inserted right after it,
    as Copy then Paste at the selection's end would, but leaving the clipboard alone.
    The copy ends up selected. A caret does nothing. Returns true if anything changed. */
bool duplicate (SoundDocument& doc);

/** Edit ▸ Crop — keep the selection, discard the rest (§5, `[INTERPRETED]`).
    Applied to the selected tracks only, consistent with Clear: unselected tracks
    keep their full length and drift out of alignment with the cropped ones.
    Afterwards the time selection is reset to everything. A caret does nothing.
    Returns true if anything changed. */
bool crop (SoundDocument& doc);

/** Edit ▸ Insert Silence — insert `bytes` of real silent samples at the caret in
    every selected track (grows those tracks). Silence IS audio, distinct from
    absence past a track's end. */
void insertSilence (SoundDocument& doc, size_t bytes);

/** Zero the selection in the selected tracks WITHOUT shortening them (turns the
    selected range into silence). */
void silenceSelection (SoundDocument& doc);

// --- Shift Sound… / Leave Silence… — disassembled (disassembly/Edit-Selection-Commands.md)

/** What Shift Sound… acts on. The dialog offers no choice: the selection decides. */
struct ShiftScope
{
    enum class Kind { sound, track, tracks, selection } kind = Kind::sound;
    int firstTrack = 1, lastTrack = 1;   // 1-based, inclusive
    size_t start = 0, end = 0;           // byte range, half-open
};

/** A caret (or a selection spanning the sound) with every track selected: the whole
    sound. Else a caret (or [0, longest selected track)): those tracks, to their
    longest end. Else the selection. */
ShiftScope shiftScope (const SoundDocument& doc);

/** The dialog's sentence (STR# 139): "Shift the whole sound by:", "Shift channel
    number 2 by:", "Shift channels 2 to 3 by:", "Shift the selection by:". */
std::string shiftCaption (const ShiftScope&);

/** Edit ▸ Shift Sound… — move the scope's range of each scoped track by `bytes`; the
    selection moves with it. Right copies it later (lengthening a track too short to
    take it) and silences the vacated head; Left first pads EVERY non-empty track with
    the shortfall if the range starts closer than `bytes` to the beginning, copies it
    earlier, then shortens a track the range ran to the end of or silences the
    vacated tail. False when `bytes` is less than a frame. */
bool shiftSound (SoundDocument& doc, size_t bytes, bool right);

/** Edit ▸ Leave Silence… — make the selected tracks' leading silence exactly `bytes`.
    `wholeSound` selects every track first. Tracks silent end to end become `bytes` of
    silence; the others all move by the same amount, set by the SHORTEST lead-in among
    them, so they stay aligned. The selection moves with the audio. */
void leaveSilence (SoundDocument& doc, size_t bytes, bool wholeSound);

// --- Select Exactly… / Select Peak… / Select Silence — disassembled -----------------

enum class ExactlyAnchor { before = 1, centered = 2, after = 3 };

/** Selection ▸ Select Exactly… (CODE 4 @0x2514): N bytes ending at the selection's
    start, around its midpoint, or starting at its end, clamped to 0 and to the
    SELECTED tracks' longest end without compensating; both ends frame-snapped. */
void selectExactly (SoundDocument& doc, size_t bytes, ExactlyAnchor);

/** Select Peak's opening choice: the whole sound iff the selection is a caret or
    spans the sound, with every track selected. */
bool peakScopeIsSound (const SoundDocument& doc);

/** Selection ▸ Select Peak… (Select-Silence-Peak.md, CODE 4 @0x2b90): the frame where
    |the sum of the selected tracks| peaks first, widened by `marginBytes` each side,
    each end clamped on its own. `wholeSound` searches every track over the whole sound. */
void selectPeak (SoundDocument& doc, size_t marginBytes, bool wholeSound);

/** Selection ▸ Select Silence (CODE 4 @0x61f8): from a caret, the interior of the run
    of exact digital zero around it, intersected across the selected tracks. True if
    the selection changed; a real selection does nothing. */
bool selectSilence (SoundDocument& doc);

} // namespace se
