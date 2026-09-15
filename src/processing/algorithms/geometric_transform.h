// geometric_transform.h — D14 Package B: closed-form geometric transform
// solving (ADR 0159).
//
// Least-squares solving strategy:
//   - Affine and Polynomial2/3: Hartley-normalized design matrices solved by
//     a one-sided Jacobi SVD pseudo-inverse; polynomial coefficients are
//     expanded back to real image coordinates before being reported.
//   - Translation / Rigid / Similarity: closed-form Procrustes solutions
//     (exact for noise-free data, least squares otherwise).
//   - Projective: DLT on Hartley-normalized correspondences, smallest
//     singular vector, denormalized homography.
//
// Coefficient layouts (forward and backward):
//   Translation  : [tx, ty]
//   Rigid        : [theta, tx, ty]                (rotation + translation)
//   Similarity   : [scale, theta, tx, ty]         (Helmert)
//   Affine       : [a0,a1,a2, b0,b1,b2]           x=a0+a1u+a2v, y=b0+b1u+b2v
//   Polynomial2  : [a0..a5, b0..b5]               basis {1,u,v,u²,uv,v²}
//   Polynomial3  : [a0..a9, b0..b9]               basis adds {u³,u²v,uv²,v³}
//   Projective   : [h11,h12,h13,h21,h22,h23,h31,h32,h33], h33 normalized to 1
#pragma once

#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace rs::algorithms {

enum class TransformModel {
    Translation, // 2 params: tx, ty
    Rigid,       // 3 params: theta, tx, ty (orthonormal, no scale)
    Similarity,  // 4 params: Helmert (scale, rotation, tx, ty)
    Affine,      // 6 params: a0..a2, b0..b2
    Polynomial2, // 12 params: 2nd order polynomial (6 terms for X, 6 for Y)
    Polynomial3, // 20 params: 3rd order polynomial (10 terms for X, 10 for Y)
    Projective   // 8 dof: Direct Linear Transformation (DLT) homography
};

struct TransformResult {
    TransformModel model{TransformModel::Affine};
    std::vector<double> forwardCoeffs;  // (u, v) -> (x, y)
    std::vector<double> backwardCoeffs; // (x, y) -> (u, v)
    double rmseForward{0.0};
    double rmseBackward{0.0};
    double conditionNumber{0.0}; // kappa of the (normalized) design system
    bool success{false};
};

class GeometricTransform {
  public:
    // Minimum correspondence count: Translation 1, Rigid/Similarity 2,
    // Affine 3, Polynomial2 6, Polynomial3 10, Projective 4.
    [[nodiscard]] static int minPointsRequired(TransformModel model) noexcept;

    /// Solve the model from source->target correspondences. Throws
    /// std::invalid_argument on size mismatch, too few points, or non-finite
    /// coordinates. Returns success=false (with no coefficients) when the
    /// system is singular or kappa > 1e14.
    static TransformResult solve(TransformModel model,
                                 const std::vector<std::pair<double, double>>& sourcePts,
                                 const std::vector<std::pair<double, double>>& targetPts);

    // Coordinate mapping. Empty (failed) results map identically.
    static std::pair<double, double> applyForward(const TransformResult& res, double u, double v);
    static std::pair<double, double> applyBackward(const TransformResult& res, double x, double y);

    // Batch forward mapping; all spans must have equal length (no-op else).
    static void applyForwardBatch(const TransformResult& res,
                                  std::span<const double> srcU,
                                  std::span<const double> srcV,
                                  std::span<double> dstX,
                                  std::span<double> dstY);
};

} // namespace rs::algorithms
