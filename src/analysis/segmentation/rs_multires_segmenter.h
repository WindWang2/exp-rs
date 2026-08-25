// rs_multires_segmenter.h — Native Baatz-Schäpe (FNEA) Multiresolution Segmentation Engine.
#pragma once

#include "qgis_analysis_export.h"
#include "rs_segment_map.h"
#include "rs_segmenter_port.h"

#include <QString>
#include <QVector>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

/**
 * @brief Hyperparameters for Baatz-Schäpe Multiresolution Segmentation.
 *
 * Total fusion cost:
 *   f = (1.0 - shapeWeight) * deltaH_color + shapeWeight * deltaH_shape
 *
 * Shape heterogeneity:
 *   deltaH_shape = compactnessWeight * deltaH_compactness + (1.0 - compactnessWeight) * deltaH_smoothness
 *
 * Merge condition:
 *   f <= scale * scale (S^2)
 */
struct QGIS_ANALYSIS_EXPORT RsMultiresParams
{
    double scale = 20.0;             ///< Scale parameter threshold S (> 0)
    double shapeWeight = 0.2;        ///< Weight for shape heterogeneity w_shape in [0, 1] (w_color = 1 - w_shape)
    double colorWeight = 0.8;        ///< Weight for spectral color heterogeneity w_color in [0, 1]
    double compactnessWeight = 0.5;  ///< Weight for compactness w_comp in [0, 1] (w_smooth = 1 - w_comp)
    int minRegionSize = 1;           ///< Minimum mapping unit in pixels
    int minSegmentSize = 1;          ///< Alias for minRegionSize
    std::vector<double> bandWeights; ///< Per-band spectral weights (empty = uniform)

    /// Effective shape weight normalized to [0, 1].
    double effectiveShapeWeight() const
    {
        if ( shapeWeight >= 0.0 && shapeWeight <= 1.0 )
            return shapeWeight;
        return 0.2;
    }

    /// Effective compactness weight normalized to [0, 1].
    double effectiveCompactnessWeight() const
    {
        if ( compactnessWeight >= 0.0 && compactnessWeight <= 1.0 )
            return compactnessWeight;
        return 0.5;
    }

    /// Effective minimum region size.
    int effectiveMinRegionSize() const
    {
        int sz = std::max( minRegionSize, minSegmentSize );
        return sz > 0 ? sz : 1;
    }
};

using MultiresParams = RsMultiresParams;

/**
 * @brief Native Baatz-Schäpe Multiresolution Segmentation Engine.
 *
 * Implements Region Adjacency Graph (RAG) topology, O(1) incremental
 * variance/heterogeneity calculations, Local Mutual Best Fit (LMBF)
 * bottom-up merging, and minimum region size post-processing.
 */
class QGIS_ANALYSIS_EXPORT RsMultiresSegmenter : public RsSegmenterPort
{
public:
    RsMultiresSegmenter() = default;
    ~RsMultiresSegmenter() override = default;

    /**
     * @brief Node in Region Adjacency Graph (RAG) tracking geometric and spectral moments.
     */
    struct SegmentNode
    {
        uint32_t id = 0;
        uint32_t area = 1;
        uint32_t perimeter = 4;
        uint32_t minX = 0;
        uint32_t minY = 0;
        uint32_t maxX = 0;
        uint32_t maxY = 0;
        std::vector<double> sumB;
        std::vector<double> sumSqB;
        std::vector<std::pair<uint32_t, uint32_t>> neighbors; // <neighbor_id, shared_border_len>
        bool active = true;
    };

    /**
     * @brief RsSegmenterPort implementation for hierarchical OBIA integration.
     */
    RsSegmenterResult segment(
        const QString &rasterPath,
        const RsLevelSpec &spec,
        const std::function<bool()> &isCanceled = nullptr ) override;

    /**
     * @brief Executes Baatz-Schäpe multiresolution segmentation over multi-band planar float buffers.
     *
     * @param bandData Array of pointers to float image buffers (each of size width * height)
     * @param bandCount Number of bands (M >= 1)
     * @param width Image width in pixels
     * @param height Image height in pixels
     * @param nodata NoData sentinel value
     * @param params Segmentation hyperparameters (scale, weights, MMU)
     * @param isCanceled Optional cancellation check callback
     * @param onProgress Optional progress callback [0.0 .. 1.0]
     * @return RsSegmentMap populated with 1-based segment IDs (0 = NoData)
     */
    static RsSegmentMap segment(
        const float *const *bandData,
        int bandCount,
        int width,
        int height,
        float nodata,
        const RsMultiresParams &params,
        const std::function<bool()> &isCanceled = {},
        const std::function<void(float)> &onProgress = {} );

    /**
     * @brief Vector-based overload with 64-bit coordinate safety.
     */
    static RsSegmentMap segment(
        const std::vector<const float*> &bands,
        int64_t width,
        int64_t height,
        float nodata,
        const RsMultiresParams &params,
        const std::function<bool()> &isCanceled = {},
        const std::function<void(float)> &onProgress = {} );

    /**
     * @brief Vector-based overload with atomic cancel flag and progress callback.
     */
    static RsSegmentMap segment(
        const std::vector<const float*> &bands,
        int64_t width,
        int64_t height,
        const RsMultiresParams &params,
        std::atomic<bool> *cancelFlag = nullptr,
        std::function<void(float)> progressCallback = nullptr );

    /**
     * @brief High-level entry point taking a GDAL raster file path.
     */
    static RsSegmentMap segmentRasterFile(
        const QString &rasterPath,
        const QVector<int> &bandIndices,
        const RsMultiresParams &params,
        const std::function<bool()> &isCanceled = {},
        const std::function<void(float)> &onProgress = {},
        QString *errorMessage = nullptr );
};

namespace rs::analysis {
    using RsMultiresSegmenter = ::RsMultiresSegmenter;
    using RsMultiresParams = ::RsMultiresParams;
    using MultiresParams = ::RsMultiresParams;
}
