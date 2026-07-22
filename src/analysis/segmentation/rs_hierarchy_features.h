// rs_hierarchy_features.h — F2a feature matrix on a hierarchy level.
//
// Spectral + shape (from flat RsSegmentFeatures) + childCount + areaRatioToParent.
// parentId is metadata only (not a feature column). Orphan rows kept with
// zeroed inter-level fields.
#pragma once

#include "qgis_analysis_export.h"
#include "rs_object_hierarchy.h"
#include "rs_segment_features.h"

#include <QString>
#include <QVector>

#ifdef SICNU_HAS_OPENCV
#include <opencv2/core.hpp>
#endif

struct QGIS_ANALYSIS_EXPORT RsFeatureMatrixMeta
{
    QVector<quint32> segmentIds;
    QVector<quint32> parentIds; // parallel to segmentIds; 0 = orphan / no parent
};

struct QGIS_ANALYSIS_EXPORT RsFeatureMatrixResult
{
    bool ok = false;
    QString errorMessage;
    RsFeatureMatrixMeta meta;
    /// Number of base spectral/shape columns (before childCount + areaRatio).
    int spectralShapeCols = 0;
#ifdef SICNU_HAS_OPENCV
    /// Columns: [spectral/shape…] + childCount + areaRatioToParent
    cv::Mat X;
#endif
};

class QGIS_ANALYSIS_EXPORT RsHierarchyFeatures
{
  public:
    /// Build F2a matrix for hierarchy.level(levelIndex).
    /// Does not require live OTB when hierarchy is fixture-installed.
    static RsFeatureMatrixResult buildFeatureMatrix(
        const RsObjectHierarchy &hierarchy,
        const QString &rasterPath,
        int levelIndex,
        const QVector<int> &bandIndices );

    /// Append inter-level columns onto an existing flat feature matrix.
    /// X must have one row per segmentIds entry in the same order.
    /// parentIds filled as metadata.
#ifdef SICNU_HAS_OPENCV
    static RsFeatureMatrixResult appendInterLevelFeatures(
        const cv::Mat &flatX,
        const QVector<quint32> &segmentIds,
        const RsObjectHierarchy &hierarchy,
        int levelIndex );
#endif
};
