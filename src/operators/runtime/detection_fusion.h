// src/operators/runtime/detection_fusion.h — deterministic Weighted Boxes Fusion
// (WBF) for detection ensembles.
//
// An ensemble of detectors produces ONE box set per member, every member in the
// SAME raster-pixel frame (each member engine maps its own letterbox/tiling
// reverse transform back to raster pixels). This module fuses those sets into
// ONE product with the canonical Weighted Boxes Fusion algorithm
// (Solovyev et al. 2021, arXiv:1910.13302; reference implementation
// ZFTurbo/Weighted-Boxes-Fusion) — concatenation + NMS is NOT a fusion (it is
// dedup) and is deliberately not implemented here.
//
// Determinism: every ordering key is a total order over the box data
// (effective score, then class, then geometry); the fused output order is a
// pure function of the input multiset. No GDAL/Qt: the matrix is unit-testable
// with hand-computed answers. The full semantics (including the two documented
// deviations from the reference) live in docs/adr/0171.
#pragma once

#include "operators/runtime/detection_postprocess.h"

#include <cstddef>
#include <string>
#include <vector>

namespace sicnu::operators::runtime {

/// Detection-fusion contract (manifest `ensemble.detection`, combination
/// "wbf" only). Ranges are validated by the manifest parser AND at run time
/// (a programmatically built contract must not smuggle a bad threshold).
struct DetectionFusionContract
{
  /// Cluster-match IoU: a box joins the cluster whose current fused
  /// representative has the best IoU with it, when that IoU is STRICTLY
  /// greater than this threshold (same class only). Must be in (0, 1].
  double iouThreshold = 0.55;
  /// Raw-confidence gate applied BEFORE fusion: a box whose own confidence is
  /// below this threshold never enters a cluster. Unlike the reference
  /// implementation the gate reads the RAW confidence, not the
  /// member-weight-scaled score — a heavy member weight must not smuggle a
  /// low-confidence box past the gate. Must be in [0, 1).
  double skipBoxThreshold = 0.0;

  /// Vocabulary + range validation (empty = ok).
  std::string validate() const;
};

/// One member's contribution: its ensemble weight and its decoded boxes
/// (raster-pixel coordinates, already passed through the member's own decode,
/// seam rule and NMS).
struct DetectionMemberBoxes
{
  double weight = 1.0;
  std::vector<DetectionBox> boxes;
};

/// Fusion outcome: the fused boxes plus the statistics the provenance sidecar
/// records (so a product can always explain WHY it looks the way it does).
struct DetectionFusionResult
{
  std::vector<DetectionBox> boxes;          ///< fused product, deterministic order
  std::size_t boxesPooled = 0;              ///< boxes handed in across all members
  std::size_t boxesGated = 0;               ///< dropped by the confidence gate / non-finite
  std::size_t boxesFused = 0;               ///< boxes absorbed into fused clusters
  std::size_t clusters = 0;                 ///< fused boxes in the product
  /// Per-member counts, index-aligned with the input members:
  /// how many of the member's boxes survived the gate, and how many of those
  /// were absorbed into a fused cluster.
  std::vector<std::size_t> memberSurviving;
  std::vector<std::size_t> memberAbsorbed;
};

/// Fuses every member's box set into ONE product (WBF). An empty result is a
/// valid outcome (an ensemble that detects nothing publishes nothing).
/// Never throws for empty/degenerate input; a contract violation is a typed
/// error from the caller's manifest validation (see validate()).
DetectionFusionResult fuseDetectionsWbf( const std::vector<DetectionMemberBoxes> &members,
                                         const DetectionFusionContract &contract );

} // namespace sicnu::operators::runtime
