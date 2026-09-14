// src/processing/algorithms/temporal/temporal_calendar.h
// Time normalization: irregular acquisition instants -> a regular calendar
// grid (Temporal Platform 10.0, closes the T-2 gap: gap filling alone cannot
// re-cast a series onto a configured cadence).
//
// Contract (mirrors temporal_gapfill.h / temporal_fit.h):
//   - Calendar points are REAL day offsets from the collection reference
//     epoch (never array indices); the grid is generated from the first to
//     the last valid acquisition instant, inclusive.
//   - A sample is valid iff finite (the TemporalTileReader already collapsed
//     NoData / QA-masked / non-finite values to NaN before this kernel); NaN
//     observations are transparent — anchors and windows skip them.
//   - Calendar points BEFORE the first / AFTER the last finite observation
//     are never produced: every method refuses them (NaN), a carried-forward
//     gap-fill contract, not a guessing heuristic.
//   - Deterministic: fixed evaluation order, explicit tie-breaks (ties prefer
//     the earlier observation). Bit-exact for nearest / window_mean / linear;
//     whittaker is tolerance-grade (banded solve, documented 1e-4) like
//     whittakerSmooth.
#pragma once

#include <QString>

#include <cstddef>
#include <vector>

namespace sicnu::temporal
{

/// Calendar point: real day offset from the series epoch plus the ISO date
/// the operator surfaces in band names / provenance.
struct CalendarPoint
{
  double tDays = 0.0;
  QString isoDate;
};

enum class CalendarCadence
{
  Days,    ///< anchor + k * cadenceDays
  Monthly  ///< calendar months from the anchor (day-of-month clamped)
};

struct CalendarSpec
{
  CalendarCadence cadence = CalendarCadence::Days;
  int cadenceDays = 16;  ///< Days cadence only; > 0
};

/// One regularized output position.
struct RegularizedPoint
{
  float value = 0.0f;         ///< NaN where unfilled / refused
  int validObservations = 0;  ///< observations that contributed to the value:
                              ///  nearest = 1; window_mean/linear = the count
                              ///  used; whittaker = the count MAPPED to the
                              ///  grid node (0 = penalty-defined value only).
                              ///  0 also marks unfilled/refused positions.
  bool filled = false;        ///< true when the value is synthesized (no
                              ///  observation AT the calendar point)
};

struct RegularizedSeries
{
  std::vector<RegularizedPoint> points;
  int filledCount = 0;   ///< points with filled == true and a finite value
  int validCount = 0;    ///< points with a finite value (observed or filled)
};

/// Generates the calendar grid covering the day-offset range
/// [@a rangeStartDays, @a rangeEndDays] (inclusive, offsets from the series
/// epoch @a epochIsoDate — the collection reference instant, t = 0).
/// Days cadence: epoch + k·cadence stepping from the first grid point
/// >= rangeStartDays to the last <= rangeEndDays. Monthly cadence: calendar
/// months stepped from the epoch month, day-of-month clamped into the target
/// month. Returns an empty vector for an inverted or non-finite range or
/// cadenceDays <= 0 or an unparsable epoch.
std::vector<CalendarPoint> buildRegularCalendar( const QString &epochIsoDate,
                                                 double rangeStartDays, double rangeEndDays,
                                                 const CalendarSpec &spec );

enum class RegularizeMethod
{
  Nearest,     ///< closest observation within maxWindowDays (tie -> earlier)
  WindowMean,  ///< mean of all observations within +- maxWindowDays
  Linear,      ///< time-weighted interpolation between bracketing observations
  Whittaker    ///< penalized fit defined ON the calendar grid (see below)
};

/// Options for regularizeSeries. maxGapNodes applies to Whittaker only: the
/// longest run of consecutive observation-free grid nodes the penalty may
/// bridge; longer runs split the solve (each side stays inside its observed
/// span — never extrapolated).
struct RegularizeOptions
{
  RegularizeMethod method = RegularizeMethod::Linear;
  double maxWindowDays = 32.0;   ///< Nearest / WindowMean membership radius
  double lambda = 10.0;          ///< Whittaker smoothness penalty (> 0)
  int maxGapNodes = 6;           ///< Whittaker bridging guard (>= 1)
};

/// Re-casts one irregular series (NaN = missing, @a tDays ascending, length
/// @a nObs) onto @a calendar. @a calendar must be ascending and non-empty.
/// Output length == calendar size; every point carries its valid-observation
/// count and synthetic-value flag (provenance of filled values).
RegularizedSeries regularizeSeries( const float *series, int nObs,
                                    const double *tDays,
                                    const std::vector<CalendarPoint> &calendar,
                                    const RegularizeOptions &options );

/// Vector convenience wrapper.
RegularizedSeries regularizeSeries( const std::vector<float> &series,
                                    const std::vector<double> &tDays,
                                    const std::vector<CalendarPoint> &calendar,
                                    const RegularizeOptions &options );

/// Parses "16d" (CalendarCadence::Days, 16) / "month" | "monthly".
/// Returns false on anything else (operator turns that into InvalidParameter).
bool parseCadenceToken( const QString &token, CalendarSpec *spec );

} // namespace sicnu::temporal
