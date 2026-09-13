// src/processing/algorithms/sar/sar_temporal_events.cpp — real acquisition-
// time semantics for multi-temporal SAR. Contract: sar_temporal_events.h.
#include "sar_temporal_events.h"

#include "sar_metadata.h"
#include "sar_temporal.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace sicnu::sar
{

namespace
{
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

/// Days from 1970-01-01 to the given civil date (Howard Hinnant's
/// days_from_civil, exact over the whole int range).
long long daysFromCivil( long long y, unsigned m, unsigned d )
{
    y -= m <= 2;
    const long long era = ( y >= 0 ? y : y - 399 ) / 400;
    const unsigned yoe = static_cast<unsigned>( y - era * 400 );
    const unsigned doy = ( 153 * ( m + ( m > 2 ? -3 : 9 ) ) + 2 ) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<long long>( doe ) - 719468;
}

bool parseUInt( const QString &text, int start, int len, int *out )
{
    if ( start + len > text.size() )
        return false;
    const QString part = text.mid( start, len );
    bool ok = false;
    const int v = part.toInt( &ok );
    if ( !ok )
        return false;
    *out = v;
    return true;
}
} // anonymous namespace

bool parseAcquisitionUtc( const QString &text, double *secondsSinceEpoch, QString *error )
{
    const QString t = text.trimmed();
    // Date part: YYYY-MM-DD (10 chars, strict separators).
    if ( t.size() < 10 || t[4] != QLatin1Char( '-' ) || t[7] != QLatin1Char( '-' ) )
    {
        if ( error )
            *error = QStringLiteral( "expected \"YYYY-MM-DD[THH:MM[:SS[.mmm]]]Z\", got \"%1\"" )
                         .arg( text );
        return false;
    }
    int year = 0;
    int month = 0;
    int day = 0;
    if ( !parseUInt( t, 0, 4, &year ) || !parseUInt( t, 5, 2, &month )
         || !parseUInt( t, 8, 2, &day ) || month < 1 || month > 12 || day < 1 || day > 31 )
    {
        if ( error )
            *error = QStringLiteral( "invalid calendar date in \"%1\"" ).arg( text );
        return false;
    }

    int hour = 0;
    int minute = 0;
    double second = 0.0;
    if ( t.size() > 10 )
    {
        if ( t[10] != QLatin1Char( 'T' ) )
        {
            if ( error )
                *error = QStringLiteral( "expected \"T\" between date and time in \"%1\"" )
                             .arg( text );
            return false;
        }
        QString rest = t.mid( 11 );
        if ( rest.endsWith( QLatin1Char( 'Z' ) ) )
            rest.chop( 1 );
        if ( rest.isEmpty() )
        {
            if ( error )
                *error = QStringLiteral( "empty time part in \"%1\"" ).arg( text );
            return false;
        }
        // HH:MM required; :SS[.mmm] optional.
        if ( rest.size() < 5 || rest[2] != QLatin1Char( ':' ) )
        {
            if ( error )
                *error = QStringLiteral( "invalid HH:MM in \"%1\"" ).arg( text );
            return false;
        }
        if ( !parseUInt( rest, 0, 2, &hour ) || !parseUInt( rest, 3, 2, &minute ) )
        {
            if ( error )
                *error = QStringLiteral( "invalid HH:MM in \"%1\"" ).arg( text );
            return false;
        }
        if ( rest.size() > 5 )
        {
            if ( rest[5] == QLatin1Char( '+' ) || rest[5] == QLatin1Char( '-' ) )
            {
                if ( error )
                    *error = QStringLiteral( "UTC-offset acquisitions like \"%1\" are outside "
                                             "the contract (UTC-only)" )
                                 .arg( text );
                return false;
            }
            if ( rest[5] != QLatin1Char( ':' ) || rest.size() < 8 )
            {
                if ( error )
                    *error = QStringLiteral( "expected \":SS\" in \"%1\"" ).arg( text );
                return false;
            }
            int secInt = 0;
            if ( !parseUInt( rest, 6, 2, &secInt ) )
            {
                if ( error )
                    *error = QStringLiteral( "invalid SS in \"%1\"" ).arg( text );
                return false;
            }
            second = secInt;
            if ( rest.size() > 8 )
            {
                if ( rest[8] != QLatin1Char( '.' ) || rest.size() < 10 )
                {
                    if ( error )
                        *error = QStringLiteral( "expected \".mmm\" fractional seconds in "
                                                 "\"%1\"" )
                                     .arg( text );
                    return false;
                }
                const QString fraction = rest.mid( 9 );
                bool digitsOnly = !fraction.isEmpty();
                for ( const QChar &c : fraction )
                    if ( !c.isDigit() )
                        digitsOnly = false;
                if ( !digitsOnly )
                {
                    if ( error )
                        *error = QStringLiteral( "invalid fractional seconds in \"%1\"" )
                                     .arg( text );
                    return false;
                }
                second += fraction.toDouble() / std::pow( 10.0, fraction.size() );
            }
        }
        if ( hour > 23 || minute > 59 || second >= 60.0 )
        {
            if ( error )
                *error = QStringLiteral( "time-of-day out of range in \"%1\"" ).arg( text );
            return false;
        }
    }

    const long long days = daysFromCivil( year, static_cast<unsigned>( month ),
                                          static_cast<unsigned>( day ) );
    const double seconds = static_cast<double>( days ) * 86400.0 + hour * 3600.0 + minute * 60.0
                           + second;
    if ( !std::isfinite( seconds ) )
    {
        if ( error )
            *error = QStringLiteral( "acquisition time out of range: \"%1\"" ).arg( text );
        return false;
    }
    *secondsSinceEpoch = seconds;
    return true;
}

double daysSince( double epochSeconds, double epochOfFirstScene )
{
    return ( epochSeconds - epochOfFirstScene ) / 86400.0;
}

bool sarTemporalEvents( const double *values, const double *dayOffsets, int n,
                        double changeThresholdDb, TemporalEventResult *out )
{
    if ( !values || !dayOffsets || !out || n <= 0 || !( changeThresholdDb >= 0.0 ) )
        return false;
    *out = TemporalEventResult{};

    std::vector<double> valid;
    valid.reserve( static_cast<size_t>( n ) );
    double minV = std::numeric_limits<double>::infinity();
    double maxV = -std::numeric_limits<double>::infinity();
    for ( int i = 0; i < n; ++i )
    {
        const double v = values[i];
        if ( !std::isfinite( v ) || !( v > 0.0 ) )
            continue; // missing acquisition / sentinel — drops out silently
        ++out->validCount;
        valid.push_back( v );
        if ( v < minV )
        {
            minV = v;
            out->argminIndex = i;
        }
        if ( v > maxV )
        {
            maxV = v;
            out->argmaxIndex = i;
        }
    }
    if ( out->validCount == 0 )
        return false;

    // Same upper-median selection as sar_temporal.h (shared convention).
    const double medianLinear = sarUpperMedianLinear( valid );
    out->baselineDb = linearToDb( medianLinear );

    double maxDev = 0.0;
    for ( int i = 0; i < n; ++i )
    {
        const double v = values[i];
        if ( !std::isfinite( v ) || !( v > 0.0 ) )
            continue;
        const double dev = std::fabs( linearToDb( v ) - out->baselineDb );
        maxDev = std::max( maxDev, dev );
        if ( dev >= changeThresholdDb )
        {
            ++out->eventCount;
            out->lastEventIndex = i;
            if ( out->firstEventIndex < 0 )
                out->firstEventIndex = i;
        }
    }
    out->maxDeviationDb = maxDev;
    out->event = out->eventCount > 0;
    out->firstEventDays = out->firstEventIndex >= 0 ? dayOffsets[out->firstEventIndex] : kNaN;
    out->lastEventDays = out->lastEventIndex >= 0 ? dayOffsets[out->lastEventIndex] : kNaN;
    out->argmaxDays = out->argmaxIndex >= 0 ? dayOffsets[out->argmaxIndex] : kNaN;
    out->argminDays = out->argminIndex >= 0 ? dayOffsets[out->argminIndex] : kNaN;
    return true;
}

} // namespace sicnu::sar
