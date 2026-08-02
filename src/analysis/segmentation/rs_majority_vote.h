// rs_majority_vote.h — ADR 0060: single owner of the pixel-majority
// tie-break rule.
//
// The rule "pick the id with the most votes; ties → smaller id" was previously
// re-implemented in rs_parent_link.cpp (P1 coarse-parent selection), in the
// rs:obia_hierarchy labelFromRoi helper, in the rs:obia_classify labeler, and
// inlined in tests. All sites now delegate to majorityKeyWithTieBreak, which
// is the only implementation of the decision kernel. The vote-collecting
// loops (pixel scans, rasterize masks, point-in-polygon) legitimately differ
// per site and stay where they are.
//
// Semantics:
//   - empty votes        → 0 (no majority; callers treat 0 as "no label")
//   - clear majority     → the id with the most votes
//   - tie                → the smaller id
//   - all-zero counts    → the first id encountered (defensive only; no
//                          caller produces zero counts)
#pragma once

#include <QHash>

/// Pick the vote key with the maximum count; ties → smaller key; empty → 0.
template <typename Key>
Key majorityKeyWithTieBreak( const QHash<Key, int> &votes )
{
    Key best = 0;
    int bestCount = 0;
    for ( auto it = votes.constBegin(); it != votes.constEnd(); ++it )
    {
        if ( it.value() > bestCount
             || ( it.value() == bestCount && ( best == 0 || it.key() < best ) ) )
        {
            bestCount = it.value();
            best = it.key();
        }
    }
    return best;
}
