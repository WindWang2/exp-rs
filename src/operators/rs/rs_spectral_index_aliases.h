/***************************************************************************
 * rs_spectral_index_aliases.h  —  Atomic spectral index operators
 *
 * rs:ndvi / rs:evi / rs:ndwi / rs:savi / rs:ndbi / rs:mndwi — atomic
 * spectral index operators sharing the SpectralIndices computational kernels.
 * The legacy rs:spectral_index operator remains the multi-index selector facade.
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * rs:ndvi — Normalized Difference Vegetation Index.
 */
class RsNdviOperator : public RSOperator {
public:
    std::string name() const override { return "rs:ndvi"; }
    std::string displayName() const override { return "Normalized Difference Vegetation Index (NDVI)"; }
    std::string group() const override { return "spectral"; }
    std::string description() const override {
        return "Compute Normalized Difference Vegetation Index: (NIR - Red) / (NIR + Red).";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

/**
 * rs:evi — Enhanced Vegetation Index.
 */
class RsEviOperator : public RSOperator {
public:
    std::string name() const override { return "rs:evi"; }
    std::string displayName() const override { return "Enhanced Vegetation Index (EVI)"; }
    std::string group() const override { return "spectral"; }
    std::string description() const override {
        return "Compute Enhanced Vegetation Index: 2.5 * (NIR - Red) / (NIR + 6*Red - 7.5*Blue + 1).";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

/**
 * rs:ndwi — Normalized Difference Water Index.
 */
class RsNdwiOperator : public RSOperator {
public:
    std::string name() const override { return "rs:ndwi"; }
    std::string displayName() const override { return "Normalized Difference Water Index (NDWI)"; }
    std::string group() const override { return "spectral"; }
    std::string description() const override {
        return "Compute Normalized Difference Water Index: (Green - NIR) / (Green + NIR).";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

/**
 * rs:savi — Soil-Adjusted Vegetation Index.
 */
class RsSaviOperator : public RSOperator {
public:
    std::string name() const override { return "rs:savi"; }
    std::string displayName() const override { return "Soil-Adjusted Vegetation Index (SAVI)"; }
    std::string group() const override { return "spectral"; }
    std::string description() const override {
        return "Compute Soil-Adjusted Vegetation Index: ((NIR - Red) / (NIR + Red + 0.5)) * 1.5.";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

/**
 * rs:ndbi — Normalized Difference Built-up Index.
 */
class RsNdbiOperator : public RSOperator {
public:
    std::string name() const override { return "rs:ndbi"; }
    std::string displayName() const override { return "Normalized Difference Built-up Index (NDBI)"; }
    std::string group() const override { return "spectral"; }
    std::string description() const override {
        return "Compute Normalized Difference Built-up Index: (SWIR - NIR) / (SWIR + NIR).";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

/**
 * rs:mndwi — Modified Normalized Difference Water Index.
 */
class RsMndwiOperator : public RSOperator {
public:
    std::string name() const override { return "rs:mndwi"; }
    std::string displayName() const override { return "Modified Normalized Difference Water Index (MNDWI)"; }
    std::string group() const override { return "spectral"; }
    std::string description() const override {
        return "Compute Modified Normalized Difference Water Index: (Green - SWIR) / (Green + SWIR).";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
