// tests/test_classification_agent_tools.cpp — D15 Package H.
//
// Ground-truth policy: Bhattacharyya/JM values below are closed-form
// algebra (equal variances cancel the log term; the unequal-variance case
// reduces to 2(1 - 1/sqrt(1.25))); the canonical confusion matrix kappa is
// derived by hand.  Nothing is recomputed through the code under test.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <QJsonArray>
#include <QJsonObject>

#include <cmath>
#include <span>
#include <vector>

#include "agent/tools/classification_tool.h"

using Catch::Matchers::WithinAbs;
using rs::agent::ClassificationDiagnosisTool;

namespace
{
  QJsonArray jsonVector( const std::vector<double> &v )
  {
    QJsonArray a;
    for ( const double x : v )
      a.append( x );
    return a;
  }

  QJsonArray jsonMatrix( const std::vector<std::vector<double>> &m )
  {
    QJsonArray a;
    for ( const auto &row : m )
      a.append( jsonVector( row ) );
    return a;
  }
} // namespace

TEST_CASE( "JM distance vanishes for identical distributions", "[d15][agent]" )
{
  const std::vector<double> m1 = { 0.0 };
  const std::vector<double> c1 = { 1.0 };
  const double jm = ClassificationDiagnosisTool::computeJeffriesMatusitaDistance( m1, c1, m1, c1, 1 );
  REQUIRE_THAT( jm, WithinAbs( 0.0, 1e-9 ) );
}

TEST_CASE( "JM distance saturates at 2 for disjoint Gaussians", "[d15][agent]" )
{
  // B = (1/8) * 100^2 / 1 = 1250 >> saturation threshold -> exactly 2.
  const std::vector<double> m1 = { 0.0 };
  const std::vector<double> m2 = { 100.0 };
  const std::vector<double> c = { 1.0 };
  const double jm = ClassificationDiagnosisTool::computeJeffriesMatusitaDistance( m1, c, m2, c, 1 );
  REQUIRE_THAT( jm, WithinAbs( 2.0, 1e-9 ) );
}

TEST_CASE( "JM distance matches the closed form for shifted equal variances", "[d15][agent]" )
{
  // Equal covariances cancel the log term: B = (1/8) * 4 / 1 = 0.5
  // => JM = 2(1 - e^-0.5) = 0.7869386805747332.
  const std::vector<double> m1 = { 0.0 };
  const std::vector<double> m2 = { 2.0 };
  const std::vector<double> c = { 1.0 };
  const double jm = ClassificationDiagnosisTool::computeJeffriesMatusitaDistance( m1, c, m2, c, 1 );
  REQUIRE_THAT( jm, WithinAbs( 2.0 * ( 1.0 - std::exp( -0.5 ) ), 1e-9 ) );
}

TEST_CASE( "JM distance handles unequal variances via the analytic reduction", "[d15][agent]" )
{
  // Same mean, sigma^2 of 1 vs 4: B = (1/2) ln( 2.5 / sqrt(4) ) = (1/2) ln 1.25
  // => JM = 2(1 - e^-(0.5 ln 1.25)) = 2(1 - 1/sqrt(1.25)) = 0.21114561800...
  const std::vector<double> m = { 0.0 };
  const std::vector<double> c1 = { 1.0 };
  const std::vector<double> c2 = { 4.0 };
  const double jm = ClassificationDiagnosisTool::computeJeffriesMatusitaDistance( m, c1, m, c2, 1 );
  REQUIRE_THAT( jm, WithinAbs( 2.0 * ( 1.0 - 1.0 / std::sqrt( 1.25 ) ), 1e-9 ) );
}

TEST_CASE( "Diagnosis flags spectral confusion on a weak kappa", "[d15][agent]" )
{
  // Hand-derived: diag = 210, N = 300 -> po = 0.7; row sums 100 each,
  // col sums (120, 100, 80) -> pe = 30000/90000 = 1/3
  // => kappa = (0.7 - 1/3)/(2/3) = 0.55 < 0.6 -> WARNING.
  // Max off-diagonal is 25 (truth "built" predicted as "water_label"? no:
  // labels index 1 -> predicted index 0).
  QJsonObject input;
  input.insert( QStringLiteral( "confusion_matrix" ), jsonMatrix( {
                                                                   { 80, 15, 5 },
                                                                   { 25, 65, 10 },
                                                                   { 15, 20, 65 },
                                                                 } ) );
  QJsonArray labels;
  labels.append( QStringLiteral( "water" ) );
  labels.append( QStringLiteral( "built" ) );
  labels.append( QStringLiteral( "vegetation" ) );
  input.insert( QStringLiteral( "class_labels" ), labels );

  const rs::agent::ClassificationDiagnosisTool tool;
  const QJsonObject out = tool.execute( input );

  REQUIRE( out.value( QStringLiteral( "status" ) ).toString() == QStringLiteral( "WARNING" ) );
  REQUIRE( out.value( QStringLiteral( "code" ) ).toString() == QStringLiteral( "WARN_SEVERE_SPECTRAL_CONFUSION" ) );
  REQUIRE_THAT( out.value( QStringLiteral( "kappa" ) ).toDouble(), WithinAbs( 0.55, 1e-9 ) );
  const QJsonObject worst = out.value( QStringLiteral( "worst_pair" ) ).toObject();
  REQUIRE( worst.value( QStringLiteral( "truth" ) ).toString() == QStringLiteral( "built" ) );
  REQUIRE( worst.value( QStringLiteral( "predicted" ) ).toString() == QStringLiteral( "water" ) );
  REQUIRE( worst.value( QStringLiteral( "count" ) ).toInt() == 25 );
}

