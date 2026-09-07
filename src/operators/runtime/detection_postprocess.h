// src/operators/runtime/detection_postprocess.h — declarative detection decode.
//
// Pure postprocessing for object-detection heads: decode a (C,N)/(N,C) raw
// output tensor into scored boxes in RASTER pixel coordinates, deterministic
// NMS, and cross-tile dedup (overlap windows re-detect the same object; the
// whole-raster NMS pass — plus exact-duplicate collapse — is the dedup).
// No GDAL/Qt here: everything is floats and in/out params, so the matrix is
// unit-testable without a raster stack. Geographic transforms live in the
// vector writer (detection_tile_engine), not in this module.
#pragma once

#include "operators/framework/model_catalog.h"


#include <opencv2/core.hpp>

#include <string>
#include <vector>

namespace sicnu::operators::runtime {

/// One decoded detection in RASTER pixel coordinates (top-left corner + size).
struct DetectionBox
{
  float x = 0.0f;
  float y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;
  int classId = 0;
  float confidence = 0.0f;
};

/// The decode contract is the manifest-owned ModelDetectionContract
/// (framework layer) — one vocabulary from model.json to postprocess.
using DetectionDecodeContract = sicnu::operators::ModelDetectionContract;

/// Decode ONE head tensor for ONE tile into raster-coordinate boxes.
/// @param output      raw head tensor, dims 3 (1, C, N) or (1, N, C); float32
/// @param contract    decode contract (layout/tensorLayout/threshold)
/// @param tileX/tileY core tile origin in raster pixels
/// @param scaleX/Y    fed→raster scale (resize to_input: fedPx * scale = rasterPx)
/// @param rasterW/H   bounds the boxes are clamped to
/// @param out         decoded boxes (appended), raster pixel coords
/// @returns empty on success, else a human-readable failure (bad shape/layout).
std::string decodeDetections( const cv::Mat &output, const DetectionDecodeContract &contract,
                              int tileX, int tileY, double scaleX, double scaleY,
                              int rasterW, int rasterH,
                              std::vector<DetectionBox> &out );

/// Deterministic greedy NMS over one class set: sort by confidence desc
/// (ties broken by classId, then x, y, w, h lexicographic — never by pointer
/// or insertion order), suppress IoU > threshold. All-boxes NMS (classes are
/// part of the tie-break key but do NOT gate suppression — overlapping boxes
/// of different classes suppress each other; cross-class duplicates are the
/// tile-overlap pathology this pass exists to remove).
std::vector<DetectionBox> nonMaxSuppression( const std::vector<DetectionBox> &boxes, double iouThreshold );

/// Cross-tile dedup: exact-duplicate collapse (bit-equal boxes from overlap
/// seams) followed by the whole-raster NMS. Bounded by contract.maxDetections
/// upstream — the accumulator refuses more, it never silently drops.
void dedupDetections( std::vector<DetectionBox> &boxes, double iouThreshold );

} // namespace sicnu::operators::runtime
