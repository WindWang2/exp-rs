// rs_classifier_normalbayes.cpp — Phase 10A Task 10.8.

#include "rs_classifier_normalbayes.h"
#include "sicnu_logging.h"

RsClassifierNormalBayes::RsClassifierNormalBayes()
{
  m_clf = cv::ml::NormalBayesClassifier::create();
  SICNU_LOG_INFO( SicnuLogTags::Classification, "NormalBayes classifier initialized" );
}
