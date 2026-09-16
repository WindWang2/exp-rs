// tests/test_temporal_uncertainty.cpp — known-answer tests for the Temporal
// Intelligence 11.0 uncertainty kernel (package C: analytic weighted-LS
// coefficient CIs + deterministic residual bootstrap).
// Oracles: hand-derived linear algebra truths and closed-form invariants —
// never the implementation under test.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processing/algorithms/temporal/temporal_uncertainty.h"
#include "processing/algorithms/temporal/temporal_fit.h"
#include "processing/algorithms/temporal/temporal_change.h"
#include "temporal_corpus.h"

#include <cmath>
#include <vector>

using namespace sicnu::temporal;
using Catch::Approx;

namespace
{
constexpr double kPi = 3.14159265358979323846;
} // namespace

TEST_CASE( "analytic CI: exact linear series recovers coefficients and binds "
           "the truth",
           "[temporal][uncertainty][analytic]" )
{
  // y = 2 + 0.5·t exactly (harmonics=1 adds sin/cos columns whose true
  // coefficients are 0 — with an exact linear series the system still
  // solves: sin/cos regressors are not collinear with [1, t]).
  const int n = 60;
  std::vector<float> y;
  std::vector<double> t;
  for ( int i = 0; i < n; ++i )
  {
    t.push_back( 10.0 * i );
    y.push_back( static_cast<float>( 2.0 + 0.5 * t.back() ) );
  }
  const AnalyticCiResult ci = harmonicTrendCoefficientCi(
      y, t, 0, n, 1, {}, 0.95 );
  REQUIRE( ci.valid );
  REQUIRE( ci.coefficients.size() == 4 );
  CHECK( ci.df == n - 4 );
  CHECK( ci.coefficients[0].estimate == Approx( 2.0 ).epsilon( 1e-9 ) );
  CHECK( ci.coefficients[1].estimate == Approx( 0.5 ).epsilon( 1e-9 ) );
  // Perfect fit: sigma2 == 0 → degenerate zero-width intervals... the
  // documented behavior: variance 0 → valid with zero-width bounds.
  CHECK( ci.sigma2 == Approx( 0.0 ).margin( 1e-18 ) );
  CHECK( ci.coefficients[0].lower == Approx( 2.0 ).epsilon( 1e-9 ) );
  CHECK( ci.coefficients[0].upper == Approx( 2.0 ).epsilon( 1e-9 ) );
}

TEST_CASE( "analytic CI: noise widens intervals and the 95% bounds bracket "
           "the true slope",
           "[temporal][uncertainty][analytic]" )
{
  const temporal_corpus::Scenario scenario =
    temporal_corpus::noChangeControl( 96, 2.0, 0.0, 0.002, 0.05, 20260924u );
  const int n = static_cast<int>( scenario.y.size() );
  // Truth has NO seasonality → fit with harmonics=1; slope oracle = 0.002.
  const AnalyticCiResult ci =
    harmonicTrendCoefficientCi( scenario.y, scenario.grid.tDays, 0, n, 1, {}, 0.95 );
  REQUIRE( ci.valid );
  const CoefficientInterval &slope = ci.coefficients[1];
  CHECK( slope.stdError > 0.0 );
  CHECK( slope.lower <= 0.002 );
  CHECK( slope.upper >= 0.002 );
  CHECK( slope.upper - slope.lower ==
         Approx( 2.0 * 1.959964 * slope.stdError ).epsilon( 1e-6 ) );
  // A larger noise realization must not shrink the same-truth interval.
  const temporal_corpus::Scenario noisier =
    temporal_corpus::noChangeControl( 96, 2.0, 0.0, 0.002, 0.20, 20260925u );
  const AnalyticCiResult ci2 =
    harmonicTrendCoefficientCi( noisier.y, noisier.grid.tDays, 0, n, 1, {}, 0.95 );
  REQUIRE( ci2.valid );
  CHECK( ci2.sigma2 > ci.sigma2 );
  CHECK( ci2.coefficients[1].stdError > slope.stdError );
}

