#pragma once
#include "qgis_analysis_export.h"
#include <QJsonObject>
#include <QString>
#include <opencv2/core.hpp>
#include <vector>

class QGIS_ANALYSIS_EXPORT RsFeatureScaler
{
  public:
    enum class Method
    {
      ZScore,
      MinMax
    };

    bool fit( const cv::Mat &trainX, Method method = Method::ZScore );
    cv::Mat transform( const cv::Mat &X ) const;
    bool isFitted() const { return mFitted; }
    int bandCount() const { return static_cast<int>( mMean.size() ); }
    Method method() const { return mMethod; }
    bool saveJson( const QString &path ) const;
    bool loadJson( const QString &path );
    /// Serialise to {version, method, mean[], std[], min[], max[]} for embedding in sidecar.
    QJsonObject toJson() const;
    /// Inverse of toJson(); returns false and resets state when invalid.
    bool fromJson( const QJsonObject &obj );

  private:
    bool mFitted = false;
    Method mMethod = Method::ZScore;
    std::vector<double> mMean;
    std::vector<double> mStd;
    std::vector<double> mMin;
    std::vector<double> mMax;
    static constexpr double kMinStd = 1e-6;
};
