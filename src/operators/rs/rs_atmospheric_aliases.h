/***************************************************************************
 * rs_atmospheric_aliases.h  —  Atomic atmospheric correction operators
 *
 * rs:dn_to_radiance / rs:atmospheric_dos1 / rs:atmospheric_dos2 /
 * rs:atmospheric_quac — atomic method operators sharing the AtmosphericCorrection
 * computational kernels. The legacy rs:atmospheric_correction operator remains
 * the multi-method selector facade.
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * rs:dn_to_radiance — Calibrate raw digital numbers (DN) to spectral radiance.
 */
class RsDnToRadianceOperator : public RSOperator {
public:
    std::string name() const override { return "rs:dn_to_radiance"; }
    std::string displayName() const override { return "DN to Radiance"; }
    std::string group() const override { return "radiometric"; }
    std::string description() const override {
        return "Calibrate raw Digital Numbers (DN) to spectral radiance using sensor gain and bias.";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override {
        return RSOperatorMemoryPolicy::Streaming;
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

/**
 * rs:atmospheric_dos1 — Dark Object Subtraction 1 (DOS1) atmospheric correction.
 */
class RsAtmosphericDos1Operator : public RSOperator {
public:
    std::string name() const override { return "rs:atmospheric_dos1"; }
    std::string displayName() const override { return "Atmospheric Correction DOS1"; }
    std::string group() const override { return "radiometric"; }
    std::string description() const override {
        return "Dark Object Subtraction 1 (DOS1) atmospheric correction to estimate surface reflectance.";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override {
        return RSOperatorMemoryPolicy::MultiPassStreaming;
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

/**
 * rs:atmospheric_dos2 — Dark Object Subtraction 2 (DOS2) atmospheric correction.
 */
class RsAtmosphericDos2Operator : public RSOperator {
public:
    std::string name() const override { return "rs:atmospheric_dos2"; }
    std::string displayName() const override { return "Atmospheric Correction DOS2"; }
    std::string group() const override { return "radiometric"; }
    std::string description() const override {
        return "Dark Object Subtraction 2 (DOS2) atmospheric correction incorporating solar zenith and transmittance.";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override {
        return RSOperatorMemoryPolicy::MultiPassStreaming;
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

/**
 * rs:atmospheric_quac — Quick Atmospheric Correction (QUAC) multi-band retrieval.
 */
class RsAtmosphericQuacOperator : public RSOperator {
public:
    std::string name() const override { return "rs:atmospheric_quac"; }
    std::string displayName() const override { return "Atmospheric Correction QUAC"; }
    std::string group() const override { return "radiometric"; }
    std::string description() const override {
        return "Quick Atmospheric Correction (QUAC) multi-band scene-statistics surface reflectance retrieval.";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override {
        return RSOperatorMemoryPolicy::FullRaster;
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value estimateExecution(const Json::Value& params) const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