TEST_CASE( "analytic CI: quality weights enter the variance — downweighting "
           "noisy samples shrinks the interval",
           "[temporal][uncertainty][analytic][weights]" )
{
  // Half the samples carry 10× the noise; the weighted fit knows which.
  const int n = 80;
  std::vector<float> y;
  std::vector<double> t;
  std::vector<double> weights;
  temporal_corpus::Gaussian gauss( 20260926u );
  for ( int i = 0; i < n; ++i )
  {
    t.push_back( 8.0 * i );
    const bool noisy = ( i % 2 ) == 1;
    y.push_back( static_cast<float>(
      1.0 + 0.01 * t.back() + ( noisy ? 0.3 : 0.03 ) * gauss() ) );
    weights.push_back( noisy ? 1.0 / 100.0 : 1.0 );
  }
  const AnalyticCiResult weighted =
    harmonicTrendCoefficientCi( y, t, 0, n, 1, weights, 0.95 );
  const AnalyticCiResult unweighted =
    harmonicTrendCoefficientCi( y, t, 0, n, 1, {}, 0.95 );
  REQUIRE( weighted.valid );
  REQUIRE( unweighted.valid );
  CHECK( weighted.coefficients[1].stdError < unweighted.coefficients[1].stdError );
  // Global weight rescaling leaves standard errors unchanged (σ̂² and
  // (XᵀWX)⁻¹ scale inversely).
  std::vector<double> scaled( weights.size() );
  for ( size_t i = 0; i < weights.size(); ++i )
    scaled[i] = 50.0 * weights[i];
  const AnalyticCiResult scaledCi =
    harmonicTrendCoefficientCi( y, t, 0, n, 1, scaled, 0.95 );
  REQUIRE( scaledCi.valid );
  CHECK( scaledCi.coefficients[1].stdError ==
         Approx( weighted.coefficients[1].stdError ).epsilon( 1e-12 ) );
}

TEST_CASE( "analytic CI: refusal semantics",
           "[temporal][uncertainty][analytic][negative]" )
{
  std::vector<float> y{ 1.0f, 2.0f };
  std::vector<double> t{ 0.0, 8.0 };
  const AnalyticCiResult short_ =
    harmonicTrendCoefficientCi( y, t, 0, 2, 1, {}, 0.95 );
  CHECK( !short_.valid );
  CHECK( std::string( short_.refusalReason ) == "insufficient_valid_samples" );

  // All samples at one t → singular system.
  std::vector<float> y2( 10, 1.0f );
  std::vector<double> t2( 10, 5.0 );
  const AnalyticCiResult singular =
    harmonicTrendCoefficientCi( y2, t2, 0, 10, 1, {}, 0.95 );
  CHECK( !singular.valid );
  CHECK( std::string( singular.refusalReason ) == "singular_system" );
}

TEST_CASE( "bootstrap: deterministic, centered on the estimate, and "
           "seed-sensitive",
           "[temporal][uncertainty][bootstrap]" )
{
  const temporal_corpus::Scenario scenario =
    temporal_corpus::noChangeControl( 80, 5.0, 0.0, 0.0, 0.1, 20260927u );
  std::vector<float> fitted( scenario.y.size(), 5.0f );
  const auto meanStat = []( const std::vector<float> &v ) {
    double s = 0.0;
    int c = 0;
    for ( float x : v )
      if ( std::isfinite( x ) )
      {
        s += x;
        ++c;
      }
    return c > 0 ? s / c : std::numeric_limits<double>::quiet_NaN();
  };
  BootstrapOptions options;
  options.seed = 42u;
  options.resamples = 199;
  const BootstrapCi first =
    residualBootstrapCi( scenario.y, fitted, meanStat, options );
  const BootstrapCi second =
    residualBootstrapCi( scenario.y, fitted, meanStat, options );
  REQUIRE( first.valid );
  CHECK( first.lower == second.lower );
  CHECK( first.upper == second.upper );
  CHECK( first.successes == second.successes );
  CHECK( first.estimate == Approx( meanStat( scenario.y ) ) );
  // The 95% interval brackets the sample mean (percentile bootstrap of a
  // symmetric statistic) and its width is in the right regime.
  CHECK( first.lower <= first.estimate );
  CHECK( first.upper >= first.estimate );
  CHECK( ( first.upper - first.lower ) > 0.0 );
  CHECK( ( first.upper - first.lower ) < 1.0 );  // σ=0.1, n=80: se≈0.011

  options.seed = 43u;
  const BootstrapCi third = residualBootstrapCi( scenario.y, fitted, meanStat, options );
  CHECK( third.valid );
  CHECK( third.lower != first.lower );  // a different seed moves the bounds
}

