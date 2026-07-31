#include "rs_feature_scaler.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>

bool RsFeatureScaler::fit( const cv::Mat &trainX )
{
  mFitted = false;
  mMean.clear();
  mStd.clear();
  if ( trainX.empty() || trainX.type() != CV_32F || trainX.cols < 1 )
    return false;
  const int B = trainX.cols;
  mMean.resize( B );
  mStd.resize( B );
  for ( int j = 0; j < B; ++j )
  {
    cv::Scalar mean, stddev;
    cv::meanStdDev( trainX.col( j ), mean, stddev );
    mMean[j] = mean[0];
    mStd[j] = ( stddev[0] < kMinStd ) ? 1.0 : stddev[0];
  }
  mFitted = true;
  return true;
}

cv::Mat RsFeatureScaler::transform( const cv::Mat &X ) const
{
  if ( !mFitted || X.empty() || X.cols != static_cast<int>( mMean.size() ) )
    return cv::Mat();
  cv::Mat in = X;
  if ( in.type() != CV_32F )
    X.convertTo( in, CV_32F );
  cv::Mat out = in.clone();
  for ( int j = 0; j < out.cols; ++j )
  {
    const float mean = static_cast<float>( mMean[j] );
    const float stdv = static_cast<float>( mStd[j] );
    for ( int i = 0; i < out.rows; ++i )
      out.at<float>( i, j ) = ( out.at<float>( i, j ) - mean ) / stdv;
  }
  return out;
}

bool RsFeatureScaler::saveJson( const QString &path ) const
{
  if ( !mFitted )
    return false;
  QFile f( path );
  if ( !f.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
    return false;
  f.write( QJsonDocument( toJson() ).toJson( QJsonDocument::Compact ) );
  return true;
}

bool RsFeatureScaler::loadJson( const QString &path )
{
  mFitted = false;
  mMean.clear();
  mStd.clear();
  QFile f( path );
  if ( !f.open( QIODevice::ReadOnly ) )
    return false;
  const QJsonDocument doc = QJsonDocument::fromJson( f.readAll() );
  if ( !doc.isObject() )
    return false;
  return fromJson( doc.object() );
}

QJsonObject RsFeatureScaler::toJson() const
{
  QJsonArray meanArr, stdArr;
  for ( double v : mMean )
    meanArr.append( v );
  for ( double v : mStd )
    stdArr.append( v );
  QJsonObject root;
  root.insert( QStringLiteral( "version" ), 1 );
  root.insert( QStringLiteral( "mean" ), meanArr );
  root.insert( QStringLiteral( "std" ), stdArr );
  return root;
}

bool RsFeatureScaler::fromJson( const QJsonObject &obj )
{
  mFitted = false;
  mMean.clear();
  mStd.clear();
  const QJsonArray meanArr = obj.value( QStringLiteral( "mean" ) ).toArray();
  const QJsonArray stdArr = obj.value( QStringLiteral( "std" ) ).toArray();
  if ( meanArr.isEmpty() || meanArr.size() != stdArr.size() )
    return false;
  mMean.resize( meanArr.size() );
  mStd.resize( stdArr.size() );
  for ( int i = 0; i < meanArr.size(); ++i )
  {
    mMean[i] = meanArr[i].toDouble();
    const double s = stdArr[i].toDouble();
    mStd[i] = ( s < kMinStd ) ? 1.0 : s;
  }
  mFitted = true;
  return true;
}
