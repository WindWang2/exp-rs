// src/processing/algorithms/temporal/temporal_time.cpp
#include "temporal_time.h"

#include <QDate>
#include <QDateTime>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTimeZone>

namespace sicnu::temporal
{

namespace
{

constexpr qint64 kMillisPerDay = 24LL * 60 * 60 * 1000;

bool plausibleDate( int year, int month, int day )
{
  if ( year < 1970 || year > 2099 )
    return false;
  const QDate d( year, month, day );
  return d.isValid();
}

} // namespace

AcquisitionTime parseAcquisitionTime( const QString &text )
{
  AcquisitionTime t;
  const QString trimmed = text.trimmed();
  if ( trimmed.isEmpty() )
    return t;

  // Product metadata writes MODIS dates as "YYYY-DOYnnn" (stackToGeoTiff);
  // resolve to a calendar date so metadata-sourced times survive renames.
  {
    static const QRegularExpression doyRe(
      QStringLiteral( "^(\\d{4})-DOY(\\d{3})$" ), QRegularExpression::CaseInsensitiveOption );
    const auto m = doyRe.match( trimmed );
    if ( m.hasMatch() )
    {
      const QDate jan1( m.captured( 1 ).toInt(), 1, 1 );
      const int doy = m.captured( 2 ).toInt();
      if ( jan1.isValid() && doy >= 1 && doy <= 366 )
      {
        const QDate d = jan1.addDays( doy - 1 );
        if ( d.isValid() && d.year() == jan1.year() )
          return parseAcquisitionTime( d.toString( Qt::ISODate ) );
      }
      return t;
    }
  }

  // Pure date: day precision only.
  const QDate dateOnly = QDate::fromString( trimmed, Qt::ISODate );
  if ( dateOnly.isValid() && !trimmed.contains( QLatin1Char( 'T' ) ) )
  {
    t.precision = TimePrecision::Date;
    t.epochMillis = static_cast<qint64>( dateOnly.startOfDay( QTimeZone::utc() ).toMSecsSinceEpoch() );
    t.iso = dateOnly.toString( Qt::ISODate );
    t.valid = true;
    return t;
  }

  QDateTime dt = QDateTime::fromString( trimmed, Qt::ISODateWithMs );
  if ( !dt.isValid() )
    dt = QDateTime::fromString( trimmed, Qt::ISODate );
  if ( dt.isValid() )
  {
    if ( dt.timeSpec() == Qt::LocalTime )
    {
      // A timezone-less string is ambiguous; toUTC() would subtract the HOST
      // offset and make results machine-dependent. Reinterpret the wall time
      // as UTC instead (deterministic everywhere).
      dt = QDateTime( dt.date(), dt.time(), QTimeZone::utc() );
    }
    t.precision = TimePrecision::DateTime;
    t.epochMillis = dt.toMSecsSinceEpoch();
    t.iso = dt.toUTC().toString( Qt::ISODate );
    t.valid = true;
    return t;
  }
  return t;
}

AcquisitionTime makeDateTime( int year, int month, int day,
                              int hour, int minute, int second )
{
  AcquisitionTime t;
  const QDateTime dt( QDate( year, month, day ), QTime( hour, minute, second ), QTimeZone::utc() );
  if ( !dt.isValid() )
    return t;
  t.precision = TimePrecision::DateTime;
  t.epochMillis = dt.toMSecsSinceEpoch();
  t.iso = dt.toUTC().toString( Qt::ISODate );
  t.valid = true;
  return t;
}

double AcquisitionTime::daysSince( const AcquisitionTime &reference ) const
{
  if ( !valid || !reference.valid )
    return 0.0;
  return static_cast<double>( epochMillis - reference.epochMillis ) /
         static_cast<double>( kMillisPerDay );
}

QString AcquisitionTime::dateString() const
{
  if ( !valid )
    return {};
  return QDateTime::fromMSecsSinceEpoch( epochMillis, QTimeZone::utc() ).date().toString( Qt::ISODate );
}

AcquisitionTime timeFromFilename( const QString &path )
{
  AcquisitionTime t;
  const QString name = QFileInfo( path ).fileName();

  // 1. Sentinel-2 product id fragment: _YYYYMMDDTHHMMSS
  {
    static const QRegularExpression re( QStringLiteral( "_(\\d{8})T(\\d{6})" ) );
    const auto m = re.match( name );
    if ( m.hasMatch() )
    {
      const int y = m.captured( 1 ).mid( 0, 4 ).toInt();
      const int mo = m.captured( 1 ).mid( 4, 2 ).toInt();
      const int d = m.captured( 1 ).mid( 6, 2 ).toInt();
      const int hh = m.captured( 2 ).mid( 0, 2 ).toInt();
      const int mi = m.captured( 2 ).mid( 2, 2 ).toInt();
      const int ss = m.captured( 2 ).mid( 4, 2 ).toInt();
      if ( plausibleDate( y, mo, d ) )
        return makeDateTime( y, mo, d, hh, mi, ss );
    }
  }

  // 2. Landsat Collection scene id: L?##_LLLL_PPPRRR_YYYYMMDD_...
  {
    static const QRegularExpression re(
      QStringLiteral( "^L[A-Z]\\d{2}_[A-Z0-9]+_\\d{6}_(\\d{8})_" ) );
    const auto m = re.match( name );
    if ( m.hasMatch() )
    {
      const int y = m.captured( 1 ).mid( 0, 4 ).toInt();
      const int mo = m.captured( 1 ).mid( 4, 2 ).toInt();
      const int d = m.captured( 1 ).mid( 6, 2 ).toInt();
      if ( plausibleDate( y, mo, d ) )
        return parseAcquisitionTime( m.captured( 1 ).insert( 4, QLatin1Char( '-' ) )
                                         .insert( 7, QLatin1Char( '-' ) ) );
    }
  }

  // 3. MODIS file date code: .AYYYYDDD.
  {
    static const QRegularExpression re( QStringLiteral( "\\.A(\\d{4})(\\d{3})\\." ) );
    const auto m = re.match( name );
    if ( m.hasMatch() )
    {
      const int year = m.captured( 1 ).toInt();
      const int doy = m.captured( 2 ).toInt();
      const QDate jan1( year, 1, 1 );
      if ( jan1.isValid() && doy >= 1 && doy <= 366 )
      {
        const QDate d = jan1.addDays( doy - 1 );
        if ( d.isValid() && d.year() == year )
          return parseAcquisitionTime( d.toString( Qt::ISODate ) );
      }
    }
  }

  // 4. Bare YYYYMMDD token that parses to a plausible date.
  {
    static const QRegularExpression re( QStringLiteral( "(?<!\\d)(\\d{8})(?!\\d)" ) );
    auto it = re.globalMatch( name );
    while ( it.hasNext() )
    {
      const auto m = it.next();
      const int y = m.captured( 1 ).mid( 0, 4 ).toInt();
      const int mo = m.captured( 1 ).mid( 4, 2 ).toInt();
      const int d = m.captured( 1 ).mid( 6, 2 ).toInt();
      if ( plausibleDate( y, mo, d ) )
        return parseAcquisitionTime( m.captured( 1 ).insert( 4, QLatin1Char( '-' ) )
                                         .insert( 7, QLatin1Char( '-' ) ) );
    }
  }

  return t;
}

} // namespace sicnu::temporal