TEST_CASE( "bootstrap: breaks statistic — CI contains the planted magnitude",
           "[temporal][uncertainty][bootstrap][integration]" )
{
  const temporal_corpus::Scenario scenario =
    temporal_corpus::trendBreakOnly( 92, 46, 5.0, 1.5, 0.05, 20260928u );
  const SeasonalTrendBreaksResult fit = fitSeasonalTrendBreaks(
      scenario.y, scenario.grid.tDays, 1, 2, 8, 0.10, false );
  REQUIRE( !fit.breaks.empty() );
  REQUIRE( fit.fitted.size() == scenario.y.size() );
  const double estimate = fit.breaks.front().magnitude;
  REQUIRE( std::isfinite( estimate ) );

  const auto magnitudeStat = [&]( const std::vector<float> &resampled ) {
    const SeasonalTrendBreaksResult r = fitSeasonalTrendBreaks(
        resampled, scenario.grid.tDays, 1, 2, 8, 0.10, false );
    if ( r.breaks.empty() )
      return std::numeric_limits<double>::quiet_NaN();
    return r.breaks.front().magnitude;
  };
  BootstrapOptions options;
  options.seed = 20260915u;
  options.resamples = 99;
  const BootstrapCi ci =
    residualBootstrapCi( scenario.y, fit.fitted, magnitudeStat, options );
  REQUIRE( ci.valid );
  CHECK( ci.successes >= 59 );  // ≥ 60% of 99
  CHECK( ci.lower <= estimate + 0.25 );
  CHECK( ci.upper >= estimate - 0.25 );
}

TEST_CASE( "bootstrap: low refit success rate refuses instead of fabricating",
           "[temporal][uncertainty][bootstrap][negative]" )
{
  // Alternating 0/1 residuals around fitted 0: the statistic succeeds only
  // when the first resample draws a 0 (~50% < the 60% floor) → refusal.
  std::vector<float> y;
  for ( int i = 0; i < 50; ++i )
    y.push_back( i % 2 == 0 ? 0.0f : 1.0f );
  std::vector<float> fitted( 50, 0.5f );
  const auto pickyStat = []( const std::vector<float> &v ) {
    return v.front() == 0.0f ? 1.0 : std::numeric_limits<double>::quiet_NaN();
  };
  BootstrapOptions options;
  options.seed = 7u;
  options.resamples = 99;
  const BootstrapCi ci = residualBootstrapCi( y, fitted, pickyStat, options );
  CHECK( !ci.valid );
  CHECK( std::string( ci.refusalReason ) == "low_success_rate" );
}

TEST_CASE( "bootstrap: missing/irregular sampling keeps only observed indices",
           "[temporal][uncertainty][bootstrap][negative]" )
{
  temporal_corpus::Scenario scenario =
    temporal_corpus::noChangeControl( 80, 1.0, 0.0, 0.0, 0.1, 20260929u );
  temporal_corpus::applyMissing( scenario, 0.4, 99u );
  std::vector<float> fitted( scenario.y.size(), 1.0f );
  const auto countStat = []( const std::vector<float> &v ) {
    double s = 0.0;
    int c = 0;
    for ( float x : v )
      if ( std::isfinite( x ) )
      {
        s += x;
        ++c;
      }
    return c > 0 ? s / c : std::numeric_limits<double>::quiet_NaN();
  };
  BootstrapOptions options;
  options.seed = 5u;
  const BootstrapCi ci = residualBootstrapCi( scenario.y, fitted, countStat, options );
  // Gaps stay NaN in every resample (residuals are only drawn at observed
  // indices); the statistic still succeeds and the CI is finite.
  REQUIRE( ci.valid );
  CHECK( std::isfinite( ci.lower ) );
  CHECK( std::isfinite( ci.upper ) );
}
