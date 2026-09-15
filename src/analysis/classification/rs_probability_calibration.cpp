// rs_probability_calibration.cpp — see rs_probability_calibration.h.
#include "rs_probability_calibration.h"

#include "rs_class_order.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr double kGradientEps = 1e-10;
constexpr double kHessianRidge = 1e-10;
constexpr double kRowSumTolerance = 1e-2;

bool allFinite( std::span<const float> v )
{
  for ( float x : v )
  {
    if ( !std::isfinite( x ) )
      return false;
  }
  return true;
}

double sigmoid( double z )
{
  if ( z >= 0.0 )
  {
    const double e = std::exp( -z );
    return 1.0 / ( 1.0 + e );
  }
  const double e = std::exp( z );
  return e / ( 1.0 + e );
}

double clamp01( double v )
{
  return std::min( 1.0, std::max( 0.0, v ) );
}

struct BinarySet
{
  std::vector<double> scores;
  std::vector<int> targets;
};

/// Builds the one-vs-rest binary sub-problem for column \a column.
bool buildBinarySet( std::span<const float> rawScores, int sampleCount, int column,
                     std::span<const int> labels, const QVector<int> &classIds,
                     BinarySet &out )
{
  out.scores.clear();
  out.targets.clear();
  out.scores.reserve( static_cast<size_t>( sampleCount ) );
  out.targets.reserve( static_cast<size_t>( sampleCount ) );
  const int positiveClass = classIds[column];
  for ( int i = 0; i < sampleCount; ++i )
  {
    const float s = rawScores[static_cast<size_t>( i ) * classIds.size() + static_cast<size_t>( column )];
    if ( !std::isfinite( s ) )
      return false;
    out.scores.push_back( s );
    out.targets.push_back( labels[i] == positiveClass ? 1 : 0 );
  }
  return true;
}

// Pool-adjacent-violators: weighted isotonic regression on (score, target).
// Returns blocks as (blockMeanScore, blockRate, blockWeight).
bool pav( std::vector<double> scores, std::vector<int> targets,
          std::vector<double> &outX, std::vector<double> &outY )
{
  struct Block
  {
    double sumX;
    double sumT;
    double weight;
    double meanX() const { return sumX / weight; }
    double rate() const { return clamp01( sumT / weight ); }
  };
  std::vector<Block> stack;
  const size_t n = scores.size();
  for ( size_t i = 0; i < n; ++i )
  {
    if ( !std::isfinite( scores[i] ) )
      return false;
    Block b { scores[i], static_cast<double>( targets[i] ), 1.0 };
    stack.push_back( b );
    // Pool while the last block's rate violates monotonicity with the one below.
    while ( stack.size() >= 2 &&
            stack[stack.size() - 2].rate() > stack[stack.size() - 1].rate() )
    {
      Block top = stack.back();
      stack.pop_back();
      Block &below = stack.back();
      below.sumX += top.sumX;
      below.sumT += top.sumT;
      below.weight += top.weight;
    }
  }
  outX.clear();
  outY.clear();
  outX.reserve( stack.size() );
  outY.reserve( stack.size() );
  for ( const Block &b : stack )
  {
    outX.push_back( b.meanX() );
    outY.push_back( b.rate() );
  }
  return true;
}
} // namespace

// ---------------------------------------------------------------- model ---

