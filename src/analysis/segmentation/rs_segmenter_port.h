// rs_segmenter_port.h — Injected segmenter port for hierarchical OBIA buildLevels.
#pragma once

#include "qgis_analysis_export.h"
#include "rs_segment_map.h"

#include <QString>
#include <QVector>

#include <functional>

/// Per-level segmentation specification (H1 independent OTB run).
struct QGIS_ANALYSIS_EXPORT RsLevelSpec
{
    enum class Filter
    {
        MeanShift,
        Watershed
    };

    Filter filter = Filter::MeanShift;
    QString name; // optional human label (e.g. "fine", "coarse")

    // MeanShift parameters
    int spatialRadius = 5;
    double rangeRadius = 15.0;
    int minRegionSize = 100;
    int maxIterations = 100;
    double threshold = 0.1;

    // Watershed parameters
    double watershedThreshold = 0.01;
};

struct QGIS_ANALYSIS_EXPORT RsSegmenterResult
{
    bool ok = false;
    QString errorMessage;
    RsSegmentMap segMap;
};

/// Injected segmenter (no OTB CLI strings inside hierarchy core).
class QGIS_ANALYSIS_EXPORT RsSegmenterPort
{
  public:
    virtual ~RsSegmenterPort() = default;

    virtual RsSegmenterResult segment(
        const QString &rasterPath,
        const RsLevelSpec &spec,
        const std::function<bool()> &isCanceled = nullptr ) = 0;
};
