// src/processing/algorithms/temporal/temporal_time.h
// Acquisition-time semantics for temporal remote sensing (ADR: time is a
// first-class citizen, not a filename list).
//
// Scientific contract:
//  * Acquisition times are ISO 8601. When only a date is known ("2026-08-01")
//    the precision is Date and the value is NEVER silently re-interpreted as
//    an overpass instant — date precision contributes day-resolution time
//    intervals only.
//  * Sorting and duplicate detection use the UTC instant; ties are broken by
//    the original input index so ordering is fully deterministic.
#pragma once

#include <QString>

#include <cstdint>

namespace sicnu::temporal
{

enum class TimePrecision
{
  Date,     ///< calendar date only (day-resolution intervals)
  DateTime  ///< full UTC instant
};

/// A parsed acquisition time with explicit precision.
struct AcquisitionTime
{
  QString iso;                    ///< canonical text as provided/parsed
  TimePrecision precision = TimePrecision::Date;
  qint64 epochMillis = 0;         ///< UTC instant; Date == midnight UTC (ordering only)
  bool valid = false;             ///< false when no time could be determined

  /// Whole days (fractional) between this time and @a reference
  /// (this − reference), computed from real UTC instants — never from array
  /// index positions.
  double daysSince( const AcquisitionTime &reference ) const;

  /// "YYYY-MM-DD" form (invalid time -> empty string).
  QString dateString() const;

  bool operator==( const AcquisitionTime &other ) const
  {
    return valid == other.valid && epochMillis == other.epochMillis;
  }
};

/// Parses an ISO-8601 date ("YYYY-MM-DD") or datetime
/// ("YYYY-MM-DD[THH:mm[:ss]][Z|±HH:mm]") into an AcquisitionTime. Invalid
/// input yields valid == false (never a guessed time).
AcquisitionTime parseAcquisitionTime( const QString &text );

/// Builds a Date-precision time from an explicit QDate-like triple.
AcquisitionTime makeDateTime( int year, int month, int day,
                              int hour = 0, int minute = 0, int second = 0 );

/// Attempts to derive an acquisition time from a product/filename.
/// Recognized (checked in order, deliberately conservative — no guessing):
///   1. Sentinel-2 product id: "_YYYYMMDDTHHMMSS"      -> DateTime (UTC)
///   2. Landsat scene id:      L?##_*_PPPRRR_YYYYMMDD_ -> Date
///   3. MODIS file code:       ".AYYYYDDD."            -> Date (DOY resolved)
///   4. bare "YYYYMMDD" token  (must parse to a real date in 1970..2099)
/// Returns valid == false when nothing matches.
AcquisitionTime timeFromFilename( const QString &path );

} // namespace sicnu::temporal
