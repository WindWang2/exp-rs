// src/processing/algorithms/confusion_matrix.cpp — D15 Package F.
#include "confusion_matrix.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace rs::processing
{
namespace
{
  // Deduplicated class slots, first-occurrence order.
  std::vector<int> dedupe( const std::vector<int> &targetClasses )
  {
    std::vector<int> classes;
    classes.reserve( targetClasses.size() );
    for ( const int c : targetClasses )
      if ( std::find( classes.begin(), classes.end(), c ) == classes.end() )
        classes.push_back( c );
    return classes;
  }

  std::unordered_map<int, size_t> slotMap( const std::vector<int> &classes )
  {
    std::unordered_map<int, size_t> map;
    map.reserve( classes.size() * 2 );
    for ( size_t i = 0; i < classes.size(); ++i )
      map.emplace( classes[i], i );
    return map;
  }
} // namespace

EvaluationMetrics ConfusionMatrixEvaluator::compute( std::span<const int> groundTruth,
                                                     std::span<const int> predictions,
                                                     const std::vector<int> &targetClasses )
{
  if ( groundTruth.size() != predictions.size() )
    return {};
  const std::vector<int> classes = dedupe( targetClasses );
  std::vector<std::vector<int64_t>> matrix( classes.size(), std::vector<int64_t>( classes.size(), 0 ) );
  accumulateTile( matrix, groundTruth, predictions, classes );
  return finalize( matrix, classes );
}

void ConfusionMatrixEvaluator::accumulateTile( std::vector<std::vector<int64_t>> &inOutMatrix,
                                               std::span<const int> gtTile,
                                               std::span<const int> predTile,
                                               const std::vector<int> &targetClasses )
{
  if ( gtTile.size() != predTile.size() )
    return;
  const std::vector<int> classes = dedupe( targetClasses );
  const size_t k = classes.size();
  if ( k == 0 || inOutMatrix.size() != k )
    return;
  for ( const auto &row : inOutMatrix )
    if ( static_cast<size_t>( row.size() ) != k )
      return;
  const auto slots = slotMap( classes );

  for ( size_t i = 0; i < gtTile.size(); ++i )
  {
    const auto gt = slots.find( gtTile[i] );
    if ( gt == slots.end() )
      continue; // off-target class: excluded from the evaluation entirely
    const auto pred = slots.find( predTile[i] );
    if ( pred == slots.end() )
      continue;
    ++inOutMatrix[gt->second][pred->second];
  }
}

EvaluationMetrics ConfusionMatrixEvaluator::finalize( const std::vector<std::vector<int64_t>> &matrix,
                                                      const std::vector<int> &targetClasses )
{
  EvaluationMetrics metrics;
  const std::vector<int> classes = dedupe( targetClasses );
  const size_t k = classes.size();
  if ( k == 0 || matrix.size() != k )
    return metrics;
  for ( const auto &row : matrix )
    if ( static_cast<size_t>( row.size() ) != k )
      return metrics;

  metrics.classLabels = classes;
  metrics.matrix = matrix;

  int64_t total = 0;
  std::vector<int64_t> rowSums( k, 0 ), colSums( k, 0 );
  for ( size_t i = 0; i < k; ++i )
    for ( size_t j = 0; j < k; ++j )
    {
      total += matrix[i][j];
      rowSums[i] += matrix[i][j];
      colSums[j] += matrix[i][j];
    }
  metrics.totalSampleCount = total;
  if ( total == 0 )
  {
    metrics.perClassStats.assign( k, ClassAccuracyStats {} );
    for ( size_t i = 0; i < k; ++i )
      metrics.perClassStats[i].classId = classes[i];
    return metrics;
  }

  int64_t diagonal = 0;
  double expectedAgreement = 0.0;
  for ( size_t i = 0; i < k; ++i )
  {
    diagonal += matrix[i][i];
    expectedAgreement += static_cast<double>( rowSums[i] ) * static_cast<double>( colSums[i] );
  }
  expectedAgreement /= static_cast<double>( total ) * static_cast<double>( total );

  const double po = static_cast<double>( diagonal ) / static_cast<double>( total );
  metrics.overallAccuracy = po;

  // Guards: perfect agreement is kappa 1 even when pe degenerates to 1;
  // pe == 1 otherwise means chance-level consistency -> kappa 0.
  if ( po >= 1.0 - 1e-12 )
    metrics.cohensKappa = 1.0;
  else if ( expectedAgreement >= 1.0 - 1e-12 )
    metrics.cohensKappa = 0.0;
  else
    metrics.cohensKappa = ( po - expectedAgreement ) / ( 1.0 - expectedAgreement );

  metrics.perClassStats.assign( k, ClassAccuracyStats {} );
  double f1Sum = 0.0;
  for ( size_t i = 0; i < k; ++i )
  {
    ClassAccuracyStats &stats = metrics.perClassStats[i];
    stats.classId = classes[i];
    stats.groundTruthTotal = rowSums[i];
    stats.predictedTotal = colSums[i];
    stats.truePositives = matrix[i][i];
    stats.producerAccuracy = rowSums[i] > 0
                               ? static_cast<double>( matrix[i][i] ) / static_cast<double>( rowSums[i] )
                               : 0.0;
    stats.userAccuracy = colSums[i] > 0
                           ? static_cast<double>( matrix[i][i] ) / static_cast<double>( colSums[i] )
                           : 0.0;
    const int64_t denom = rowSums[i] + colSums[i];
    stats.f1Score = denom > 0 ? static_cast<double>( 2 * matrix[i][i] ) / static_cast<double>( denom )
                              : 0.0;
    f1Sum += stats.f1Score;
  }
  metrics.macroF1Score = f1Sum / static_cast<double>( k );
  return metrics;
}

} // namespace rs::processing
