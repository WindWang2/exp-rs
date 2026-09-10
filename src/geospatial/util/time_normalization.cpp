/***************************************************************************
  geospatial/util/time_normalization.cpp — ISO-8601/RFC 3339 instant parsing.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  Implementation notes:
  * pure string scanning + civil-days arithmetic (Howard Hinnant's days_from_
    civil) — no locale, no timezone database, no dynamic allocation in the
    hot path beyond the return value.
  * the parser is deliberately strict: it accepts the RFC 3339 core shape and
    documented lenient variants (space separator, compact offsets) and rejects
    everything else. A half-parsed datetime is a correctness hazard for any
    consumer that orders acquisitions.
 ***************************************************************************/

#include "geospatial/util/time_normalization.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace sicnu::geo
{

namespace
{

bool digitsToInt( const char *text, std::size_t count, std::int64_t &out )
{
  std::int64_t value = 0;
  for ( std::size_t i = 0; i < count; ++i )
  {
    const char c = text[i];
    if ( c < '0' || c > '9' )
      return false;
    value = value * 10 + ( c - '0' );
  }
  out = value;
  return true;
}

/// Days since 1970-01-01 for a civil date (proleptic Gregorian).
std::int64_t daysFromCivil( std::int64_t y, unsigned m, unsigned d )
{
  y -= m <= 2;
  const std::int64_t era = ( y >= 0 ? y : y - 399 ) / 400;
  const unsigned yoe = static_cast<unsigned>( y - era * 400 );            // [0, 399]
  const unsigned doy = ( 153 * ( m + ( m > 2 ? -3 : 9 ) ) + 2 ) / 5 + d - 1; // [0, 365]
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;             // [0, 146096]
  return era * 146097 + static_cast<std::int64_t>( doe ) - 719468;
}

bool isLeap( std::int64_t y ) { return ( y % 4 == 0 && y % 100 != 0 ) || y % 400 == 0; }

unsigned daysInMonth( std::int64_t y, unsigned m )
{
  static const unsigned days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  if ( m == 2 && isLeap( y ) )
    return 29;
  return days[m - 1];
}

} // namespace

InstantParse parseIso8601Instant( const std::string &text )
{
  InstantParse result;
  // Strict minimum: "YYYY-MM-DDTHH:MM:SS" (20 chars). Bound the scan by the
  // actual length — the year accepts exactly 4 digits (0000–9999 by contract).
  if ( text.size() < 20 )
    return result;

  std::int64_t year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
  const char *p = text.c_str();
  if ( !digitsToInt( p, 4, year ) || p[4] != '-' )
    return result;
  if ( !digitsToInt( p + 5, 2, month ) || p[7] != '-' )
    return result;
  if ( !digitsToInt( p + 8, 2, day ) )
    return result;
  const char sep = p[10];
  if ( sep != 'T' && sep != 't' && sep != ' ' )
    return result;
  if ( !digitsToInt( p + 11, 2, hour ) || p[13] != ':' )
    return result;
  if ( !digitsToInt( p + 14, 2, minute ) || p[16] != ':' )
    return result;
  if ( !digitsToInt( p + 17, 2, second ) )
    return result;

  if ( month < 1 || month > 12 || day < 1 || day > 31 )
    return result;
  if ( static_cast<unsigned>( day ) > daysInMonth( year, static_cast<unsigned>( month ) ) )
    return result;
  if ( hour > 23 || minute > 59 || second > 60 ) // 60 = leap second; clamped below
    return result;

  std::size_t i = 19;
  std::int64_t fractionalNanos = 0;
  if ( i < text.size() && text[i] == '.' )
  {
    const std::size_t start = ++i;
    std::int64_t digits = 0;
    while ( i < text.size() && std::isdigit( static_cast<unsigned char>( text[i] ) ) && digits < 9 )
    {
      fractionalNanos = fractionalNanos * 10 + ( text[i] - '0' );
      ++i;
      ++digits;
    }
    // Skip digits beyond nanosecond precision (they cannot change ordering at
    // our resolution) but they must be digits — anything else falls through
    // to the offset scan which will fail on a non-offset character.
    while ( i < text.size() && std::isdigit( static_cast<unsigned char>( text[i] ) ) )
      ++i;
    if ( digits == 0 )
      return result; // "." with no digits is not a fraction
    for ( std::int64_t pad = digits; pad < 9; ++pad )
      fractionalNanos *= 10;
  }

  std::int64_t offsetSeconds = 0;
  if ( i < text.size() && ( text[i] == 'Z' || text[i] == 'z' ) )
  {
    result.hadOffset = true;
    ++i;
  }
  else if ( i < text.size() && ( text[i] == '+' || text[i] == '-' ) )
  {
    result.hadOffset = true;
    const std::int64_t sign = text[i] == '-' ? -1 : 1;
    ++i;
    // Accept hh:mm, hhmm and hh.
    std::int64_t oh = 0, om = 0;
    if ( !digitsToInt( text.c_str() + i, 2, oh ) )
      return result;
    i += 2;
    if ( i < text.size() && text[i] == ':' )
      ++i;
    if ( i < text.size() && std::isdigit( static_cast<unsigned char>( text[i] ) ) )
    {
      if ( !digitsToInt( text.c_str() + i, 2, om ) )
        return result;
      i += 2;
    }
    if ( oh > 23 || om > 59 )
      return result;
    offsetSeconds = sign * ( oh * 3600 + om * 60 );
  }
  // Trailing garbage (a second "T", a timezone name, whitespace) is refused.
  if ( i != text.size() )
    return result;

  // Naive timestamps (no offset at all): STAC's documented reading is UTC —
  // applied, but flagged so freshness claims stay honest about the assumption.
  result.assumedUtc = !result.hadOffset;

  // Leap second (":60") carries the same epoch as ":59" — an instant must be
  // monotonic; the second is preserved in the canonical rendering only if we
  // carried it, which we deliberately do not (no consumer sorts leap seconds
  // differently, and epoch time has no representation for them).
  const std::int64_t clampedSecond = std::min<std::int64_t>( second, 59 );

  const std::int64_t days = daysFromCivil( year, static_cast<unsigned>( month ),
                                           static_cast<unsigned>( day ) );
  const std::int64_t secondsOfDay =
    hour * 3600 + minute * 60 + clampedSecond - offsetSeconds;
  result.epochNanos = ( days * 86400 + secondsOfDay ) * 1000000000 + fractionalNanos;
  result.ok = true;
  return result;
}

std::string instantToUtcString( std::int64_t epochNanos )
{
  std::int64_t seconds = epochNanos / 1000000000;
  std::int64_t nanos = epochNanos % 1000000000;
  if ( nanos < 0 )
  {
    nanos += 1000000000;
    seconds -= 1;
  }
  std::int64_t days = seconds / 86400;
  std::int64_t secondsOfDay = seconds % 86400;
  if ( secondsOfDay < 0 )
  {
    secondsOfDay += 86400;
    days -= 1;
  }
  // Civil-from-days (Howard Hinnant) — inverse of daysFromCivil.
  std::int64_t z = days + 719468;
  const std::int64_t era = ( z >= 0 ? z : z - 146096 ) / 146097;
  const std::int64_t doe = z - era * 146097;
  const std::int64_t yoe = ( doe - doe / 1460 + doe / 36524 - doe / 146096 ) / 365;
  const std::int64_t y = yoe + era * 400;
  const std::int64_t doy = doe - ( 365 * yoe + yoe / 4 - yoe / 100 );
  const std::int64_t mp = ( 5 * doy + 2 ) / 153;
  const std::int64_t d = doy - ( 153 * mp + 2 ) / 5 + 1;
  const std::int64_t m = mp + ( mp < 10 ? 3 : -9 );
  const std::int64_t year = y + ( m <= 2 );

  const std::int64_t hour = secondsOfDay / 3600;
  const std::int64_t minute = ( secondsOfDay % 3600 ) / 60;
  const std::int64_t second = secondsOfDay % 60;

  char buffer[64];
  int written = 0;
  if ( nanos == 0 )
  {
    written = std::snprintf( buffer, sizeof( buffer ), "%04lld-%02lld-%02lldT%02lld:%02lld:%02lldZ",
                             static_cast<long long>( year ), static_cast<long long>( m ),
                             static_cast<long long>( d ), static_cast<long long>( hour ),
                             static_cast<long long>( minute ), static_cast<long long>( second ) );
  }
  else
  {
    // 3/6/9 fractional digits (milli/micro/nano) — trailing zeros trimmed.
    long long frac = static_cast<long long>( nanos );
    int digits = 9;
    while ( digits > 3 && frac % 1000 == 0 )
    {
      frac /= 1000;
      digits -= 3;
    }
    written = std::snprintf( buffer, sizeof( buffer ), "%04lld-%02lld-%02lldT%02lld:%02lld:%02lld.%0*lldZ",
                             static_cast<long long>( year ), static_cast<long long>( m ),
                             static_cast<long long>( d ), static_cast<long long>( hour ),
                             static_cast<long long>( minute ), static_cast<long long>( second ),
                             digits, frac );
  }
  return written > 0 ? std::string( buffer, static_cast<std::size_t>( written ) ) : std::string();
}

} // namespace sicnu::geo
