/***************************************************************************
 * rs_obia_hierarchy_operator.h  — Hierarchical OBIA V1 pipeline entry (U4)
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * rs:obia_hierarchy
 *
 * Scripted hierarchical OBIA path using the same analysis library as the GUI:
 *   1. buildLevels(2): MeanShift fine + Watershed coarse via OTB, pixel-majority link
 *   2. Optional: F2a features + object classify + class raster paint
 *
 * Always writes fine-level label raster. Optionally writes coarse labels,
 * parent table CSV, and class raster when training is provided.
 *
 * Requires OTB for primary segmenters (clear error if missing — no silent fallback).
 */
class RsObiaHierarchyOperator : public RSOperator {
public:
    std::string name() const override { return "rs:obia_hierarchy"; }
    std::string displayName() const override { return "OBIA Hierarchical Segment/Classify"; }
    std::string group() const override { return "obia"; }
    std::string description() const override {
        return "Two-level OTB hierarchy (MeanShift+Watershed), parent-link, optional object classify.";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
