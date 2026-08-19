// rs_object_classify.cpp — Object-level train/predict on feature rows.
#include "rs_object_classify.h"
#include "rs_hungarian_assignment.h"

#include <QHash>
#include <QSet>

#ifdef SICNU_HAS_OPENCV

RsObjectClassifyResult RsObjectClassify::classify(
    const cv::Mat &X,
    const QVector<quint32> &segmentIds,
    const QMap<quint32, int> &trainingLabels,
    RsClassifierBackend &backend,
    bool enableScaling,
    RsFeatureScaler::Method scalingMethod )
{
  RsObjectClassifyResult result;

  if ( X.empty() || segmentIds.isEmpty() || X.rows != segmentIds.size() )
  {
    result.errorMessage = QStringLiteral( "classify: invalid inputs or row mismatch" );
    return result;
  }

  std::vector<int> trainRows;
  std::vector<int> trainYvals;
  for ( int i = 0; i < segmentIds.size(); ++i )
  {
    quint32 sId = segmentIds[i];
    if ( trainingLabels.contains( sId ) )
    {
      trainRows.push_back( i );
      trainYvals.push_back( trainingLabels[sId] );
    }
  }

  result.labeledCount = static_cast<int>( trainRows.size() );
  if ( trainRows.empty() )
  {
    result.errorMessage = QStringLiteral( "classify: no training samples found in matrix" );
    return result;
  }

  const int nFeatures = X.cols;
  cv::Mat trainX( static_cast<int>( trainRows.size() ), nFeatures, CV_32F );
  cv::Mat trainY( static_cast<int>( trainRows.size() ), 1, CV_32S );
  for ( int i = 0; i < static_cast<int>( trainRows.size() ); ++i )
  {
    X.row( trainRows[static_cast<size_t>( i )] ).copyTo( trainX.row( i ) );
    trainY.at<int>( i, 0 ) = trainYvals[static_cast<size_t>( i )];
  }

  cv::Mat fitX = trainX;
  cv::Mat predX = X;
  if ( enableScaling )
  {
    result.scaler.fit( trainX, scalingMethod );
    fitX = result.scaler.transform( trainX );
    predX = result.scaler.transform( X );
  }

  if ( !backend.isFitted() )
  {
    if ( !backend.fit( fitX, trainY ) )
    {
      result.errorMessage = QStringLiteral( "classify: backend training failed" );
      return result;
    }
  }

  // Hungarian cluster→class remap for needsLabelRemap backends (KMeans).
  QHash<int, int> kmeansRemap;
  if ( backend.needsLabelRemap() )
  {
    try
    {
      cv::Mat trainPred = backend.predict( fitX );
      if ( !trainPred.empty() && trainPred.rows == trainY.rows )
      {
        QSet<int> trueSet, clusterSet;
        for ( int i = 0; i < trainY.rows; ++i )
          trueSet.insert( trainY.at<int>( i, 0 ) );
        for ( int i = 0; i < trainPred.rows; ++i )
          clusterSet.insert( trainPred.at<int>( i, 0 ) );
        if ( !trueSet.isEmpty() )
        {
          QList<int> tList( trueSet.begin(), trueSet.end() );
          QList<int> cList( clusterSet.begin(), clusterSet.end() );
          std::sort( tList.begin(), tList.end() );
          std::sort( cList.begin(), cList.end() );
          const int N = tList.size();
          const int M = cList.size();
          cv::Mat cost = cv::Mat::zeros( N, M, CV_64F );
          for ( int i = 0; i < trainY.rows; ++i )
          {
            const int ti = tList.indexOf( trainY.at<int>( i, 0 ) );
            const int ci = cList.indexOf( trainPred.at<int>( i, 0 ) );
            if ( ti >= 0 && ci >= 0 )
              cost.at<double>( ti, ci ) -= 1.0;
          }
          const QVector<int> assign = RsHungarianAssignment::solve( cost );
          for ( int i = 0; i < N && i < assign.size(); ++i )
          {
            const int clusterIdx = assign[i];
            if ( clusterIdx >= 0 && clusterIdx < M )
              kmeansRemap[cList[clusterIdx]] = tList[i];
          }
        }
      }
    }
    catch ( ... )
    {
      // keep kmeansRemap empty — raw cluster ids will be emitted
    }
  }

  cv::Mat predictions = backend.predict( predX );
  if ( predictions.empty() || predictions.rows != X.rows )
  {
    result.errorMessage = QStringLiteral( "classify: prediction failed" );
    return result;
  }
  if ( !kmeansRemap.isEmpty() )
  {
    for ( int i = 0; i < predictions.rows; ++i )
    {
      const int raw = predictions.at<int>( i, 0 );
      predictions.at<int>( i, 0 ) = kmeansRemap.value( raw, raw );
    }
  }

  cv::Mat probs;
  // Prefer combined predictWithProbabilities when available; entropy is
  // permutation-invariant so we keep probs as-is (remapped labels via kmeansRemap).
  {
    cv::Mat combinedLabels, combinedProbs;
    if ( backend.predictWithProbabilities( predX, combinedLabels, combinedProbs )
         && combinedLabels.rows == X.rows )
    {
      // combinedLabels already contains remapped ids for MLP/RF; for KMeans
      // predictWithProbabilities is not implemented, so this branch not taken.
      probs = combinedProbs;
    }
    else
    {
      probs = backend.predictProbabilities( predX );
    }
  }
  for ( int i = 0; i < segmentIds.size(); ++i )
  {
    result.segmentClasses[segmentIds[i]] = predictions.at<int>( i, 0 );

    double entropy = 0.0;
    if ( !probs.empty() && probs.rows == segmentIds.size() )
    {
      double sumP = 0.0;
      for ( int k = 0; k < probs.cols; ++k )
      {
        double p = std::max( 0.0, static_cast<double>( probs.at<float>( i, k ) ) );
        sumP += p;
      }
      if ( sumP > 1e-6 )
      {
        for ( int k = 0; k < probs.cols; ++k )
        {
          double p = std::max( 0.0, static_cast<double>( probs.at<float>( i, k ) ) ) / sumP;
          if ( p > 1e-12 )
            entropy -= p * std::log2( p );
        }
      }
    }
    result.segmentUncertainties[segmentIds[i]] = entropy;
  }

  result.predictedCount = segmentIds.size();
  result.ok = true;
  return result;
}

#endif // SICNU_HAS_OPENCV
