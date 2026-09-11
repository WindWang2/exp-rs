/***************************************************************************
  geospatial/util/time_normalization.h
  Cloud-Native Geospatial Data Fabric 8.0 — Qt-free ISO-8601/RFC 3339 instant
  normalization (STAC task D).
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  Contract:
  * parses the datetime shapes STAC and EO metadata actually carry:
    "2021-09-03T13:42:12Z", "...+02:00", "...+0200", "...+02", fractional
    seconds (any 1–9 digits), and naive timestamps WITHOUT an offset
    (STAC treats offset-less datetimes as UTC — reported, never silent).
  * a parsed instant is a comparable epoch-nanosecond count plus a canonical
    UTC rendering ("YYYY-MM-DDTHH:MM:SS[.fff]Z") — ordering by the parsed
    instant is exact across mixed offsets; ordering by raw strings is not.
  * total function: unparseable input is `ok = false`, never a throw and
    never a guessed time.
  * bounded by design: no locale, no system clock reads. Instants are
    epoch nanoseconds (int64), so dates outside ≈1678–2262 are REFUSED
    (ok = false) rather than wrapped — the alternative is silently wrong
    ordering. The 4-digit-year syntax still accepts 0000–9999; only the
    representable subset parses.
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_TIME_NORMALIZATION_H
#define SICNU_GEOSPATIAL_TIME_NORMALIZATION_H

#include "geospatial/common.h"

#include <cstdint>
#include <string>

namespace sicnu::geo
{

/// Result of parsing one ISO-8601 / RFC 3339 datetime string.
struct InstantParse
{
    bool ok = false;
    /// Epoch nanoseconds, UTC (valid only when ok).
    std::int64_t epochNanos = 0;
    /// True when the source carried no offset and UTC was ASSUMED (the STAC
    /// spec's documented reading) — freshness claims must not dress this up
    /// as declared.
    bool assumedUtc = false;
    /// True when the source declared an explicit offset ("Z" or ±hh:mm…).
    bool hadOffset = false;
};

/// Parses an ISO-8601/RFC 3339 datetime ("YYYY-MM-DDTHH:MM:SS[.f…][Z|±hh:mm|±hhmm|±hh]").
/// A space may separate date and time (a common metadata shape). A date-only
/// string ("2021-09-03") is NOT an instant — STAC mandates full datetimes;
/// returns ok=false so callers surface the violation instead of guessing.
InstantParse parseIso8601Instant( const std::string &text );

/// Canonical UTC rendering ("YYYY-MM-DDTHH:MM:SSZ"; fractional seconds appear
/// only when nonzero, at 3/6/9 digits — trailing zeros trimmed). Inverse of
/// parseIso8601Instant on ok results (formatting lossless to the nanosecond).
std::string instantToUtcString( std::int64_t epochNanos );

} // namespace sicnu::geo

#endif // SICNU_GEOSPATIAL_TIME_NORMALIZATION_H
