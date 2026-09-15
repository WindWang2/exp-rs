// rs_classifier_svm.cpp — Phase 10A Task 10.8 + F12 one-vs-rest decision scores.

#include "rs_classifier_svm.h"
#include "rs_class_order.h"
#include "sicnu_logging.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <opencv2/core.hpp>

#include <algorithm>
#include <vector>

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

namespace
{
cv::Ptr<cv::ml::SVM> createRbfSvm()
{
  cv::Ptr<cv::ml::SVM> clf = cv::ml::SVM::create();
  clf->setType( cv::ml::SVM::C_SVC );
  clf->setKernel( cv::ml::SVM::RBF );
  clf->setC( kSvmC );
  clf->setGamma( kSvmGamma );
  clf->setTermCriteria( cv::TermCriteria(
    cv::TermCriteria::MAX_ITER + cv::TermCriteria::EPS, kSvmMaxIter, kSvmEps ) );
  return clf;
}
} // namespace

RsClassifierSvm::RsClassifierSvm()
  : RsClassifierSvm( false )
{
}

RsClassifierSvm::RsClassifierSvm( bool enableOvRDecisionScores )
  : mEnableOvR( enableOvRDecisionScores )
{
  m_clf = createRbfSvm();

  SICNU_LOG_INFO( SicnuLogTags::Classification, QString( "SVM classifier initialized: C=%1, gamma=%2, ovr=%3" )
      .arg( kSvmC ).arg( kSvmGamma ).arg( mEnableOvR ) );
}

bool RsClassifierSvm::fit( const cv::Mat &X, const cv::Mat &y )
{
  mClassLabels.clear();
  mOvrModels.clear();
  mOvrSigns.clear();
  if ( !RsClassifierCvBackend<cv::ml::SVM>::fit( X, y ) )
    return false;

  // Capture the ascending class order for decisionScores() columns.
  std::vector<int> ids( y.rows );
  for ( int i = 0; i < y.rows; ++i )
    ids[static_cast<size_t>( i )] = y.at<int>( i, 0 );
  std::sort( ids.begin(), ids.end() );
  ids.erase( std::unique( ids.begin(), ids.end() ), ids.end() );
  mClassLabels = QVector<int>( ids.cbegin(), ids.cend() );
  if ( !mEnableOvR )
    return true;

  // One-vs-rest: one binary C_SVC per class. OpenCV's RAW_OUTPUT sign
  // convention relative to the two labels is not documented as stable, so
  // each model is sign-calibrated against its own training positives.
  for ( int c = 0; c < mClassLabels.size(); ++c )
  {
    const int positive = mClassLabels[c];
    cv::Mat binary; // CV_32S: +1 positive member, -1 rest
    binary.create( y.rows, 1, CV_32S );
    for ( int i = 0; i < y.rows; ++i )
      binary.at<int>( i, 0 ) = ( y.at<int>( i, 0 ) == positive ) ? 1 : -1;

    cv::Ptr<cv::ml::SVM> ovr = createRbfSvm();
    if ( !ovr->train( X, cv::ml::ROW_SAMPLE, binary ) )
    {
      mOvrModels.clear();
      mOvrSigns.clear();
      return false;
    }

    // Sign calibration: evaluate RAW_OUTPUT on training positives; the sign
    // that makes the majority positive-decision > 0 is stored.
    cv::Mat raw;
    ovr->predict( X, raw, cv::ml::StatModel::RAW_OUTPUT );
    int positives = 0;
    int seen = 0;
    for ( int i = 0; i < y.rows; ++i )
    {
      if ( binary.at<int>( i, 0 ) == 1 )
      {
        ++seen;
        if ( raw.at<float>( i, 0 ) > 0.0f )
          ++positives;
      }
    }
    const int sign = ( positives * 2 >= seen ) ? 1 : -1;
    mOvrModels.append( ovr );
    mOvrSigns.append( sign );
  }
  return true;
}

