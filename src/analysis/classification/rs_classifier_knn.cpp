// rs_classifier_knn.cpp — Foundation 5.0 Milestone H.1.

#include "rs_classifier_knn.h"

RsClassifierKnn::RsClassifierKnn( int k )
    : m_k( k > 0 ? k : 5 )
{
    m_clf = cv::ml::KNearest::create();
    m_clf->setDefaultK( m_k );
    m_clf->setIsClassifier( true );
}
