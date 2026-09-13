// src/processing/algorithms/temporal/temporal_calendar.cpp
#include "temporal_calendar.h"

#include "temporal_fit.h"

#include <QDate>

#include <algorithm>
#include <cmath>
#include <limits>

namespace sicnu::temporal
{

namespace
{
constexpr float kNan = std::numeric_limits<float>::quiet_NaN();

/// Index of the last observation at or before @a t (-1 when none).
int lowerBoundObs( const double *tDays, int nObs, double t )
{
  int lo = 0;
  int hi = nObs; // first index with tDays[idx] > t
  while ( lo < hi )
  {
    const int mid = lo + ( hi - lo ) / 2;
    if ( tDays[mid] <= t )
      lo = mid + 1;
    else
      hi = mid;
  }
  return lo - 1;
}
} // namespace

std::vector<CalendarPoint> buildRegularCalendar( const QString &epochIsoDate,
                                                 double rangeStartDays, double rangeEndDays,
                                                 const CalendarSpec &spec )
{
  std::vector<CalendarPoint> out;
  if ( !std::isfinite( rangeStartDays ) || !std::isfinite( rangeEndDays ) ||
       rangeEndDays < rangeStartDays )
    return out;
  if ( spec.cadence == CalendarCadence::Days && spec.cadenceDays <= 0 )
    return out;

  const QDate epoch = QDate::fromString( epochIsoDate, Qt::ISODate );
  if ( !epoch.isValid() )
    return out;

  if ( spec.cadence == CalendarCadence::Days )
  {
    // First grid point >= rangeStart: k = ceil((rangeStart - 0)/cadence) with
    // grid anchored at the epoch (t = 0 = epoch). A negative range start
    // steps backwards symmetrically (anchor + k·cadence, k any integer).
    const long long kStart = static_cast<long long>( std::ceil( rangeStartDays / spec.cadenceDays ) );
    const long long kEnd = static_cast<long long>( std::floor( rangeEndDays / spec.cadenceDays ) );
    out.reserve( static_cast<size_t>( std::max<long long>( 0, kEnd - kStart + 1 ) ) );
    for ( long long k = kStart; k <= kEnd; ++k )
    {
      const double t = static_cast<double>( k ) * spec.cadenceDays;
      const QDate date = epoch.addDays( static_cast<qint64>( std::llround( t ) ) );
      if ( !date.isValid() )
        continue;
      out.push_back( { t, date.toString( Qt::ISODate ) } );
    }
    return out;
  }

  // Monthly cadence: grid point k sits in month epoch.month()+k at the
  // epoch's day-of-month clamped into that month (Jan 31 -> Feb 28). t(k) is
  // strictly increasing in k, so scanning a day-derived k window plus a
  // 2-month guard on each side covers the whole range exactly.
  auto monthlyDate = [&]( long long k ) -> QDate {
    const long long total = static_cast<long long>( epoch.year() ) * 12 +
                            ( epoch.month() - 1 ) + k;
    const int y = static_cast<int>( total / 12 );
    const int m = static_cast<int>( total % 12 ) + 1;
    const int day = std::min( epoch.day(), QDate( y, m, 1 ).daysInMonth() );
    return QDate( y, m, day );
  };
  constexpr double kMeanDaysPerMonth = 30.4375;
  const long long kStart = static_cast<long long>(
                             std::floor( rangeStartDays / kMeanDaysPerMonth ) ) - 2;
  const long long kEnd = static_cast<long long>(
                           std::ceil( rangeEndDays / kMeanDaysPerMonth ) ) + 2;
  out.reserve( static_cast<size_t>( std::max<long long>( 0, kEnd - kStart + 1 ) ) );
  for ( long long k = kStart; k <= kEnd; ++k )
  {
    const QDate date = monthlyDate( k );
    if ( !date.isValid() )
      continue;
    const double t = static_cast<double>( epoch.daysTo( date ) );
    if ( t < rangeStartDays || t > rangeEndDays )
      continue;
    out.push_back( { t, date.toString( Qt::ISODate ) } );
  }
  return out;
}

RegularizedSeries regularizeSeries( const float *series, int nObs,
                                    const double *tDays,
                                    const std::vector<CalendarPoint> &calendar,
                                    const RegularizeOptions &options )
{
  RegularizedSeries out;
  const int nCal = static_cast<int>( calendar.size() );
  out.points.assign( static_cast<size_t>( nCal ), RegularizedPoint{} );
  if ( nCal == 0 || nObs <= 0 || series == nullptr || tDays == nullptr )
    return out;

  if ( options.method == RegularizeMethod::Whittaker )
  {
    // Penalized fit defined ON the calendar grid (D3): observations map to
    // their nearest grid node; unobserved nodes carry weight 0. Runs of more
    // than maxGapNodes observation-free nodes split the solve; points
    // outside an observed span stay NaN (no extrapolation).
    std::vector<float> gridY( static_cast<size_t>( nCal ), kNan );
    std::vector<int> observedCount( static_cast<size_t>( nCal ), 0 );
    int firstObsNode = -1;
    int lastObsNode = -1;
    for ( int i = 0; i < nObs; ++i )
    {
      if ( !std::isfinite( series[i] ) )
        continue;
      // Nearest node (tie -> earlier node); calendar is ascending.
      const auto it = std::lower_bound(
        calendar.begin(), calendar.end(), tDays[i],
        []( const CalendarPoint &p, double t ) { return p.tDays < t; } );
      int node;
      if ( it == calendar.end() )
        node = nCal - 1;
      else if ( it->tDays == tDays[i] || it == calendar.begin() )
        node = static_cast<int>( it - calendar.begin() );
      else
      {
        const int prev = static_cast<int>( it - calendar.begin() ) - 1;
        node = ( tDays[i] - calendar[static_cast<size_t>( prev )].tDays <=
                 it->tDays - tDays[i] )
                 ? prev
                 : static_cast<int>( it - calendar.begin() );
      }
      // Multiple observations on one node average (deterministic order).
      if ( !std::isfinite( gridY[static_cast<size_t>( node )] ) )
        gridY[static_cast<size_t>( node )] = series[i];
      else
        gridY[static_cast<size_t>( node )] =
          0.5f * ( gridY[static_cast<size_t>( node )] + series[i] );
      ++observedCount[static_cast<size_t>( node )];
      if ( firstObsNode < 0 )
        firstObsNode = node;
      lastObsNode = node;
    }
    if ( firstObsNode < 0 )
      return out;

    // Split at runs of unobserved nodes longer than maxGapNodes.
    const int maxGap = std::max( 1, options.maxGapNodes );
    auto observed = [&]( int node ) {
      return std::isfinite( gridY[static_cast<size_t>( node )] );
    };
    // Per-segment Whittaker via the shared banded solver (temporal_fit.h).
    auto solveSegment = [&]( int lo, int hi ) {
      const int len = hi - lo + 1;
      if ( len <= 0 )
        return;
      std::vector<float> seg( static_cast<size_t>( len ), kNan );
      for ( int i = 0; i < len; ++i )
        seg[static_cast<size_t>( i )] = gridY[static_cast<size_t>( lo + i )];
      const std::vector<float> smoothed =
        whittakerSmooth( seg, {}, options.lambda > 0.0 ? options.lambda : 10.0 );
      for ( int i = 0; i < len; ++i )
      {
        const int node = lo + i;
        if ( node < firstObsNode || node > lastObsNode )
          continue; // outside the observed span of the whole grid: refuse
        const float v = smoothed[static_cast<size_t>( i )];
        auto &dst = out.points[static_cast<size_t>( node )];
        dst.value = v;
        // Truthful accounting: the count of observations MAPPED to this
        // node (0 for purely penalty-defined values).
        dst.validObservations = observedCount[static_cast<size_t>( node )];
        dst.filled = !observed( node ) && std::isfinite( v );
      }
    };
    int segStart = -1; // start of the current bridged segment
    int runStart = -1; // start of the current unobserved run
    for ( int node = 0; node < nCal; ++node )
    {
      if ( observed( node ) )
      {
        if ( runStart >= 0 && node - runStart > maxGap )
        {
          // The run splits segments only between observed spans.
          if ( segStart >= 0 )
            solveSegment( segStart, runStart - 1 );
          segStart = -1;
        }
        if ( segStart < 0 )
          segStart = node;
        runStart = -1;
      }
      else if ( runStart < 0 )
        runStart = node;
    }
    if ( segStart >= 0 )
      solveSegment( segStart, nCal - 1 );
    // The final segment must end at the LAST OBSERVED node: trim trailing
    // unobserved tail from the solve by masking (solveSegment already
    // refuses past lastObsNode — nothing more needed).
    for ( const auto &p : out.points )
    {
      if ( std::isfinite( p.value ) )
      {
        ++out.validCount;
        if ( p.filled )
          ++out.filledCount;
      }
    }
    return out;
  }

  // Observation-window methods: per calendar point.
  for ( int c = 0; c < nCal; ++c )
  {
    const double t = calendar[static_cast<size_t>( c )].tDays;
    auto &dst = out.points[static_cast<size_t>( c )];

    const int right = lowerBoundObs( tDays, nObs, t ) + 1; // first tDays > t
    const int left = right - 1;                            // last tDays <= t

    if ( options.method == RegularizeMethod::Nearest )
    {
      const double radius = options.maxWindowDays;
      const bool hasLeft = left >= 0 && std::isfinite( series[left] ) &&
                           ( t - tDays[left] ) <= radius;
      const bool hasRight = right < nObs && std::isfinite( series[right] ) &&
                            ( tDays[right] - t ) <= radius;
      if ( hasLeft && hasRight )
      {
        // Tie prefers the earlier observation (left on equal distance).
        const double dl = t - tDays[left];
        const double dr = tDays[right] - t;
        const int pick = dr < dl ? right : left;
        dst.value = series[pick];
        dst.validObservations = 1;
      }
      else if ( hasLeft )
      {
        dst.value = series[left];
        dst.validObservations = 1;
        dst.filled = std::fabs( t - tDays[left] ) >= 1e-9;
      }
      else if ( hasRight )
      {
        dst.value = series[right];
        dst.validObservations = 1;
        dst.filled = std::fabs( tDays[right] - t ) >= 1e-9;
      }
    }
    else if ( options.method == RegularizeMethod::WindowMean )
    {
      const double radius = options.maxWindowDays;
      double sum = 0.0;
      int count = 0;
      bool exactHit = false;
      for ( int i = std::max( 0, left ); i < std::min( nObs, right + 1 ); ++i )
      {
        if ( !std::isfinite( series[i] ) )
          continue;
        if ( std::fabs( tDays[i] - t ) > radius )
          continue;
        sum += series[i];
        ++count;
        if ( std::fabs( tDays[i] - t ) < 1e-9 )
          exactHit = true;
      }
      if ( count > 0 )
      {
        dst.value = static_cast<float>( sum / count );
        dst.validObservations = count;
        dst.filled = !exactHit; // a window mean is synthetic unless the grid
                                // point sits exactly on an observation
      }
    }
    else // Linear
    {
      const bool hasLeft = left >= 0 && std::isfinite( series[left] );
      const bool hasRight = right < nObs && std::isfinite( series[right] );
      if ( hasLeft && hasRight )
      {
        const double t0 = tDays[left];
        const double t1 = tDays[right];
        const double v0 = series[left];
        const double v1 = series[right];
        if ( t1 > t0 )
        {
          const double a = ( t - t0 ) / ( t1 - t0 );
          dst.value = static_cast<float>( v0 + a * ( v1 - v0 ) );
          dst.validObservations = 2;
          // Interpolated between two observed instants counts as synthetic
          // only when no observation sits at the grid point itself.
          dst.filled = ( t0 != t );
        }
        else
        {
          dst.value = v0; // duplicate instants bracket degenerately
          dst.validObservations = 2;
          dst.filled = ( t0 != t );
        }
      }
      else if ( hasLeft && std::fabs( t - tDays[left] ) < 1e-9 )
      {
        dst.value = series[left]; // exact observation, no interpolation
        dst.validObservations = 1;
      }
      else if ( hasRight && std::fabs( tDays[right] - t ) < 1e-9 )
      {
        dst.value = series[right];
        dst.validObservations = 1;
      }
    }

    if ( std::isfinite( dst.value ) )
    {
      ++out.validCount;
      if ( dst.filled )
        ++out.filledCount;
    }
  }
  return out;
}

RegularizedSeries regularizeSeries( const std::vector<float> &series,
                                    const std::vector<double> &tDays,
                                    const std::vector<CalendarPoint> &calendar,
                                    const RegularizeOptions &options )
{
  return regularizeSeries( series.data(), static_cast<int>( series.size() ),
                           tDays.data(), calendar, options );
}

bool parseCadenceToken( const QString &token, CalendarSpec *spec )
{
  const QString t = token.trimmed().toLower();
  if ( t == QLatin1String( "month" ) || t == QLatin1String( "monthly" ) )
  {
    if ( spec )
      spec->cadence = CalendarCadence::Monthly;
    return true;
  }
  if ( t.endsWith( QLatin1String( "d" ) ) )
  {
    bool ok = false;
    const int days = t.left( t.size() - 1 ).toInt( &ok );
    if ( ok && days > 0 )
    {
      if ( spec )
      {
        spec->cadence = CalendarCadence::Days;
        spec->cadenceDays = days;
      }
      return true;
    }
  }
  return false;
}

} // namespace sicnu::temporal