QJsonObject RsCalibrationModel::toJson() const
{
  QJsonObject obj;
  obj.insert( QStringLiteral( "version" ), 1 );
  switch ( method )
  {
    case Method::Platt:
      obj.insert( QStringLiteral( "method" ), QStringLiteral( "platt" ) );
      break;
    case Method::Isotonic:
      obj.insert( QStringLiteral( "method" ), QStringLiteral( "isotonic" ) );
      break;
    case Method::None:
      return obj;
  }
  obj.insert( QStringLiteral( "classIds" ), RsClassOrder::toJsonArray( classIds ) );
  if ( method == Method::Platt )
  {
    QJsonObject platt;
    QJsonArray a, b;
    for ( double v : plattA )
      a.append( v );
    for ( double v : plattB )
      b.append( v );
    platt.insert( QStringLiteral( "a" ), a );
    platt.insert( QStringLiteral( "b" ), b );
    obj.insert( QStringLiteral( "platt" ), platt );
  }
  else
  {
    QJsonObject iso;
    QJsonArray xs, ys;
    for ( const auto &vec : isotonicX )
    {
      QJsonArray row;
      for ( double v : vec )
        row.append( v );
      xs.append( row );
    }
    for ( const auto &vec : isotonicY )
    {
      QJsonArray row;
      for ( double v : vec )
        row.append( v );
      ys.append( row );
    }
    iso.insert( QStringLiteral( "x" ), xs );
    iso.insert( QStringLiteral( "y" ), ys );
    obj.insert( QStringLiteral( "isotonic" ), iso );
  }
  return obj;
}

bool RsCalibrationModel::fromJson( const QJsonObject &obj )
{
  *this = RsCalibrationModel();
  if ( obj.value( QStringLiteral( "version" ) ).toInt() != 1 )
    return false;
  const QString methodStr = obj.value( QStringLiteral( "method" ) ).toString();
  if ( methodStr == QLatin1String( "platt" ) )
    method = Method::Platt;
  else if ( methodStr == QLatin1String( "isotonic" ) )
    method = Method::Isotonic;
  else
    return false;

  if ( !RsClassOrder::fromJsonArray( obj.value( QStringLiteral( "classIds" ) ).toArray(), classIds ) )
  {
    *this = RsCalibrationModel();
    return false;
  }

  if ( method == Method::Platt )
  {
    const QJsonObject platt = obj.value( QStringLiteral( "platt" ) ).toObject();
    const QJsonArray a = platt.value( QStringLiteral( "a" ) ).toArray();
    const QJsonArray b = platt.value( QStringLiteral( "b" ) ).toArray();
    if ( a.size() != classIds.size() || b.size() != classIds.size() )
    {
      *this = RsCalibrationModel();
      return false;
    }
    for ( int i = 0; i < a.size(); ++i )
    {
      plattA.append( a.at( i ).toDouble() );
      plattB.append( b.at( i ).toDouble() );
    }
  }
  else
  {
    const QJsonObject iso = obj.value( QStringLiteral( "isotonic" ) ).toObject();
    const QJsonArray xs = iso.value( QStringLiteral( "x" ) ).toArray();
    const QJsonArray ys = iso.value( QStringLiteral( "y" ) ).toArray();
    if ( xs.size() != classIds.size() || ys.size() != classIds.size() )
    {
      *this = RsCalibrationModel();
      return false;
    }
    for ( int i = 0; i < xs.size(); ++i )
    {
      const QJsonArray xr = xs.at( i ).toArray();
      const QJsonArray yr = ys.at( i ).toArray();
      if ( xr.size() != yr.size() || xr.isEmpty() )
      {
        *this = RsCalibrationModel();
        return false;
      }
      QVector<double> xv, yv;
      xv.reserve( xr.size() );
      yv.reserve( yr.size() );
      for ( int j = 0; j < xr.size(); ++j )
      {
        xv.append( xr.at( j ).toDouble() );
        yv.append( yr.at( j ).toDouble() );
      }
      // Validate knots: x strictly ascending, y non-decreasing in [0,1].
      for ( int j = 0; j < xv.size(); ++j )
      {
        if ( !std::isfinite( xv[j] ) || !std::isfinite( yv[j] ) || yv[j] < 0.0 || yv[j] > 1.0 )
        {
          *this = RsCalibrationModel();
          return false;
        }
        if ( j > 0 && ( xv[j] <= xv[j - 1] || yv[j] < yv[j - 1] ) )
        {
          *this = RsCalibrationModel();
          return false;
        }
      }
      isotonicX.append( xv );
      isotonicY.append( yv );
    }
  }
  return isValid();
}

// ------------------------------------------------------------------ fit ---

