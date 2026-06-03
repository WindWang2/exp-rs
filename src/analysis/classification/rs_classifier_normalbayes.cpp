// rs_classifier_normalbayes.cpp — Phase 10A Task 10.8.

#include "rs_classifier_normalbayes.h"

RsClassifierNormalBayes::RsClassifierNormalBayes()
  : mClf( cv::ml::NormalBayesClassifier::create() )
{
}

bool RsClassifierNormalBayes::fit( const cv::Mat &X, const cv::Mat &y )
{
  if ( X.empty() || y.empty() || X.rows != y.rows )
    return false;
  return mClf->train( X, cv::ml::ROW_SAMPLE, y );
}

cv::Mat RsClassifierNormalBayes::predict( const cv::Mat &X ) const
{
  cv::Mat out;
  mClf->predict( X, out );
  out.convertTo( out, CV_32S );
  return out;
}

bool RsClassifierNormalBayes::save( const QString &path ) const
{
  try
  {
    mClf->save( path.toStdString() );
    return true;
  }
  catch ( ... )
  {
    return false;
  }
}

bool RsClassifierNormalBayes::load( const QString &path )
{
  try
  {
    mClf = cv::Algorithm::load<cv::ml::NormalBayesClassifier>( path.toStdString() );
    return mClf != nullptr;
  }
  catch ( ... )
  {
    return false;
  }
}
