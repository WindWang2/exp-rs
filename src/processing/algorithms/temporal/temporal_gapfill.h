// temporal_gapfill.h — per-pixel time-series gap filling (Temporal 7.0).
//
// Pure kernel extracted from rs_temporal_gap_fill_operator (which used to
// inline the interpolation loop, untestable in isolation — the operator now
// only orchestrates streaming and delegates the math here).
//
// Contract (NaN = missing, real day offsets — never array indices):
//   - finite samples copy through unchanged;
//   - a gap is anchored by the nearest finite sample on each side
//     (l < s < r in time order);
//   - Linear: needs BOTH anchors and (t[r] − t[l]) <= maxGapDays; the fill
//     is the time-weighted interpolation; duplicate instants (t[l] == t[r])
//     average the two anchors instead of a 0/0 weight. One-sided gaps stay
//     NaN — linear never extrapolates;
//   - Nearest: the closer anchor wins (tie → earlier scene), subject to
//     distance <= maxGapDays; one-sided gaps use their single anchor;
//   - maxGapDays < 0 is rejected up front (operator validates; the kernel
//     treats it as "fill nothing").
// Deterministic, single-threaded, bit-stable for a fixed input.
#pragma once

#include <cstddef>
#include <vector>

namespace sicnu::temporal
{

enum class GapFillMethod
{
    Linear,
    Nearest,
};

/// Per-series fill accounting (what the operator reports as filledFraction).
struct GapFillCounts
{
    int filled = 0;    ///< NaN positions this call wrote a finite value into
    int fillable = 0;  ///< NaN positions with at least one anchor on either
                       ///< side (regardless of maxGapDays — one-anchored gaps
                       ///< count as fillable even when unfilled)
};

/// Fills NaN gaps of one series against real time offsets @a tDays (days,
/// ascending; length @a T). Single-pass semantics: every gap is anchored on
/// the ORIGINAL observations — a freshly filled neighbour is never an anchor
/// (in-place aliasing @a out == @a series is supported and preserves this).
/// @a outCounts optional; when non-null it is reset here.
void gapFillSeries( const float *series, int T, const double *tDays,
                    GapFillMethod method, double maxGapDays,
                    float *out, GapFillCounts *outCounts = nullptr );

/// Vector convenience wrapper (NaN = missing in, NaN = unfilled gap out).
std::vector<float> gapFillSeries( const std::vector<float> &series,
                                  const std::vector<double> &tDays,
                                  GapFillMethod method, double maxGapDays );

} // namespace sicnu::temporal