bool RsProbabilityCalibrator::fitPlatt( std::span<const float> rawScores,
                                        int sampleCount,
                                        const QVector<int> &classIds,
                                        std::span<const int> labels,
                                        int maxIter,
                                        RsCalibrationModel &outModel )
{
  outModel = RsCalibrationModel();
  const int k = classIds.size();
  if ( sampleCount <= 0 || k <= 0 || maxIter <= 0 )
    return false;
  if ( !RsClassOrder::isValid( classIds ) )
    return false;
  if ( static_cast<int>( labels.size() ) != sampleCount )
    return false;
  if ( static_cast<int>( rawScores.size() ) != static_cast<size_t>( sampleCount ) * k )
    return false;

  RsCalibrationModel model;
  model.method = RsCalibrationModel::Method::Platt;
  model.classIds = classIds;
  model.plattA.resize( k );
  model.plattB.resize( k );

  for ( int c = 0; c < k; ++c )
  {
    BinarySet set;
    if ( !buildBinarySet( rawScores, sampleCount, c, labels, classIds, set ) )
      return false;
    double nPos = 0.0;
    for ( int t : set.targets )
      nPos += t;
    const double nNeg = static_cast<double>( sampleCount ) - nPos;
    // A class with no positive or no negative member has no separating
    // calibration statistic — fail closed instead of inventing one.
    if ( nPos == 0.0 || nNeg == 0.0 )
      return false;

    // Platt (1999) warm start.
    double a = 0.0;
    double b = std::log( ( nNeg + 1.0 ) / ( nPos + 1.0 ) );

    bool converged = false;
    for ( int iter = 0; iter < maxIter; ++iter )
    {
      double gA = 0.0, gB = 0.0;
      double hAA = 0.0, hAB = 0.0, hBB = 0.0;
      for ( size_t i = 0; i < set.scores.size(); ++i )
      {
        const double s = set.scores[i];
        const double t = static_cast<double>( set.targets[i] );
        const double p = sigmoid( a * s + b );
        const double w = p * ( 1.0 - p );
        const double err = p - t;
        gA += err * s;
        gB += err;
        hAA += w * s * s;
        hAB += w * s;
        hBB += w;
      }
      const double gradNorm = std::sqrt( gA * gA + gB * gB );
      if ( gradNorm < kGradientEps )
      {
        converged = true;
        break;
      }
      // Symmetric 2x2 solve with ridge guard against singular Hessians
      // (perfectly separable scores drive w -> 0).
      double hAAs = hAA + kHessianRidge;
      double hBBs = hBB + kHessianRidge;
      const double det = hAAs * hBBs - hAB * hAB;
      if ( !( std::abs( det ) > 0.0 ) )
      {
        hAAs += kHessianRidge * 10.0;
        hBBs += kHessianRidge * 10.0;
      }
      const double det2 = hAAs * hBBs - hAB * hAB;
      if ( !( std::abs( det2 ) > 0.0 ) )
        return false;
      const double deltaA = ( hBBs * gA - hAB * gB ) / det2;
      const double deltaB = ( hAAs * gB - hAB * gA ) / det2;
      a -= deltaA;
      b -= deltaB;
      if ( std::abs( deltaA ) + std::abs( deltaB ) < kGradientEps )
      {
        converged = true;
        break;
      }
    }
    if ( !converged )
    {
      // Parameter drift check: unconverged fits must not ship silently.
      if ( !std::isfinite( a ) || !std::isfinite( b ) )
        return false;
    }
    model.plattA[c] = a;
    model.plattB[c] = b;
  }

  outModel = model;
  return true;
}

