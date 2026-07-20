// rs_classifier_svm.cpp — Phase 10A Task 10.8.

#include "rs_classifier_svm.h"
#include "sicnu_logging.h"

// SVM hyperparameters (tuned for typical remote-sensing land-cover tasks).
// C=10 — moderately high regularisation penalty to reduce misclassification
//        on small training sets common in RS workflows.
// gamma=0.5 — RBF kernel bandwidth; balances locality vs. generalisation.
// maxIter=1000 / eps=1e-4 — allow large multi-class RS problems to converge
//        after standardisation; still EPS-driven for small sets.
static constexpr double kSvmC        = 10.0;
static constexpr double kSvmGamma    = 0.5;
static constexpr int    kSvmMaxIter  = 1000;
static constexpr double kSvmEps      = 1e-4;

RsClassifierSvm::RsClassifierSvm()
{
  m_clf = cv::ml::SVM::create();
  m_clf->setType( cv::ml::SVM::C_SVC );
  m_clf->setKernel( cv::ml::SVM::RBF );
  m_clf->setC( kSvmC );
  m_clf->setGamma( kSvmGamma );
  m_clf->setTermCriteria( cv::TermCriteria(
    cv::TermCriteria::MAX_ITER + cv::TermCriteria::EPS, kSvmMaxIter, kSvmEps ) );

  SICNU_LOG_INFO( SicnuLogTags::Classification, QString( "SVM classifier initialized: C=%1, gamma=%2" )
      .arg( kSvmC ).arg( kSvmGamma ) );
}
