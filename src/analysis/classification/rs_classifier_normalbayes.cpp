// rs_classifier_normalbayes.cpp — Phase 10A Task 10.8.

#include "rs_classifier_normalbayes.h"
#include "sicnu_logging.h"

RsClassifierNormalBayes::RsClassifierNormalBayes()
{
  m_clf = cv::ml::NormalBayesClassifier::create();
  SICNU_LOG_INFO( SicnuLogTags::Classification, "NormalBayes classifier initialized" );
}

cv::Mat RsClassifierNormalBayes::predictProbabilities( const cv::Mat &X ) const
{
  cv::Mat probs, outputs;
  if ( X.empty() || !m_clf || !m_clf->isTrained() )
    return probs;
  try
  {
    m_clf->predictProb( X, outputs, probs );
  }
  catch ( const cv::Exception &e )
  {
    qWarning() << "RsClassifierNormalBayes::predictProbabilities — error:" << e.what();
    probs = cv::Mat();
  }

  // OpenCV's predictProb returns unnormalized class likelihoods (Gaussian
  // PDF values) that can exceed 1. Normalize each row to a proper posterior
  // (rows sum to 1) so the values are comparable to other backends and usable
  // as confidence in [0, 1]. Rows with all-zero/negative likelihoods stay 0.
  if ( !probs.empty() && probs.rows > 0 && probs.cols >= 1 )
  {
    cv::Mat normalized( probs.rows, probs.cols, CV_32F );
    for ( int r = 0; r < probs.rows; ++r )
    {
      double sum = 0.0;
      for ( int c = 0; c < probs.cols; ++c )
        sum += probs.at<float>( r, c );
      if ( sum > 0.0 )
      {
        for ( int c = 0; c < probs.cols; ++c )
          normalized.at<float>( r, c ) = static_cast<float>( probs.at<float>( r, c ) / sum );
      }
      else
      {
        for ( int c = 0; c < probs.cols; ++c )
          normalized.at<float>( r, c ) = 0.0f;
      }
    }
    probs = normalized;
  }
  return probs;
}

bool RsClassifierNormalBayes::predictWithProbabilities( const cv::Mat &X, cv::Mat &outLabels,
                                                        cv::Mat &outProbs ) const
{
  outLabels.release();
  outProbs.release();
  if ( X.empty() || !m_clf || !m_clf->isTrained() )
    return false;
  try
  {
    cv::Mat outputs;
    m_clf->predictProb( X, outputs, outProbs );
    if ( outputs.empty() || outProbs.empty() )
      return false;
    outputs.convertTo( outLabels, CV_32S );
    // Normalize probs same as predictProbabilities
    if ( outProbs.rows > 0 && outProbs.cols >= 1 )
    {
      cv::Mat normalized( outProbs.rows, outProbs.cols, CV_32F );
      for ( int r = 0; r < outProbs.rows; ++r )
      {
        double sum = 0.0;
        for ( int c = 0; c < outProbs.cols; ++c )
          sum += outProbs.at<float>( r, c );
        if ( sum > 0.0 )
        {
          for ( int c = 0; c < outProbs.cols; ++c )
            normalized.at<float>( r, c ) = static_cast<float>( outProbs.at<float>( r, c ) / sum );
        }
        else
        {
          for ( int c = 0; c < outProbs.cols; ++c )
            normalized.at<float>( r, c ) = 0.0f;
        }
      }
      outProbs = normalized;
    }
    return true;
  }
  catch ( const cv::Exception &e )
  {
    qWarning() << "RsClassifierNormalBayes::predictWithProbabilities — error:" << e.what();
    outLabels.release();
    outProbs.release();
    return false;
  }
}
