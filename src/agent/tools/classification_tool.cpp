// src/agent/tools/classification_tool.cpp — D15 Package H.
#include "classification_tool.h"

#include "processing/algorithms/confusion_matrix.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace rs::agent
{
namespace
{
  constexpr double kSaturationB = 700.0; // e^-700 underflows: clamp JM at 2.0

  // Cholesky log-determinant with diagonal ridge escalation; false when the
  // matrix stays non-SPD (callers fall back to the diagonal).
  bool choleskyLogDeterminant( std::vector<double> cov, int dim, double &logDeterminant )
  {
    std::vector<double> l( static_cast<size_t>( dim ) * dim, 0.0 );
    for ( int i = 0; i < dim; ++i )
    {
      for ( int j = 0; j <= i; ++j )
      {
        double sum = cov[static_cast<size_t>( i ) * dim + j];
        for ( int k = 0; k < j; ++k )
          sum -= l[static_cast<size_t>( i ) * dim + k] * l[static_cast<size_t>( j ) * dim + k];
        if ( i == j )
        {
          if ( sum <= 1e-300 )
            return false;
          l[static_cast<size_t>( i ) * dim + j] = std::sqrt( sum );
        }
        else
        {
          l[static_cast<size_t>( i ) * dim + j] = sum / l[static_cast<size_t>( j ) * dim + j];
        }
      }
    }
    logDeterminant = 0.0;
    for ( int i = 0; i < dim; ++i )
      logDeterminant += 2.0 * std::log( l[static_cast<size_t>( i ) * dim + i] );
    return true;
  }

  bool spdLogDeterminant( std::vector<double> cov, int dim, double &logDeterminant )
  {
    double trace = 0.0;
    for ( int i = 0; i < dim; ++i )
      trace += cov[static_cast<size_t>( i ) * dim + i];
    double ridge = std::max( 1e-9, 1e-9 * trace / dim );
    for ( int attempt = 0; attempt < 6; ++attempt )
    {
      std::vector<double> regularized = cov;
      for ( int i = 0; i < dim; ++i )
        regularized[static_cast<size_t>( i ) * dim + i] += ridge;
      if ( choleskyLogDeterminant( regularized, dim, logDeterminant ) )
        return true;
      ridge *= 10.0;
    }
    // Diagonal fallback.
    logDeterminant = 0.0;
    for ( int i = 0; i < dim; ++i )
      logDeterminant += std::log( std::max( cov[static_cast<size_t>( i ) * dim + i] + ridge, 1e-12 ) );
    return true;
  }

  std::vector<double> jsonToVector( const QJsonArray &array )
  {
    std::vector<double> out;
    out.reserve( static_cast<size_t>( array.size() ) );
    for ( const auto &v : array )
      out.push_back( v.toDouble() );
    return out;
  }

  std::vector<std::vector<double>> jsonToMatrix( const QJsonArray &rows )
  {
    std::vector<std::vector<double>> out;
    out.reserve( static_cast<size_t>( rows.size() ) );
    for ( const auto &row : rows )
      out.push_back( jsonToVector( row.toArray() ) );
    return out;
  }

  std::vector<std::vector<int>> jsonToIntMatrix( const QJsonArray &rows )
  {
    std::vector<std::vector<int>> out;
    out.reserve( static_cast<size_t>( rows.size() ) );
    for ( const auto &row : rows )
    {
      const QJsonArray entries = row.toArray();
      std::vector<int> values;
      values.reserve( static_cast<size_t>( entries.size() ) );
      for ( const auto &v : entries )
        values.push_back( v.toInt() );
      out.push_back( std::move( values ) );
    }
    return out;
  }

  QJsonArray vectorToJson( const std::vector<double> &v )
  {
    QJsonArray array;
    for ( const double x : v )
      array.append( x );
    return array;
  }

  QJsonArray matrixToJson( const std::vector<std::vector<double>> &m )
  {
    QJsonArray array;
    for ( const auto &row : m )
      array.append( vectorToJson( row ) );
    return array;
  }
} // namespace

ClassificationDiagnosisTool::ClassificationDiagnosisTool( QObject *parent )
    : QObject( parent )
{
}

double ClassificationDiagnosisTool::computeJeffriesMatusitaDistance( std::span<const double> mean1,
                                                                     std::span<const double> cov1,
                                                                     std::span<const double> mean2,
                                                                     std::span<const double> cov2,
                                                                     int dimension )
{
  if ( dimension <= 0 || mean1.size() < static_cast<size_t>( dimension )
       || mean2.size() < static_cast<size_t>( dimension )
       || cov1.size() < static_cast<size_t>( dimension ) * dimension
       || cov2.size() < static_cast<size_t>( dimension ) * dimension )
    return 0.0;

  // Pooled covariance and mean difference.
  std::vector<double> pooled( static_cast<size_t>( dimension ) * dimension, 0.0 );
  std::vector<double> delta( dimension, 0.0 );
  for ( int i = 0; i < dimension; ++i )
  {
    delta[i] = mean1[i] - mean2[i];
    for ( int j = 0; j < dimension; ++j )
      pooled[static_cast<size_t>( i ) * dimension + j] =
        0.5 * ( cov1[static_cast<size_t>( i ) * dimension + j] + cov2[static_cast<size_t>( i ) * dimension + j] );
  }

  // Solve pooled^-1 delta by Cholesky (with ridge escalation).
  std::vector<double> cholesky( static_cast<size_t>( dimension ) * dimension, 0.0 );
  double pooledLogDet = 0.0;
  // Ridge-free first attempt: well-conditioned (SPD) inputs get an exact
  // factorization; only genuinely singular inputs pay the regularized bias.
  double ridge = 0.0;
  bool ok = false;
  for ( int attempt = 0; attempt < 7 && !ok; ++attempt )
  {
    std::vector<double> regularized = pooled;
    for ( int i = 0; i < dimension; ++i )
      regularized[static_cast<size_t>( i ) * dimension + i] += ridge;
    ok = choleskyLogDeterminant( regularized, dimension, pooledLogDet );
    if ( ok )
      cholesky = std::move( regularized );
    else
      ridge = ridge == 0.0 ? 1e-9 : ridge * 10.0;
  }
  if ( !ok )
    return 0.0;

  double mahalanobis = 0.0;
  {
    // Back-substitution against the accepted factor.  For SPD inputs the
    // accepted factor carries ridge == 0, so the solve is exact; a degenerate
    // input keeps its (documented) regularized factor.
    std::vector<double> tmp( dimension, 0.0 );
    for ( int i = 0; i < dimension; ++i )
    {
      double sum = delta[i];
      for ( int k = 0; k < i; ++k )
        sum -= cholesky[static_cast<size_t>( i ) * dimension + k] * tmp[k];
      tmp[i] = sum / cholesky[static_cast<size_t>( i ) * dimension + i];
    }
    for ( int i = dimension - 1; i >= 0; --i )
    {
      double sum = tmp[i];
      for ( int k = i + 1; k < dimension; ++k )
        sum -= cholesky[static_cast<size_t>( k ) * dimension + i] * tmp[k];
      tmp[i] = sum / cholesky[static_cast<size_t>( i ) * dimension + i];
    }
    for ( int i = 0; i < dimension; ++i )
      mahalanobis += delta[i] * tmp[i];
  }

  double logDet1 = 0.0, logDet2 = 0.0;
  spdLogDeterminant( std::vector<double>( cov1.begin(), cov1.end() ), dimension, logDet1 );
  spdLogDeterminant( std::vector<double>( cov2.begin(), cov2.end() ), dimension, logDet2 );

  const double bhattacharyya =
    0.125 * mahalanobis
    + 0.5 * ( pooledLogDet - 0.5 * ( logDet1 + logDet2 ) );
  if ( bhattacharyya >= kSaturationB )
    return 2.0;
  return 2.0 * ( 1.0 - std::exp( -std::max( 0.0, bhattacharyya ) ) );
}

QJsonObject ClassificationDiagnosisTool::execute( const QJsonObject &inputParameters ) const
{
  QJsonObject out;
  const QJsonArray matrixJson = inputParameters.value( QStringLiteral( "confusion_matrix" ) ).toArray();
  const QJsonArray labelsJson = inputParameters.value( QStringLiteral( "class_labels" ) ).toArray();
  const auto matrix = jsonToIntMatrix( matrixJson );

  const bool schemaOk = !matrix.empty() && matrixJson.size() == labelsJson.size()
                        && std::all_of( matrix.begin(), matrix.end(),
                                        [&]( const std::vector<int> &row )
                                        { return row.size() == matrix.size(); } );
  if ( !schemaOk )
  {
    out.insert( QStringLiteral( "status" ), QStringLiteral( "ERROR" ) );
    out.insert( QStringLiteral( "message" ),
                QStringLiteral( "confusion_matrix must be square and match class_labels length" ) );
    return out;
  }

  std::vector<QString> labels;
  labels.reserve( static_cast<size_t>( labelsJson.size() ) );
  for ( const auto &v : labelsJson )
    labels.push_back( v.toString() );
  std::vector<int> classes;
  for ( int i = 0; i < matrixJson.size(); ++i )
    classes.push_back( i );

  // Build the vote matrix directly (matrix-native: no per-count sample
  // expansion, which would be O(sum of counts) memory on agent input).
  std::vector<std::vector<int64_t>> votes( classes.size(), std::vector<int64_t>( classes.size(), 0 ) );
  for ( size_t t = 0; t < matrix.size(); ++t )
    for ( size_t p = 0; p < matrix[t].size(); ++p )
      votes[t][p] = std::max( 0, matrix[t][p] );
  const auto metrics = rs::processing::ConfusionMatrixEvaluator::finalize( votes, classes );
  const double kappa = metrics.cohensKappa;
  const double overall = metrics.overallAccuracy;
  out.insert( QStringLiteral( "kappa" ), kappa );
  out.insert( QStringLiteral( "overall_accuracy" ), overall );

  // Worst-confused pair: largest off-diagonal vote.
  QJsonObject worst;
  int worstCount = -1;
  for ( size_t t = 0; t < matrix.size(); ++t )
    for ( size_t p = 0; p < matrix[t].size(); ++p )
      if ( t != p && matrix[t][p] > worstCount )
      {
        worstCount = matrix[t][p];
        worst.insert( QStringLiteral( "truth" ), labels[t] );
        worst.insert( QStringLiteral( "predicted" ), labels[p] );
        worst.insert( QStringLiteral( "count" ), matrix[t][p] );
      }
  out.insert( QStringLiteral( "worst_pair" ), worst );

  // Pairwise JM when class statistics are supplied.
  QJsonArray recommendations;
  double minJm = std::numeric_limits<double>::infinity();
  const QJsonObject stats = inputParameters.value( QStringLiteral( "class_stats" ) ).toObject();
  const bool hasStats = !stats.isEmpty();
  if ( hasStats )
  {
    const auto means = jsonToMatrix( stats.value( QStringLiteral( "means" ) ).toArray() );
    const auto covs = jsonToMatrix( stats.value( QStringLiteral( "covs" ) ).toArray() );
    if ( means.size() == matrix.size() && covs.size() == matrix.size() && !means.empty() )
    {
      const int dim = static_cast<int>( means[0].size() );
      std::vector<std::vector<double>> jm( means.size(), std::vector<double>( means.size(), 0.0 ) );
      for ( size_t a = 0; a < means.size(); ++a )
        for ( size_t b = a + 1; b < means.size(); ++b )
        {
          const double distance = computeJeffriesMatusitaDistance(
            means[a], covs[a], means[b], covs[b], dim );
          jm[a][b] = distance;
          jm[b][a] = distance;
          minJm = std::min( minJm, distance );
        }
      out.insert( QStringLiteral( "jm_matrix" ), matrixToJson( jm ) );
    }

    // Prune recommendations: negligible gain share AND strong collinearity.
    const auto gains = jsonToVector( stats.value( QStringLiteral( "gains" ) ).toArray() );
    const auto correlations = jsonToMatrix( stats.value( QStringLiteral( "feature_correlations" ) ).toArray() );
    const auto featureNames = stats.value( QStringLiteral( "feature_names" ) ).toArray();
    if ( !gains.empty() )
    {
      const double gainTotal = std::accumulate( gains.begin(), gains.end(), 0.0 );
      for ( size_t f = 0; f < gains.size(); ++f )
      {
        const double share = gainTotal > 0.0 ? gains[f] / gainTotal : 0.0;
        if ( share >= 0.02 )
          continue;
        double maxCrossCorrelation = 0.0;
        if ( correlations.size() == gains.size() )
          for ( size_t other = 0; other < gains.size(); ++other )
          {
            if ( other == f )
              continue;
            maxCrossCorrelation = std::max( maxCrossCorrelation,
                                            std::abs( correlations[f][other] ) );
          }
        if ( maxCrossCorrelation > 0.9 )
        {
          QJsonObject prune;
          prune.insert( QStringLiteral( "action" ), QStringLiteral( "PRUNE_FEATURE" ) );
          prune.insert( QStringLiteral( "feature_name" ),
                        f < static_cast<size_t>( featureNames.size() )
                          ? featureNames.at( static_cast<int>( f ) ).toString()
                          : QStringLiteral( "feature_%1" ).arg( f ) );
          recommendations.append( prune );
        }
      }
    }
  }
  out.insert( QStringLiteral( "recommendations" ), recommendations );

  const bool weakKappa = kappa < 0.6;
  const bool weakJm = hasStats && minJm < 1.4;
  if ( weakKappa || weakJm )
  {
    out.insert( QStringLiteral( "status" ), QStringLiteral( "WARNING" ) );
    out.insert( QStringLiteral( "code" ), QStringLiteral( "WARN_SEVERE_SPECTRAL_CONFUSION" ) );
  }
  else
  {
    out.insert( QStringLiteral( "status" ), QStringLiteral( "OK" ) );
  }
  return out;
}

} // namespace rs::agent
