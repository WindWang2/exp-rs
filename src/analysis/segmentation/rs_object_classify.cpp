// rs_object_classify.cpp — Object-level train/predict on feature rows.
#include "rs_object_classify.h"

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

  cv::Mat predictions = backend.predict( predX );
  if ( predictions.empty() || predictions.rows != X.rows )
  {
    result.errorMessage = QStringLiteral( "classify: prediction failed" );
    return result;
  }

  cv::Mat probs = backend.predictProbabilities( predX );
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
