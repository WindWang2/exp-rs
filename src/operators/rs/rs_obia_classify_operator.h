/***************************************************************************
 * rs_obia_classify_operator.h  —  Object-based classification (OBIA)
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * rs:obia_classify
 *
 * Object-based classification with two training sources and two feature
 * models (issue #663: the OBIA GUI's interactive classify flow —
 * precomputed objects, hand-labeled segments, full feature set, classifier
 * hyperparameters, training accuracy — is now expressible through this
 * contract instead of a GUI-owned kernel pipeline):
 *
 *   Training source (exactly one):
 *     training      — vector polygons; segments labeled by pixel majority
 *                     (RsRoiLabeler, ADR 0060)
 *     segmentClasses — {segmentId: classId} map over a provided `labels`
 *                     raster (interactive sessions, rs:obia_label output)
 *
 *   Segment geometry:
 *     labels        — existing label raster (skip internal segmentation;
 *                     REQUIRED with segmentClasses, optional with training)
 *     otherwise     — internal segmentation: segmentMethod grid (default)
 *                     or quantize (RsSimpleSegmenter, ADR 0060)
 *
 *   Feature model:
 *     features=mean (default) — per-band mean spectra (historical behavior)
 *     features=full           — RsSegmentFeatures spectral+GLCM+shape with a
 *                               per-family `featureSelection` mask (the
 *                               interactive GUI feature tree)
 *
 *   Classifier: svm | normal_bayes | random_forest | kmeans | mlp
 *   (RsClassifierBackendFactory, ADR 0061 — hyperparameters rfNumTrees /
 *   rfMaxDepth / rfMinSampleCount / mlpHiddenLayerSize / mlpMaxIter).
 *
 *   Output: class-id GeoTIFF via RsClassRaster::paint (palette when
 *   classColors given, dtype escalation, NoData=0, ADR 0054/0055), optional
 *   per-segment entropy CSV (outputUncertainty), training-set accuracy in
 *   the result.
 *
 * Returns: output, method, segments, labeledSegments, trainSamples, classes,
 * features, width, height, accuracy{overallAccuracy,kappa,classes,confusion,
 * producer,user,f1}?, uncertaintyOutput?
 */
class RsObiaClassifyOperator : public RSOperator {
public:
    std::string name() const override { return "rs:obia_classify"; }
    std::string displayName() const override { return "OBIA Classification"; }
    std::string group() const override { return "obia"; }
    std::string description() const override {
        return "Classify objects from polygon training or pre-labeled segments.";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
