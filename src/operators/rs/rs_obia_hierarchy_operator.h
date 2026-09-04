/***************************************************************************
 * rs_obia_hierarchy_operator.h  — Hierarchical OBIA (build + classify)
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * rs:obia_hierarchy
 *
 * Two-level hierarchy (fine MeanShift + coarse Watershed + pixel-majority
 * parent link) with a classify leg, and — issue #663 — a rehydrate mode that
 * makes previously written outputs round-trippable:
 *
 *   Build mode (default): input + outputFine (+ optional outputCoarse,
 *   outputParents) run RsObjectHierarchy::buildLevels via the OTB CLI
 *   (fail-closed, no teaching fallback).
 *
 *   Rehydrate mode: labelsFine (+ optional labelsCoarse, parents CSV) load
 *   an existing hierarchy instead of re-segmenting — the outputs of a
 *   previous build are valid inputs, so interactive classify iterations and
 *   OTB-less machines can reuse a segmented hierarchy. output* write params
 *   are ignored in this mode.
 *
 *   Classify leg (training polygons XOR segmentClasses + outputClass):
 *   RsRoiLabeler majority labels or interactive {segmentId: classId} over
 *   the classifyLevel, F2a features (spectral+GLCM+shape + childCount +
 *   areaRatioToParent), RsObjectClassify core, RsClassRaster::paint with an
 *   optional classColors palette. Result carries training accuracy; an
 *   optional outputUncertainty CSV carries per-segment entropy.
 *
 * Returns: outputFine, fineSegments, coarseSegments, labeledSegments,
 * levels, accuracy{...}?, outputClass?/outputCoarse?/outputParents?/
 * uncertaintyOutput?
 */
class RsObiaHierarchyOperator : public RSOperator {
public:
    std::string name() const override { return "rs:obia_hierarchy"; }
    std::string displayName() const override { return "OBIA Hierarchy (OTB)"; }
    std::string group() const override { return "obia"; }
    std::string description() const override {
        return "Build or reuse a two-level object hierarchy; optionally classify a level.";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