TEST_CASE( "Diagnosis reports per-class JM and stays OK for separable classes", "[d15][agent]" )
{
  QJsonObject input;
  input.insert( QStringLiteral( "confusion_matrix" ), jsonMatrix( { { 90, 1 }, { 1, 90 } } ) );
  QJsonArray labels;
  labels.append( QStringLiteral( "water" ) );
  labels.append( QStringLiteral( "built" ) );
  input.insert( QStringLiteral( "class_labels" ), labels );

  QJsonObject stats;
  stats.insert( QStringLiteral( "means" ), jsonMatrix( { { 0.0 }, { 100.0 } } ) );
  QJsonArray covs;
  covs.append( jsonMatrix( { { 1.0 } } ) );
  covs.append( jsonMatrix( { { 1.0 } } ) );
  stats.insert( QStringLiteral( "covs" ), covs );
  input.insert( QStringLiteral( "class_stats" ), stats );

  const rs::agent::ClassificationDiagnosisTool tool;
  const QJsonObject out = tool.execute( input );
  REQUIRE( out.value( QStringLiteral( "status" ) ).toString() == QStringLiteral( "OK" ) );

  const QJsonArray jm = out.value( QStringLiteral( "jm_matrix" ) ).toArray();
  const double jm01 = jm.at( 0 ).toArray().at( 1 ).toDouble();
  // B = (1/8)*100^2 -> saturation: exactly 2.0.
  REQUIRE_THAT( jm01, WithinAbs( 2.0, 1e-9 ) );
}

TEST_CASE( "Diagnosis recommends pruning redundant low-gain features", "[d15][agent]" )
{
  QJsonObject input;
  input.insert( QStringLiteral( "confusion_matrix" ), jsonMatrix( { { 95, 1 }, { 1, 95 } } ) );
  QJsonArray labels;
  labels.append( QStringLiteral( "water" ) );
  labels.append( QStringLiteral( "built" ) );
  input.insert( QStringLiteral( "class_labels" ), labels );

  QJsonObject stats;
  // Two features: second carries 1% gain and correlates 0.95 with the first.
  QJsonArray gains;
  gains.append( 0.99 );
  gains.append( 0.01 );
  stats.insert( QStringLiteral( "gains" ), gains );
  QJsonArray names;
  names.append( QStringLiteral( "band_1_contrast" ) );
  names.append( QStringLiteral( "band_2_glcm_variance" ) );
  stats.insert( QStringLiteral( "feature_names" ), names );
  stats.insert( QStringLiteral( "means" ), jsonMatrix( { { 0.0, 0.0 }, { 100.0, 100.0 } } ) );
  QJsonArray covs;
  covs.append( jsonMatrix( { { 1.0, 0.0 }, { 0.0, 1.0 } } ) );
  covs.append( jsonMatrix( { { 1.0, 0.0 }, { 0.0, 1.0 } } ) );
  stats.insert( QStringLiteral( "covs" ), covs );
  // Pooled collinearity proxy: pass per-feature correlation through an
  // extra key the tool documents ("feature_correlations").
  stats.insert( QStringLiteral( "feature_correlations" ), jsonMatrix( { { 1.0, 0.95 }, { 0.95, 1.0 } } ) );
  input.insert( QStringLiteral( "class_stats" ), stats );

  const rs::agent::ClassificationDiagnosisTool tool;
  const QJsonObject out = tool.execute( input );
  const QJsonArray recs = out.value( QStringLiteral( "recommendations" ) ).toArray();
  bool foundPrune = false;
  for ( const auto &r : recs )
  {
    const QJsonObject o = r.toObject();
    if ( o.value( QStringLiteral( "action" ) ).toString() == QStringLiteral( "PRUNE_FEATURE" )
         && o.value( QStringLiteral( "feature_name" ) ).toString() == QStringLiteral( "band_2_glcm_variance" ) )
      foundPrune = true;
  }
  REQUIRE( foundPrune );
}

TEST_CASE( "Diagnosis rejects malformed input with an ERROR status", "[d15][agent]" )
{
  const rs::agent::ClassificationDiagnosisTool tool;
  const QJsonObject out = tool.execute( QJsonObject{} );
  REQUIRE( out.value( QStringLiteral( "status" ) ).toString() == QStringLiteral( "ERROR" ) );

  QJsonObject mismatched;
  mismatched.insert( QStringLiteral( "confusion_matrix" ), jsonMatrix( { { 1, 2 }, { 3, 4 } } ) );
  QJsonArray labels;
  labels.append( QStringLiteral( "only_one" ) );
  mismatched.insert( QStringLiteral( "class_labels" ), labels );
  REQUIRE( tool.execute( mismatched ).value( QStringLiteral( "status" ) ).toString() == QStringLiteral( "ERROR" ) );
}
