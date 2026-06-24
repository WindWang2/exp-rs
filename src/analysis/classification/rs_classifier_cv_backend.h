// rs_classifier_cv_backend.h — Intermediate template base class that factors
// out the common fit/predict/save/load boilerplate shared by OpenCV
// cv::ml::StatModel-derived classifiers (NormalBayes, SVM, etc.).
//
// Concrete subclasses need only provide a constructor that initialises m_clf
// and override name() to return a human-readable label.
#pragma once

#include "rs_classifier_backend.h"

#include <QDebug>
#include <QString>

#include <opencv2/core.hpp>
#include <opencv2/ml.hpp>

/// CRTP-free intermediate: the caller passes the concrete cv::ml type as the
/// template argument (e.g. RsClassifierCvBackend<cv::ml::SVM>).  All I/O goes
/// through the cv::Ptr<cv::Algorithm> base pointer so the four methods never
/// need to know the derived type at compile time.
template <typename T>
class RsClassifierCvBackend : public RsClassifierBackend
{
  public:
    bool fit( const cv::Mat &X, const cv::Mat &y ) override;
    cv::Mat predict( const cv::Mat &X ) const override;
    bool save( const QString &path ) const override;
    bool load( const QString &path ) override;
    bool isFitted() const override { return m_clf && m_clf->isTrained(); }

  protected:
    cv::Ptr<T> m_clf;
};

// ---------------------------------------------------------------------------
// Template implementation (must live in the header).
// ---------------------------------------------------------------------------

template <typename T>
bool RsClassifierCvBackend<T>::fit( const cv::Mat &X, const cv::Mat &y )
{
  if ( X.empty() || y.empty() || X.rows != y.rows )
    return false;
  try
  {
    return m_clf->train( X, cv::ml::ROW_SAMPLE, y );
  }
  catch ( const cv::Exception &e )
  {
    qWarning() << "CvBackend::fit — OpenCV error:" << e.what();
    return false;
  }
  catch ( const std::exception &e )
  {
    qWarning() << "CvBackend::fit — error:" << e.what();
    return false;
  }
}

template <typename T>
cv::Mat RsClassifierCvBackend<T>::predict( const cv::Mat &X ) const
{
  cv::Mat out;
  if ( X.empty() || !m_clf->isTrained() )
    return out;
  try
  {
    m_clf->predict( X, out );
    out.convertTo( out, CV_32S );
  }
  catch ( const cv::Exception &e )
  {
    qWarning() << "CvBackend::predict — OpenCV error:" << e.what();
    out = cv::Mat();
  }
  catch ( const std::exception &e )
  {
    qWarning() << "CvBackend::predict — error:" << e.what();
    out = cv::Mat();
  }
  return out;
}

template <typename T>
bool RsClassifierCvBackend<T>::save( const QString &path ) const
{
  try
  {
    // cv::Algorithm::save() is non-const but logically const for our
    // purposes (serialises state without mutating it).
    const_cast<cv::Ptr<T> &>( m_clf )->save( path.toStdString() );
    return true;
  }
  catch ( const cv::Exception &e )
  {
    qWarning() << "CvBackend::save — OpenCV error:" << e.what();
    return false;
  }
  catch ( const std::exception &e )
  {
    qWarning() << "CvBackend::save — error:" << e.what();
    return false;
  }
}

template <typename T>
bool RsClassifierCvBackend<T>::load( const QString &path )
{
  try
  {
    m_clf = cv::Algorithm::load<T>( path.toStdString() );
    return m_clf != nullptr;
  }
  catch ( const cv::Exception &e )
  {
    qWarning() << "CvBackend::load — OpenCV error:" << e.what();
    return false;
  }
  catch ( const std::exception &e )
  {
    qWarning() << "CvBackend::load — error:" << e.what();
    return false;
  }
}
