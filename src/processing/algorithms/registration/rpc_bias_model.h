// rpc_bias_model.h — F13 Package D: RPC bias refinement math layer.
//
// Pure functions, no GDAL dependency: the caller feeds per-GCP bias samples
// (observed ground position minus RPC-predicted ground position, in the
// destination planar CRS) and gets a bias model that is either
//   Constant : (median biasX, median biasY)      — robust, >= 3 samples
//   Affine   : 6-parameter linear bias field     — >= 6 samples, and only
//              adopted when k-fold held-out RMSE beats the constant model
//              by minImprovement (relative). Otherwise the constant model
//              is kept (fail-closed to the pre-F13 behavior).
//
// The median — not the mean — constant keeps single-GCP outliers from
// shifting the bias (matches and preserves the D14 median-refinement
// semantics; see qgsrpcgcptransformer.cpp history).
//
// Height sensitivity: the caller supplies a reprojector closure
// (ground lon/lat + height -> reprojected ground) — typically the GDAL RPC
// transformer — and this layer turns it into a per-GCP dGround/dHeight
// estimate plus aggregate statistics for the quality report.
#pragma once

#include "registration_types.h"

#include <QString>
#include <array>
#include <functional>
#include <utility>
#include <vector>

namespace sicnu::registration {

enum class RpcBiasModelKind { Constant, Affine };

struct RpcBiasSample {
    double groundX{0.0}; // destination CRS planar coordinate
    double groundY{0.0};
    double biasX{0.0};   // observed - RPC-predicted
    double biasY{0.0};
};

struct RpcBiasOptions {
    int minSamplesConstant{3};
    int minSamplesAffine{6};
    double minImprovement{0.10}; // relative held-out gain for Affine
    int folds{4};
};

struct RpcBiasFit {
    RpcBiasModelKind kind{RpcBiasModelKind::Constant};
    bool applied{false}; // significance/improvement gate passed
    double constX{0.0};
    double constY{0.0};
    // Affine coefficients: biasX = a0 + a1·x + a2·y ; biasY = b0 + b1·x + b2·y
    std::array<double, 6> affine{0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    double rmseBefore{0.0};     // full-sample RMSE without bias correction
    double rmseAfter{0.0};      // full-sample RMSE with the fitted bias
    double heldoutRmseConstant{0.0}; // k-fold held-out, constant model
    double heldoutRmseAffine{0.0};   // k-fold held-out, affine model
    QString refusalReason;      // non-empty when !applied (too_few_matches |
                                // model_not_justified)
};

struct HeightSensitivityReport {
    double medianDxPerM{0.0};   // ground meters of image shift per meter of height error
    double medianDyPerM{0.0};
    double maxMagnitudePerM{0.0};
    int samples{0};
};

class RpcBiasModel {
  public:
    /// Fit the bias model. Samples below minSamplesConstant yield
    /// applied=false with reason too_few_matches.
    static RpcBiasFit fit(const std::vector<RpcBiasSample>& samples,
                          const RpcBiasOptions& options = {});

    /// Apply a fitted bias to a ground coordinate. Unapplied fits are the
    /// identity.
    static std::pair<double, double> apply(const RpcBiasFit& fit, double groundX, double groundY);

    /// Height sensitivity via finite differences on a caller-supplied
    /// reprojector: reproject(groundX, groundY, heightM) -> ground (typically
    /// RPC + DEM forward evaluation). heightStepM defaults to 10 m.
    static HeightSensitivityReport
    heightSensitivity(const std::vector<std::pair<double, double>>& groundPoints, double heightM,
                      const std::function<std::pair<double, double>(double, double, double)>&
                          reproject,
                      double heightStepM = 10.0);
};

} // namespace sicnu::registration
