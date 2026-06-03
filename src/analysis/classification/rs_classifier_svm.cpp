// rs_classifier_svm.cpp — Phase 10A Task 10.8.

#include "rs_classifier_svm.h"

RsClassifierSvm::RsClassifierSvm()
  : mClf( cv::ml::SVM::create() )
{
  mClf->setType( cv::ml::SVM::C_SVC );
  mClf->setKernel( cv::ml::SVM::RBF );
  mClf->setC( 10.0 );
  mClf->setGamma( 0.5 );
  mClf->setTermCriteria( cv::TermCriteria(
    cv::TermCriteria::MAX_ITER + cv::TermCriteria::EPS, 200, 1e-4 ) );
}

bool RsClassifierSvm::fit( const cv::Mat &X, const cv::Mat &y )
{
  if ( X.empty() || y.empty() || X.rows != y.rows )
    return false;
  return mClf->train( X, cv::ml::ROW_SAMPLE, y );
}

cv::Mat RsClassifierSvm::predict( const cv::Mat &X ) const
{
  cv::Mat out;
  mClf->predict( X, out );
  out.convertTo( out, CV_32S );
  return out;
}

bool RsClassifierSvm::save( const QString &path ) const
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

bool RsClassifierSvm::load( const QString &path )
{
  try
  {
    mClf = cv::Algorithm::load<cv::ml::SVM>( path.toStdString() );
    return mClf != nullptr;
  }
  catch ( ... )
  {
    return false;
  }
}
