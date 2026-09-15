// rs_uncertainty.cpp — see rs_uncertainty.h.
#include "rs_uncertainty.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
constexpr double kSumTolerance = 1e-3;

bool validateRow( std::span<const float> probs, double &sumOut )
{
  double sum = 0.0;
  for ( float p : probs )
  {
    if ( !std::isfinite( p ) || p < 0.0f )
      return false;
    sum += p;
  }
  if ( std::abs( sum - 1.0 ) > kSumTolerance )
    return false;
  sumOut = sum;
  return true;
}
} // namespace

bool RsUncertainty::entropy( std::span<const float> probs, double &outH )
{
  double sum = 0.0;
  if ( !validateRow( probs, sum ) )
    return false;
  double h = 0.0;
  for ( float p : probs )
  {
    const double v = static_cast<double>( p );
    if ( v > 0.0 )
      h -= v * std::log2( v );
  }
  outH = h;
  return true;
}

bool RsUncertainty::margin( std::span<const float> probs, double &outM )
{
  double sum = 0.0;
  if ( !validateRow( probs, sum ) )
    return false;
  if ( probs.size() < 2 )
  {
    outM = 0.0;
    return true;
  }
  double top1 = -1.0;
  double top2 = -1.0;
  for ( float p : probs )
  {
    const double v = static_cast<double>( p );
    if ( v > top1 )
    {
      top2 = top1;
      top1 = v;
    }
    else if ( v > top2 )
    {
      top2 = v;
    }
  }
  outM = top1 - top2;
  return true;
}

bool RsUncertainty::confidence( std::span<const float> probs, double &outC )
{
  double sum = 0.0;
  if ( !validateRow( probs, sum ) )
    return false;
  double top = 0.0;
  for ( float p : probs )
    top = std::max( top, static_cast<double>( p ) );
  outC = top;
  return true;
}

double RsUncertainty::normalizeEntropy( double h, int classCount )
{
  if ( classCount <= 1 )
    return 0.0;
  return h / std::log2( static_cast<double>( classCount ) );
}

bool RsUncertainty::measure( Measure m, std::span<const float> probs, double &out )
{
  switch ( m )
  {
    case Measure::Entropy:
      return entropy( probs, out );
    case Measure::Margin:
      return margin( probs, out );
    case Measure::Confidence:
      return confidence( probs, out );
  }
  return false;
}

bool RsUncertainty::isRejected( Measure m, double value, double threshold )
{
  switch ( m )
  {
    case Measure::Entropy:
      return value >= threshold;
    case Measure::Margin:
    case Measure::Confidence:
      return value <= threshold;
  }
  return false;
}

bool RsUncertainty::ensembleDisagreement( std::span<const float> memberProbs,
                                          int memberCount,
                                          int classCount,
                                          double &outD )
{
  outD = 0.0;
  if ( memberCount < 2 || classCount < 1 )
    return false;
  if ( static_cast<int>( memberProbs.size() ) != memberCount * classCount )
    return false;
  std::vector<double> mean( static_cast<size_t>( classCount ), 0.0 );
  for ( float p : memberProbs )
  {
    if ( !std::isfinite( p ) || p < 0.0f )
      return false;
  }
  for ( int j = 0; j < memberCount; ++j )
  {
    for ( int c = 0; c < classCount; ++c )
    {
      mean[static_cast<size_t>( c )] +=
        static_cast<double>( memberProbs[static_cast<size_t>( j ) * classCount + static_cast<size_t>( c )] );
    }
  }
  for ( int c = 0; c < classCount; ++c )
    mean[static_cast<size_t>( c )] /= memberCount;
  double total = 0.0;
  for ( int c = 0; c < classCount; ++c )
  {
    double var = 0.0;
    for ( int j = 0; j < memberCount; ++j )
    {
      const double diff =
        static_cast<double>( memberProbs[static_cast<size_t>( j ) * classCount + static_cast<size_t>( c )] ) -
        mean[static_cast<size_t>( c )];
      var += diff * diff;
    }
    total += var / memberCount;
  }
  outD = total / classCount;
  return true;
}