bool RsProbabilityCalibrator::fitIsotonic( std::span<const float> rawScores,
                                           int sampleCount,
                                           const QVector<int> &classIds,
                                           std::span<const int> labels,
                                           RsCalibrationModel &outModel )
{
  outModel = RsCalibrationModel();
  const int k = classIds.size();
  if ( sampleCount <= 0 || k <= 0 )
    return false;
  if ( !RsClassOrder::isValid( classIds ) )
    return false;
  if ( static_cast<int>( labels.size() ) != sampleCount )
    return false;
  if ( static_cast<int>( rawScores.size() ) != static_cast<size_t>( sampleCount ) * k )
    return false;

  RsCalibrationModel model;
  model.method = RsCalibrationModel::Method::Isotonic;
  model.classIds = classIds;

  for ( int c = 0; c < k; ++c )
  {
    BinarySet set;
    if ( !buildBinarySet( rawScores, sampleCount, c, labels, classIds, set ) )
      return false;
    double nPos = 0.0;
    for ( int t : set.targets )
      nPos += t;
    if ( nPos == 0.0 || nPos == static_cast<double>( sampleCount ) )
      return false;

    // Sort by score (stable for deterministic tie handling).
    std::vector<size_t> order( set.scores.size() );
    for ( size_t i = 0; i < order.size(); ++i )
      order[i] = i;
    std::sort( order.begin(), order.end(), [&]( size_t x, size_t y ) {
      if ( set.scores[x] != set.scores[y] )
        return set.scores[x] < set.scores[y];
      return x < y;
    } );
    std::vector<double> s( order.size() );
    std::vector<int> t( order.size() );
    for ( size_t i = 0; i < order.size(); ++i )
    {
      s[i] = set.scores[order[i]];
      t[i] = set.targets[order[i]];
    }

    std::vector<double> knotX, knotY;
    if ( !pav( std::move( s ), std::move( t ), knotX, knotY ) )
      return false;
    if ( knotX.empty() )
      return false;
    model.isotonicX.append( QVector<double>( knotX.cbegin(), knotX.cend() ) );
    model.isotonicY.append( QVector<double>( knotY.cbegin(), knotY.cend() ) );
  }

  outModel = model;
  return true;
}

bool RsProbabilityCalibrator::apply( const RsCalibrationModel &model,
                                     std::span<const float> rawScores,
                                     int sampleCount,
                                     std::vector<float> &outProbs )
{
  outProbs.clear();
  if ( !model.isValid() )
    return false;
  const int k = model.classIds.size();
  if ( sampleCount < 0 )
    return false;
  if ( static_cast<int>( rawScores.size() ) != static_cast<size_t>( sampleCount ) * k )
    return false;
  if ( !allFinite( rawScores ) )
    return false;
  if ( sampleCount == 0 )
    return true;

  outProbs.assign( rawScores.size(), 0.0f );
  for ( int i = 0; i < sampleCount; ++i )
  {
    double rowSum = 0.0;
    for ( int c = 0; c < k; ++c )
    {
      const double s = rawScores[static_cast<size_t>( i ) * k + static_cast<size_t>( c )];
      double p = 0.0;
      if ( model.method == RsCalibrationModel::Method::Platt )
      {
        p = sigmoid( model.plattA[c] * s + model.plattB[c] );
      }
      else
      {
        const auto &x = model.isotonicX[c];
        const auto &y = model.isotonicY[c];
        if ( s <= x.first() )
        {
          p = y.first();
        }
        else if ( s >= x.last() )
        {
          p = y.last();
        }
        else
        {
          const auto it = std::lower_bound( x.cbegin(), x.cend(), s );
          const int hi = static_cast<int>( it - x.cbegin() );
          const int lo = hi - 1;
          if ( x[hi] == x[lo] )
          {
            p = y[hi];
          }
          else
          {
            const double w = ( s - x[lo] ) / ( x[hi] - x[lo] );
            p = y[lo] + w * ( y[hi] - y[lo] );
          }
        }
      }
      p = clamp01( p );
      outProbs[static_cast<size_t>( i ) * k + static_cast<size_t>( c )] = static_cast<float>( p );
      rowSum += p;
    }
    if ( rowSum <= 0.0 )
    {
      // Isotonic can map every class of a row to 0; the documented fallback
      // is the uniform distribution (no preference is invented).
      for ( int c = 0; c < k; ++c )
        outProbs[static_cast<size_t>( i ) * k + static_cast<size_t>( c )] =
          static_cast<float>( 1.0 / k );
    }
    else
    {
      for ( int c = 0; c < k; ++c )
        outProbs[static_cast<size_t>( i ) * k + static_cast<size_t>( c )] /=
          static_cast<float>( rowSum );
    }
  }
  return true;
}

