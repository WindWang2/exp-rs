#pragma once
#include "qgis_analysis_export.h"
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

  private:
    bool mFitted = false;
    std::vector<double> mMean;
    std::vector<double> mStd;
    static constexpr double kMinStd = 1e-6;
};
