/***************************************************************************
 * rs_segmentation_utils.h  —  Shared OBIA segmentation helpers
 *
 * ADR 0060 — the cv::Mat teaching segmenter (segmentQuantize +
 * mergeSmallRegions), the label writer (writeLabelGeoTiff), the class writer
 * (writeClassGeoTiff, orphaned since ADR 0055) and the geometry rasterizer
 * (rasterizeGeometry, superseded by analysis RsPixelRasterizer) were
 * deleted; the operators delegate to RsSimpleSegmenter / RsRoiLabeler /
 * RsSegmentMap. Only the grid-superpixel fallback remains here.
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator_context.h"

#include <opencv2/core.hpp>

namespace sicnu::operators::rs::segutil {

/** Regular grid superpixels, labels 1..N (CV_32S). */
cv::Mat segmentGrid(int width, int height, int cellSize, RSOperatorContext& context);

} // namespace sicnu::operators::rs::segutil
