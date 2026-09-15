// registration_types.h — F13 shared contracts for the multimodal
// registration module (src/processing/algorithms/registration).
//
// Failure semantics are part of the contract: every registration product
// carries a RegistrationStatus. Cross-modal matchers must return
// LowConfidence or Refused — never a silently wrong alignment — when the
// evidence does not support a transform (see docs/processing/
// geometric-registration.md, "Refusal semantics").
//
// Numeric layer: no Qt GUI types. QString is used only for stable
// machine-readable reason codes, mirroring rs::core::GcpPoint.
#pragma once

#include <QString>

#include <vector>

namespace sicnu::registration {

// Lifecycle status of a registration product (match set, transform, warp,
// stack solution). Failed is reserved for internal errors (I/O, allocation);
// Refused means the inputs are structurally insufficient (too few matches,
// degenerate geometry, cap exhaustion); LowConfidence means a transform was
// produced but its evidence score is below the requested trust threshold.
enum class RegistrationStatus {
    Success,
    LowConfidence,
    Refused,
    Failed
};

// Stable snake_case reason codes (documented in
// docs/processing/geometric-registration.md). Unknown codes must be
// surfaced verbatim by UI/agent layers — never silently dropped.
//
//   too_few_matches       fewer usable correspondences than the requested
//                         model requires
//   flat_region           matched windows carry no structure (phase-correlation
//                         peak SNR / MI below the structure floor)
//   low_peak_snr          phase-correlation peak indistinguishable from noise
//   insufficient_coverage matched points cluster in too small a fraction of
//                         the overlap area
//   low_consensus         consensus (inlier) ratio below 0.5 — a minority of
//                         candidates agree on one transform
//   degenerate_geometry   fit aborted: collinear/degenerate control geometry
//   model_not_justified   RPC bias layer kept the simpler model (held-out
//                         evidence did not justify the affine upgrade)
//   cancelled             cooperative cancellation observed
//   cap_exhausted         bounded queue/cap hit before completion
//   io_error              input rasters could not be read

struct RegistrationPoint {
    double srcX{0.0};
    double srcY{0.0};
    double dstX{0.0};
    double dstY{0.0};
    double score{0.0};    // match quality in [0, 1] (metric-normalized)
    double residual{0.0}; // post-fit residual magnitude (pixels), filled later
    bool inlier{false};
};

// Shared bound overrides used by every bounded entry point in this module.
struct ResourceBounds {
    int maxMatches{4000};        // hard cap on retained tie points
    int maxPyramidLevels{6};     // coarse-to-fine depth ceiling
    double maxWorkingMiB{512.0}; // per-call scratch budget ceiling
};

[[nodiscard]] QString statusToString(RegistrationStatus status);

} // namespace sicnu::registration
