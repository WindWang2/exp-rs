/***************************************************************************
 * rs_spectral_detection_operators.h — matched filter + ACE + CEM detectors
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * rs:matched_filter — signed matched filter (t−μ)ᵀΣ⁻¹(x−μ) of a target
 * spectrum against the streamed scene background (Milestone C).
 *
 * rs:ace — adaptive coherence/cosine estimator, squared whitened cosine
 * between target and pixel, output in [0, 1] (Milestone C).
 *
 * Shared contract:
 *   input  (string, required)        Multi-band input raster
 *   output (string, required)        Single-band score raster (Float32)
 *   target (array of numbers, req.)  Target spectrum, one value per band
 *
 * Background statistics (mean, covariance) stream from the input with the
 * RX operator's valid-pixel predicate (non-finite or declared-NoData pixels
 * excluded); three passes, O(tile + bands²) memory, bit-exact grade.
 * Thresholding is a downstream step (rs:threshold_raster).
 */
class RsMatchedFilterOperator : public RSOperator {
public:
    std::string name() const override { return "rs:matched_filter"; }
    std::string displayName() const override { return "Matched Filter"; }
    std::string group() const override { return "spectral"; }
    std::string description() const override {
        return "Matched filter detection of a target spectrum against the scene "
               "background: signed whitened projection per pixel.";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override
    {
        return RSOperatorMemoryPolicy::MultiPassStreaming;
    }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

class RsAceOperator : public RSOperator {
public:
    std::string name() const override { return "rs:ace"; }
    std::string displayName() const override { return "ACE Detector"; }
    std::string group() const override { return "spectral"; }
    std::string description() const override {
        return "Adaptive coherence estimator: squared whitened cosine between a "
               "target spectrum and each pixel, in [0, 1].";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override
    {
        return RSOperatorMemoryPolicy::MultiPassStreaming;
    }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

/**
 * rs:cem_detection — Constrained Energy Minimization target detector
 * (Spectral Intelligence 12.0): w = R⁻¹t/(tᵀR⁻¹t) against the streamed
 * scene CORRELATION matrix (second moments, not mean-centered), score
 * wᵀx per pixel; the target itself scores exactly 1.
 *
 * Extra parameters over the shared contract:
 *   loading (number, optional, >= 0, default 0)
 *       Scaled diagonal loading alpha in R + alpha·(tr(R)/B)·I.
 * Fail-closed: scenes with fewer valid background samples than
 * SpectralCem::minSamplesRequired(bands, loading > 0) are refused
 * (2B+2 without loading, B+1 with).
 */
class RsCemOperator : public RSOperator {
public:
    std::string name() const override { return "rs:cem_detection"; }
    std::string displayName() const override { return "CEM Detector"; }
    std::string group() const override { return "spectral"; }
    std::string description() const override {
        return "Constrained energy minimization target detection against the "
               "scene correlation background: the target scores exactly 1.";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override
    {
        return RSOperatorMemoryPolicy::MultiPassStreaming;
    }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

/**
 * rs:tcimf_detection — Target-Constrained Interference-Minimized Filter
 * (Spectral Intelligence 13.0): the CEM companion with exact null
 * constraints on a set of undesired (interference) signatures —
 * w = R⁻¹[t − S(SᵀR⁻¹S)⁻¹SᵀR⁻¹t] / (…) against the streamed scene
 * CORRELATION matrix; the target scores exactly 1 and every interference
 * signature exactly 0. With no interference the filter is the CEM filter.
 *
 * Extra parameters over the shared contract:
 *   loading (number, optional, >= 0, default 0)
 *       Scaled diagonal loading alpha in R + alpha·(tr(R)/B)·I.
 *   interference (array of spectra, required) / interferenceRef (path)
 *       Undesired signatures: one spectrum per input band, through the same
 *       reference seam as the target (inline array-of-arrays, spectral-table
 *       artifact or library JSON path). 'libraryPath' stays reserved for the
 *       target; use interferenceRef for library-backed interference.
 *
 * Same fail-closed background floor as CEM (2B+2 valid samples, B+1 with
 * loading); linearly dependent interference signatures under the background
 * metric, and a target inside the interference span, are typed refusals.
 */
class RsTcimfOperator : public RSOperator {
public:
    std::string name() const override { return "rs:tcimf_detection"; }
    std::string displayName() const override { return "TCIMF Detector"; }
    std::string group() const override { return "spectral"; }
    std::string description() const override {
        return "Target-constrained interference-minimized filter: CEM with exact "
               "null constraints on undesired signatures (target scores 1, "
               "interference scores 0).";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override
    {
        return RSOperatorMemoryPolicy::MultiPassStreaming;
    }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

/**
 * rs:osp_detection — Orthogonal Subspace Projection target detector
 * (Spectral Intelligence 13.0): w = (I − U(UᵀU)⁻¹Uᵀ)d, score wᵀx per pixel;
 * every undesired signature scores exactly 0. Unlike the covariance-based
 * detectors OSP consumes NO background statistics (the undesired subspace is
 * an input), so it runs a single scoring pass with no under-sampling refusal.
 * Scores are signed and scale with the target magnitude (documented, unlike
 * the brightness-invariant ACE/CEM).
 *
 * Extra parameters over the shared contract:
 *   interference (array of spectra, required) / interferenceRef (path)
 *       Undesired signatures, same seam as TCIMF.
 */
class RsOspOperator : public RSOperator {
public:
    std::string name() const override { return "rs:osp_detection"; }
    std::string displayName() const override { return "OSP Detector"; }
    std::string group() const override { return "spectral"; }
    std::string description() const override {
        return "Orthogonal subspace projection target detection: suppresses the "
               "undesired signature subspace and projects the target onto what "
               "remains; interference scores exactly 0.";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override
    {
        return RSOperatorMemoryPolicy::Streaming;
    }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
