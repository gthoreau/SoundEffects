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

namespace se {

struct ByteRange { size_t start = 0; size_t end = 0; };

/** The range a given track actually processes: clamp to the track's own length
    first, then snap both ends down to a frame boundary. */
ByteRange trackRange (const SoundDocument& doc, int trackIndex, size_t selSt, size_t selEnd);

/** Set the selection time range, clamped to the longest track and frame-snapped.
    Swaps if inverted. Leaves the track range untouched. */
void setSelection (SoundDocument& doc, size_t start, size_t end);

/** Selection ▸ Set Loop — loop bounds follow the current selection. */
void setLoop (SoundDocument& doc);

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

/** Edit ▸ Insert Silence — insert `bytes` of real silent samples at the caret in
    every selected track (grows those tracks). Silence IS audio, distinct from
    absence past a track's end. */
void insertSilence (SoundDocument& doc, size_t bytes);

/** Zero the selection in the selected tracks WITHOUT shortening them (turns the
    selected range into silence). */
void silenceSelection (SoundDocument& doc);

} // namespace se
