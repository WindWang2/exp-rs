/***************************************************************************
 * rs_segmentation_utils.h  —  Shared OBIA segmentation helpers
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator_context.h"

#include <gdal.h>
#include <ogr_api.h>

#include <opencv2/core.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace sicnu::operators::rs::segutil {

/** Merge tiny islands into larger neighbors (in-place label map, 1-based IDs). */
void mergeSmallRegions(cv::Mat& labels, int minSize);

/** Regular grid superpixels, labels 1..N. */
cv::Mat segmentGrid(int width, int height, int cellSize, RSOperatorContext& context);

/**
 * Mean intensity → Gaussian smooth → quantize → connected components → merge.
 * bandData: per-band float vectors of size width*height.
 */
cv::Mat segmentQuantize(const std::vector<std::vector<float>>& bandData,
                        int width, int height,
                        int smoothKernel, int quantizeBins, int minRegionSize,
                        RSOperatorContext& context);

/** Rasterize one OGR geometry to Byte mask matching raster grid. */
std::vector<uint8_t> rasterizeGeometry(OGRGeometryH geom, int width, int height,
                                       const double gt[6]);

/** Write UInt32 label GeoTIFF, copying geotransform/projection from source. */
void writeLabelGeoTiff(const std::string& outputPath,
                       const cv::Mat& labels, // CV_32S
                       int width, int height,
                       const double gt[6],
                       const std::string& projectionWkt);

/**
 * Write single-band class map GeoTIFF from Int32 class ids (GDAL converts
 * to the band dtype). Caller picks \a type with the pipeline's escalation
 * policy (Byte only when all class ids fit 0..255 — never a silent clamp).
 * NoData is 0 (unclassified).
 */
void writeClassGeoTiff(const std::string& outputPath,
                       const std::vector<int32_t>& data,
                       int width, int height,
                       const double gt[6],
                       const std::string& projectionWkt,
                       GDALDataType type);

} // namespace sicnu::operators::rs::segutil
