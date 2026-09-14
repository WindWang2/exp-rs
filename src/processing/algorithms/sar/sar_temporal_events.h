// sar_temporal_events.h — multi-temporal SAR with real acquisition-time
// semantics (Advanced SAR / PolSAR / InSAR 10.0, package D; closes the S-1
// gap: temporal results carry CALENDAR DATES, not bare scene indices).
//
// Acquisition-time contract (additive dataset metadata, docs/processing/
// sar-domain.md §9): every scene declares
//   SICNU_SAR_ACQUISITION_UTC = "YYYY-MM-DD[THH:MM[:SS[.mmm]]]Z"
// (strict UTC; the explicit `dates` parameter array at the operator seam
// overrides the per-scene metadata). Indices into a declared series map to
// dates 1:1 by position; irregular revisit intervals are the NORMAL case —
// every time quantity below is computed on actual day offsets (floating
// days since the first scene), never on equal-interval assumptions
// (DECISIONS D-008).
//
// Domain rule (inherited from sar_temporal.h): samples are LINEAR POWER;
// valid = finite AND strictly positive; invalid samples (NaN sentinels,
// missing acquisitions) drop out of every statistic without changing the
// dates of the remaining ones.
//
// Event semantics: the per-pixel baseline is the median linear-power
// sample (upper-median for even counts — the SAME selection as
// sar_temporal.h, shared helper); a date is an EVENT when
// |10·log10(x) − 10·log10(median)| >= threshold dB. First/last event days
// give the "when did it change" answer the index-only band could not.
#pragma once

#include <QString>
#include <vector>

namespace sicnu::sar
{

/// Dataset metadata key declaring a scene's acquisition time (UTC ISO 8601).
inline const char *kAcquisitionUtcKey = "SICNU_SAR_ACQUISITION_UTC";

/// Parses the declared acquisition-time grammar (strict UTC). Returns
/// seconds since the UNIX epoch; false with a human-readable @a error
/// otherwise. Accepted: "YYYY-MM-DD", "YYYY-MM-DDTHH:MM",
/// "YYYY-MM-DDTHH:MM:SS", "…SS.mmm", each with optional trailing "Z"
/// (offsets like "+01:00" are refused — the contract is UTC-only).
bool parseAcquisitionUtc( const QString &text, double *secondsSinceEpoch, QString *error = nullptr );

/// Seconds → floating days since @a epochSeconds (scene 0's acquisition).
double daysSince( double epochSeconds, double epochOfFirstScene );

/// Per-pixel event analysis over one pixel's series.
/// @param values   LINEAR POWER samples (NaN/inf/nonpositive = invalid).
/// @param dayOffsets  floating day offset of every scene relative to scene 0
///                    (same length as @a values; ascending contract is the
///                    caller's typed refusal).
/// @param changeThresholdDb  event threshold (>= 0).
struct TemporalEventResult
{
    int validCount = 0;
    double baselineDb = 0.0;    ///< 10·log10(median linear) over valid samples
    double maxDeviationDb = 0.0;
    bool event = false;         ///< any date at/above the threshold
    int eventCount = 0;
    int firstEventIndex = -1;   ///< 0-based scene indices (−1 = none)
    int lastEventIndex = -1;
    double firstEventDays = 0.0; ///< days since scene 0 (NaN when no event)
    double lastEventDays = 0.0;
    int argmaxIndex = -1;       ///< first occurrence of the max valid sample
    int argminIndex = -1;       ///< first occurrence of the min valid sample
    double argmaxDays = 0.0;    ///< days since scene 0 (NaN when no valid)
    double argminDays = 0.0;
};
bool sarTemporalEvents( const double *values, const double *dayOffsets, int n,
                        double changeThresholdDb, TemporalEventResult *out );

} // namespace sicnu::sar
