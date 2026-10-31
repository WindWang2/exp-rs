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

  // F12 hardening — fail closed on non-finite training values. NoData/NaN
  // cells that slipped past the caller would silently poison mean/stddev
  // (cv::meanStdDev is undefined on NaN), so the scaler refuses the fit
  // instead of producing a corrupted normalisation.
  for ( int i = 0; i < trainX.rows; ++i )
  {
    const float *row = trainX.ptr<float>( i );
    for ( int j = 0; j < trainX.cols; ++j )
    {
      if ( !std::isfinite( row[j] ) )
        return false;
    }
  }

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

  // Pre-compute per-column offset and scale so the inner loop is branch-free.
  // The original code iterated columns in the outer loop and rows in the inner
  // loop via out.at<float>(i, j) - a strided access over a row-major Mat that
  // thrashed the cache. Iterating rows in the outer loop with ptr<float>(i)
  // gives contiguous access; pulling offset/scale into flat arrays removes the
  // per-pixel method branch.
  const int B = in.cols;
  std::vector<float> offset( B ), scale( B );
  for ( int j = 0; j < B; ++j )
  {
    if ( mMethod == Method::MinMax )
    {
      const float minV = static_cast<float>( mMin[j] );
      const float maxV = static_cast<float>( mMax[j] );
      const float range = ( ( maxV - minV ) < static_cast<float>( kMinStd ) ) ? 1.0f : ( maxV - minV );
      offset[j] = minV;
      scale[j] = 1.0f / range;
    }
    else
    {
      offset[j] = static_cast<float>( mMean[j] );
      scale[j] = 1.0f / static_cast<float>( mStd[j] );
    }
  }

  cv::Mat out = in.clone();
  for ( int i = 0; i < out.rows; ++i )
  {
    float *row = out.ptr<float>( i );
    for ( int j = 0; j < B; ++j )
      row[j] = ( row[j] - offset[j] ) * scale[j];
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

  // Fail closed on a foreign discriminator: an out-of-range method silently
  // fell through to ZScore and an unknown version promised a schema this
  // reader does not implement — both would rescale bands with the wrong
  // statistics (R26-S12), the same drift the length checks below guard
  // against. A missing version is tolerated (pre-versioning sidecars).
  const int methodInt = obj.value( QStringLiteral( "method" ) ).toInt( -1 );
  if ( methodInt != static_cast<int>( Method::ZScore )
       && methodInt != static_cast<int>( Method::MinMax ) )
    return false;
  const int version = obj.value( QStringLiteral( "version" ) ).toInt( 1 );
  if ( version != 1 )
    return false;
  mMethod = static_cast<Method>( methodInt );
  const QJsonArray meanArr = obj.value( QStringLiteral( "mean" ) ).toArray();
  const QJsonArray stdArr = obj.value( QStringLiteral( "std" ) ).toArray();
  const QJsonArray minArr = obj.value( QStringLiteral( "min" ) ).toArray();
  const QJsonArray maxArr = obj.value( QStringLiteral( "max" ) ).toArray();

  // min/max must match mean's length: toJson() always emits four equal
  // arrays, so a short one means a truncated or foreign sidecar. Silently
  // defaulting to 0.0/1.0 would rescale every band with range=1, offset=0
  // under MinMax — a silent drift from the training-time statistics. Fail
  // closed, same discipline as fit's non-finite rejection.
  if ( meanArr.isEmpty() || meanArr.size() != stdArr.size()
       || minArr.size() != meanArr.size() || maxArr.size() != meanArr.size() )
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
    mMin[i] = minArr[i].toDouble();
    mMax[i] = maxArr[i].toDouble();
  }
  mFitted = true;
  return true;
}