// -------------------------------------------------------------- metrics ---

QJsonObject RsCalibrationMetrics::Report::toJson() const
{
  QJsonObject obj;
  obj.insert( QStringLiteral( "ok" ), ok );
  obj.insert( QStringLiteral( "brier" ), brier );
  obj.insert( QStringLiteral( "ece" ), ece );
  obj.insert( QStringLiteral( "logLoss" ), logLoss );
  obj.insert( QStringLiteral( "binCount" ), binCount );
  obj.insert( QStringLiteral( "classIds" ), RsClassOrder::toJsonArray( classIds ) );
  QJsonArray binArr;
  for ( const Bin &b : bins )
  {
    QJsonObject bo;
    bo.insert( QStringLiteral( "meanConfidence" ), b.meanConfidence );
    bo.insert( QStringLiteral( "empiricalAccuracy" ), b.empiricalAccuracy );
    bo.insert( QStringLiteral( "count" ), b.count );
    binArr.append( bo );
  }
  obj.insert( QStringLiteral( "bins" ), binArr );
  return obj;
}

RsCalibrationMetrics::Report RsCalibrationMetrics::compute( std::span<const int> labels,
                                                            std::span<const float> probs,
                                                            int sampleCount,
                                                            const QVector<int> &classIds,
                                                            int binCount )
{
  Report report;
  const int k = classIds.size();
  if ( sampleCount <= 0 || k <= 0 || binCount <= 0 || binCount > 1000 )
    return report;
  if ( !RsClassOrder::isValid( classIds ) )
    return report;
  if ( static_cast<int>( labels.size() ) != sampleCount )
    return report;
  if ( static_cast<int>( probs.size() ) != static_cast<size_t>( sampleCount ) * k )
    return report;
  if ( !allFinite( probs ) )
    return report;
  // Rows must be normalised (fail closed on drift > 1e-2 per row).
  for ( int i = 0; i < sampleCount; ++i )
  {
    double rowSum = 0.0;
    for ( int c = 0; c < k; ++c )
    {
      const double p = probs[static_cast<size_t>( i ) * k + static_cast<size_t>( c )];
      if ( p < 0.0 )
        return report;
      rowSum += p;
    }
    if ( std::abs( rowSum - 1.0 ) > kRowSumTolerance )
      return report;
  }

  report.ok = true;
  report.classIds = classIds;
  report.binCount = binCount;
  report.bins.resize( binCount );

  double brierSum = 0.0;
  double logLossSum = 0.0;
  constexpr double kLogClamp = 1e-12;
  for ( int i = 0; i < sampleCount; ++i )
  {
    const int label = labels[i];
    const int trueColumn = RsClassOrder::columnOf( classIds, label );
    if ( trueColumn < 0 )
    {
      report = Report();
      return report;
    }
    double topProb = -1.0;
    int topColumn = -1;
    for ( int c = 0; c < k; ++c )
    {
      const double p = probs[static_cast<size_t>( i ) * k + static_cast<size_t>( c )];
      const double diff = p - ( c == trueColumn ? 1.0 : 0.0 );
      brierSum += diff * diff;
      if ( c == trueColumn )
        logLossSum += -std::log( std::max( p, kLogClamp ) );
      if ( p > topProb )
      {
        topProb = p;
        topColumn = c;
      }
    }
    const int bin = std::min( binCount - 1,
                              static_cast<int>( topProb * binCount ) );
    Bin &b = report.bins[bin];
    b.count += 1;
    b.meanConfidence += topProb;
    if ( topColumn == trueColumn )
      b.empiricalAccuracy += 1.0;
  }

  report.brier = brierSum / sampleCount;
  report.logLoss = logLossSum / sampleCount;
  double eceSum = 0.0;
  for ( Bin &b : report.bins )
  {
    if ( b.count > 0 )
    {
      b.meanConfidence /= b.count;
      b.empiricalAccuracy /= b.count;
      eceSum += ( static_cast<double>( b.count ) / sampleCount ) *
                std::abs( b.empiricalAccuracy - b.meanConfidence );
    }
  }
  report.ece = eceSum;
  return report;
}
