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
  return probs;
}
