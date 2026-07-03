// rs_obia_segmentation.h — Shared OTB / SimpleSegmenter segmentation for OBIA.
#pragma once

#include "rs_segment_map.h"

#include <QString>
#include <QVector>

#include <functional>

struct RsObiaSegmentationConfig
{
    QString rasterPath;
    QVector<int> bandIndices; // 1-based GDAL band numbers

    bool preferOtb = true;
    int spatialRadius = 5;
    int rangeRadius = 15;
    int minRegionSize = 100;
    int maxIteration = 100;

    int smoothKernel = 5;
    int quantizeBins = 32;
};

struct RsObiaSegmentationResult
{
    bool ok = false;
    bool usedOtb = false;
    QString errorMessage;
    RsSegmentMap segMap;
};

class RsObiaSegmentation
{
  public:
    static bool isOtbAvailable();

    /// Run segmentation: OTB MeanShift when available (and preferOtb), else SimpleSegmenter.
    static RsObiaSegmentationResult run(
        const RsObiaSegmentationConfig &cfg,
        const std::function<bool()> &isCanceled = nullptr );
};