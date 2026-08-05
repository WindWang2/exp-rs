#include "rs_feature_scaler.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>

bool RsFeatureScaler::fit( const cv::Mat &trainX, Method method )
{
  mFitted = false;
  mMethod = method;
  mMean.clear();
  mStd.clear();
  mMin.clear();
  mMax.clear();
  if ( trainX.empty() || trainX.type() != CV_32F || trainX.cols < 1 )
    return false;

  const int B = trainX.cols;
  mMean.resize( B );
  mStd.resize( B );
  mMin.resize( B );
  mMax.resize( B );

  for ( int j = 0; j < B; ++j )
  {
    double minVal = 0.0, maxVal = 0.0;
    cv::minMaxLoc( trainX.col( j ), &minVal, &maxVal );
    mMin[j] = minVal;
    mMax[j] = ( ( maxVal - minVal ) < kMinStd ) ? ( minVal + 1.0 ) : maxVal;

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
    if ( mMethod == Method::MinMax )
    {
      const float minV = static_cast<float>( mMin[j] );
      const float maxV = static_cast<float>( mMax[j] );
      const float range = ( ( maxV - minV ) < static_cast<float>( kMinStd ) ) ? 1.0f : ( maxV - minV );
      for ( int i = 0; i < out.rows; ++i )
        out.at<float>( i, j ) = ( out.at<float>( i, j ) - minV ) / range;
    }
    else
    {
      const float mean = static_cast<float>( mMean[j] );
      const float stdv = static_cast<float>( mStd[j] );
      for ( int i = 0; i < out.rows; ++i )
        out.at<float>( i, j ) = ( out.at<float>( i, j ) - mean ) / stdv;
    }
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
  QJsonArray meanArr, stdArr, minArr, maxArr;
  for ( double v : mMean )
    meanArr.append( v );
  for ( double v : mStd )
    stdArr.append( v );
  for ( double v : mMin )
    minArr.append( v );
  for ( double v : mMax )
    maxArr.append( v );

  QJsonObject root;
  root.insert( QStringLiteral( "version" ), 1 );
  root.insert( QStringLiteral( "method" ), static_cast<int>( mMethod ) );
  root.insert( QStringLiteral( "mean" ), meanArr );
  root.insert( QStringLiteral( "std" ), stdArr );
  root.insert( QStringLiteral( "min" ), minArr );
  root.insert( QStringLiteral( "max" ), maxArr );
  return root;
}

bool RsFeatureScaler::fromJson( const QJsonObject &obj )
{
  mFitted = false;
  mMean.clear();
  mStd.clear();
  mMin.clear();
  mMax.clear();

  mMethod = static_cast<Method>( obj.value( QStringLiteral( "method" ) ).toInt( 0 ) );
  const QJsonArray meanArr = obj.value( QStringLiteral( "mean" ) ).toArray();
  const QJsonArray stdArr = obj.value( QStringLiteral( "std" ) ).toArray();
  const QJsonArray minArr = obj.value( QStringLiteral( "min" ) ).toArray();
  const QJsonArray maxArr = obj.value( QStringLiteral( "max" ) ).toArray();

  if ( meanArr.isEmpty() || meanArr.size() != stdArr.size() )
    return false;

  const int size = meanArr.size();
  mMean.resize( size );
  mStd.resize( size );
  mMin.resize( size );
  mMax.resize( size );

  for ( int i = 0; i < size; ++i )
  {
    mMean[i] = meanArr[i].toDouble();
    const double s = stdArr[i].toDouble();
    mStd[i] = ( s < kMinStd ) ? 1.0 : s;
    mMin[i] = ( i < minArr.size() ) ? minArr[i].toDouble() : 0.0;
    mMax[i] = ( i < maxArr.size() ) ? maxArr[i].toDouble() : 1.0;
  }
  mFitted = true;
  return true;
}