cv::Mat RsClassifierSvm::decisionScores( const cv::Mat &X ) const
{
  if ( X.empty() || !mEnableOvR || mOvrModels.isEmpty() )
    return cv::Mat();
  cv::Mat out( X.rows, mOvrModels.size(), CV_32F );
  for ( int c = 0; c < mOvrModels.size(); ++c )
  {
    cv::Mat raw;
    try
    {
      mOvrModels[c]->predict( X, raw, cv::ml::StatModel::RAW_OUTPUT );
    }
    catch ( const cv::Exception &e )
    {
      qWarning() << "SvmBackend::decisionScores — OpenCV error:" << e.what();
      return cv::Mat();
    }
    const int sign = mOvrSigns[c];
    for ( int i = 0; i < X.rows; ++i )
      out.at<float>( i, c ) = sign * raw.at<float>( i, 0 );
  }
  return out;
}

bool RsClassifierSvm::save( const QString &path ) const
{
  if ( !RsClassifierCvBackend<cv::ml::SVM>::save( path ) )
    return false;
  if ( !mEnableOvR || mOvrModels.isEmpty() )
    return true;

  // OvR ensembles are part of the model's contract: persist next to the
  // main model. Unlike the RF/MLP labels sidecar this is NOT best-effort —
  // a saved OvR model without its ensemble would silently lose
  // decisionScores() on reload (fail closed instead).
  QJsonObject obj;
  obj.insert( QStringLiteral( "version" ), 1 );
  obj.insert( QStringLiteral( "classIds" ), RsClassOrder::toJsonArray( mClassLabels ) );
  QJsonArray signs;
  for ( int s : mOvrSigns )
    signs.append( s );
  obj.insert( QStringLiteral( "signs" ), signs );
  QFile jsonFile( path + QStringLiteral( ".ovr.json" ) );
  if ( !jsonFile.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
    return false;
  jsonFile.write( QJsonDocument( obj ).toJson( QJsonDocument::Compact ) );
  jsonFile.close();

  for ( int c = 0; c < mOvrModels.size(); ++c )
  {
    const QString ovrPath = path + QStringLiteral( ".ovr%1.yaml" ).arg( c );
    try
    {
      mOvrModels[c]->save( ovrPath.toStdString() );
    }
    catch ( const cv::Exception &e )
    {
      // Leave no partial ensemble behind: an old model reloaded from the
      // fragments plus the new main YAML would be a silently mismatched pair.
      qWarning() << "SvmBackend::save — OvR model error:" << e.what();
      QFile::remove( path + QStringLiteral( ".ovr.json" ) );
      for ( int prev = 0; prev <= c; ++prev )
        QFile::remove( path + QStringLiteral( ".ovr%1.yaml" ).arg( prev ) );
      return false;
    }
  }
  return true;
}

bool RsClassifierSvm::load( const QString &path )
{
  mClassLabels.clear();
  mOvrModels.clear();
  mOvrSigns.clear();
  if ( !RsClassifierCvBackend<cv::ml::SVM>::load( path ) )
    return false;

  QFile jsonFile( path + QStringLiteral( ".ovr.json" ) );
  if ( !jsonFile.exists() )
    return true; // plain model without OvR — historical behaviour

  if ( !jsonFile.open( QIODevice::ReadOnly ) )
    return false;
  const QJsonDocument doc = QJsonDocument::fromJson( jsonFile.readAll() );
  const QJsonObject obj = doc.object();
  if ( obj.value( QStringLiteral( "version" ) ).toInt() != 1 )
    return false;
  if ( !RsClassOrder::fromJsonArray( obj.value( QStringLiteral( "classIds" ) ).toArray(),
                                     mClassLabels ) )
    return false;
  const QJsonArray signs = obj.value( QStringLiteral( "signs" ) ).toArray();
  if ( signs.size() != mClassLabels.size() )
    return false;
  for ( const auto &v : signs )
    mOvrSigns.append( v.toInt() == -1 ? -1 : 1 );

  for ( int c = 0; c < mClassLabels.size(); ++c )
  {
    const QString ovrPath = path + QStringLiteral( ".ovr%1.yaml" ).arg( c );
    try
    {
      cv::Ptr<cv::ml::SVM> ovr = cv::Algorithm::load<cv::ml::SVM>( ovrPath.toStdString() );
      if ( !ovr )
        return false;
      mOvrModels.append( ovr );
    }
    catch ( const cv::Exception &e )
    {
      qWarning() << "SvmBackend::load — OvR model error:" << e.what();
      return false;
    }
  }
  return !mOvrModels.isEmpty();
}
