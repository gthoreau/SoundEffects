// Selection semantics for effects — SPEC §2.4, [SOURCE: Gain.c].
// Ported from the TS reference `src/selection.ts`.
//
// The per-track clamping lives in engine/Edit.h (`trackRange`), shared with the
// clipboard. What lives HERE is the one rule that belongs to effects rather than
// to editing: an empty selection means everything.
//
// That rule is also the effects panel's first UI rule, for free — "a Window is
// selected but nothing is selected inside it, so apply to all the audio in the
// Window" is not new logic to write, it is what a caret has always meant.

#pragma once

#include "engine/Edit.h"
#include "engine/SoundDocument.h"

namespace se {

/**
 * "Empty selection means everything." [SOURCE: Gain.c]
 *
 * Expands a caret to [0, maxSelectedTrackSize) — note SELECTED: the max over the
 * selected tracks, not the whole document. Asks without committing, since callers
 * often want to measure before they mutate.
 */
inline ByteRange resolveSelection (const SoundDocument& doc) noexcept
{
    if (doc.selSt == doc.selEnd) return { 0, doc.maxSelectedTrackSize() };
    return { doc.selSt, doc.selEnd };
}

/** Apply resolveSelection to the document, as `effect()` does in place — this is
    why running an effect on a caret leaves the whole sound selected afterwards. */
inline void expandEmptySelection (SoundDocument& doc) noexcept
{
    if (doc.selSt == doc.selEnd)
    {
        doc.selSt = 0;
        doc.selEnd = doc.maxSelectedTrackSize();
    }
}

} // namespace se
