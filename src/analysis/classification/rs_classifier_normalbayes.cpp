// rs_classifier_normalbayes.cpp — Phase 10A Task 10.8 + F12 class order sidecar.

#include "rs_classifier_normalbayes.h"
#include "rs_classifier_labels_sidecar.h"
#include "rs_class_order.h"
#include "sicnu_logging.h"

#include <QFile>
#include <QJsonDocument>

#include <algorithm>
#include <vector>

RsClassifierNormalBayes::RsClassifierNormalBayes()
{
  m_clf = cv::ml::NormalBayesClassifier::create();
  SICNU_LOG_INFO( SicnuLogTags::Classification, "NormalBayes classifier initialized" );
}

bool RsClassifierNormalBayes::fit( const cv::Mat &X, const cv::Mat &y )
{
  mClassLabels.clear();
  if ( !RsClassifierCvBackend<cv::ml::NormalBayesClassifier>::fit( X, y ) )
    return false;
  // OpenCV's predictProb columns follow the sorted distinct training labels;
  // capture the order at fit time so it can be persisted and verified
  // (RsClassOrder contract).
  std::vector<int> ids( y.rows );
  for ( int i = 0; i < y.rows; ++i )
    ids[static_cast<size_t>( i )] = y.at<int>( i, 0 );
  std::sort( ids.begin(), ids.end() );
  ids.erase( std::unique( ids.begin(), ids.end() ), ids.end() );
  mClassLabels = QVector<int>( ids.cbegin(), ids.cend() );
  return true;
}

bool RsClassifierNormalBayes::save( const QString &path ) const
{
  if ( !RsClassifierCvBackend<cv::ml::NormalBayesClassifier>::save( path ) )
    return false;
  // Persist the probability column order alongside the model. Unlike the
  // RF/MLP sidecars this is fail-closed: a save without the sidecar means
  // the column-order guarantee is lost — and an EMPTY order must never be
  // written either, because "[]" would make every subsequent load() of this
  // model file fail validation (self-poisoning legacy round-trip).
  if ( mClassLabels.isEmpty() )
    return false;
  // #1175: stamp the pair so a torn sidecar cannot load as "legacy empty".
  if ( !sicnu::classification::labels_sidecar::writePair(
         path, RsClassOrder::toJson( mClassLabels ) ) )
  {
    QFile::remove( path );
    return false;
  }
  return true;
}

bool RsClassifierNormalBayes::load( const QString &path )
{
  mClassLabels.clear();
  if ( !RsClassifierCvBackend<cv::ml::NormalBayesClassifier>::load( path ) )
    return false;
  QJsonArray arr;
  using LS = sicnu::classification::labels_sidecar::LoadStatus;
  const LS status = sicnu::classification::labels_sidecar::loadArray( path, arr );
  if ( status == LS::Failed )
    return false;
  if ( status == LS::LegacyMissing )
    return true; // legacy model without a sidecar — classOrder() stays empty
  QVector<int> order;
  if ( !RsClassOrder::fromJsonArray( arr, order ) )
    return false;
  mClassLabels = order;
  return true;
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
