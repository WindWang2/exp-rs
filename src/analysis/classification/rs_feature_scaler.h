#pragma once
#include "qgis_analysis_export.h"
#include <QJsonObject>
#include <QString>
#include <opencv2/core.hpp>
#include <vector>

class QGIS_ANALYSIS_EXPORT RsFeatureScaler
{
  public:
    bool fit( const cv::Mat &trainX );
    cv::Mat transform( const cv::Mat &X ) const;
    bool isFitted() const { return mFitted; }
    int bandCount() const { return static_cast<int>( mMean.size() ); }
    bool saveJson( const QString &path ) const;
    bool loadJson( const QString &path );
    /// Serialise to {version, mean[], std[]} for embedding in a larger JSON
    /// document (ADR 0019 S2 superset model sidecar). Invalid when unfitted.
    QJsonObject toJson() const;
    /// Inverse of toJson(); returns false and resets state when invalid.
    bool fromJson( const QJsonObject &obj );

  private:
    bool mFitted = false;
    std::vector<double> mMean;
    std::vector<double> mStd;
    static constexpr double kMinStd = 1e-6;
};
