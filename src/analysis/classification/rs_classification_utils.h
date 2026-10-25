// rs_classification_utils.h — ADR 0061: small helpers shared across the
// classification layer and its thin adapters. Each policy has exactly one
// owner; the operators must call here instead of re-implementing the
// formula / sampling policy.
#pragma once

#include <QColor>

#include "processing/algorithms/portable_random.h"

#include <algorithm>
#include <cstddef>
#include <random>
#include <vector>

/// Deterministic per-class color synthesis used by the pipeline and the
/// headless operators when ROI class defs are unavailable (Byte color
/// table + sidecar class metadata). Exact formula:
/// QColor::fromHsv( ( classId * 47 ) % 360, 200, 230 ).
inline QColor rsSynthesizedClassColor( int classId )
{
  return QColor::fromHsv( ( classId * 47 ) % 360, 200, 230 );
}

/// Deterministic subsampling policy shared by training-data extraction
/// (per-class cap) and the K-Means operator (global cap): shuffle with the
/// caller's engine and keep the first \a maxKeep items. maxKeep == 0 keeps
/// everything; sequences at or below the cap are left untouched (training
/// extraction preserves sorted bucket order). The shuffle is the portable
/// Fisher-Yates (#1685) — std::shuffle's permutation is
/// implementation-defined, so the kept subset would differ across STLs.
template <typename T>
void rsShuffleAndKeep( std::mt19937 &rng, std::vector<T> &items, std::size_t maxKeep )
{
  if ( maxKeep > 0 && items.size() > maxKeep )
  {
    rs::algorithms::portable::fisherYatesShuffle( rng, items );
    items.resize( maxKeep );
  }
}
