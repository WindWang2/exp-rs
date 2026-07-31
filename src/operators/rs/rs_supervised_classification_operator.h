/***************************************************************************
 * rs_supervised_classification_operator.h  —  Supervised pixel classification
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/**
 * rs:supervised_classification
 *
 * ADR 0019 slice S3 — thin JSON adapter over RsTrainingDataExtraction +
 * RsClassificationPipeline (src/analysis/classification). Tiled prediction,
 * dtype escalation, NoData policy, scaler transform and the superset
 * .meta.json sidecar all come from the pipeline (GUI parity).
 *
 * Modes:
 *   A) Train+predict: provide `training` polygons (classField integer).
 *   B) Predict-only:  provide `modelIn` (OpenCV-saved model from modelOut);
 *      the superset sidecar's scaler is applied automatically when present.
 *
 * Methods (OpenCV ml): svm | normal_bayes
 *
 * Parameters:
 *   input       (string, required)
 *   output      (string, required)
 *   training    (string, required unless modelIn set)
 *   modelIn     (string, optional) load model, skip training
 *   modelOut    (string, optional) save model after training
 *   method      (string, optional) svm | normal_bayes (default svm)
 *   scale       (boolean, optional) fit feature scaler on training split,
 *               embed in sidecar (default true, matching the GUI)
 *   testSplit   (number, optional) stratified holdout fraction 0-0.9
 *               (default 0 = off); adds accuracy keys to the result
 *   classField, bands, maxSamplesPerClass — training mode only
 */
class RsSupervisedClassificationOperator : public RSOperator {
public:
    std::string name() const override { return "rs:supervised_classification"; }
    std::string displayName() const override { return "Supervised Classification"; }
    std::string group() const override { return "classification"; }
    std::string description() const override {
        return "Train or apply SVM/NormalBayes classification on multi-band rasters.";
    }

    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value run(const Json::Value& params, RSOperatorContext& context) override;
};

} // namespace sicnu::operators::rs
