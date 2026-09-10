// ir_mad_kernels.h — Iteratively Reweighted MAD change-detection math
// (Foundation 7.0 primitive consolidation).
//
// Single owner of the two IR-MAD helpers that previously existed as
// verbatim mirrors (change_detection.cpp anonymous namespace and
// rs_change_streaming.cpp) — the streaming path must reproduce the
// kernel's weights bit-for-bit, which is only guaranteed when both call
// the same compiled function (pinned by the IR-MAD streaming-parity
// tests in tests/test_change_detection.cpp).
//
// This header is consumed only by TUs that already use OpenCV (the MAD
// solver and the streaming mirror); it deliberately stays out of
// change_detection.h to keep that public surface OpenCV-free.
#pragma once

#include <opencv2/core.hpp>

namespace ChangeDetectionMAD
{

/// Chi-square survival function P(X_k > x) (regularized upper incomplete
/// gamma over the k/2 shape). Closed forms for k = 1 and k = 2; series /
/// continued-fraction expansion elsewhere; result clamped to [0, 1].
double chiSquareUpperCdf( double k, double x );

/// SVD-based symmetric square-root inverse; eigenvalues <= 1e-12 are zeroed
/// (the singular-value floor of the MAD whitening transform).
cv::Mat madSqrtInv( const cv::Mat &M );

} // namespace ChangeDetectionMAD
