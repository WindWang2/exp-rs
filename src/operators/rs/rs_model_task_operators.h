/***************************************************************************
 * rs_model_task_operators.h — task-shaped adapters over the model
 * execution service (Platform 4.0).
 *
 * rs:segment / rs:detect / rs:embedding are NOT second inference paths:
 * each is a thin JSON adapter whose defaults come from the model manifest
 * and whose entire execution is runModelInference(). rs:infer remains the
 * generic raster surface; the task operators exist so agents and workflows
 * declare INTENT (segment vs detect vs embed) instead of format knobs.
 ***************************************************************************/
#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::rs {

/// Semantic segmentation: probability stack (default) or argmax labels /
/// binary mask / confidence band via output.format.
class RsSegmentOperator : public RSOperator {
public:
    std::string name() const override { return "rs:segment"; }
    std::string displayName() const override { return "Semantic Segmentation (Model)"; }
    std::string group() const override { return "ml"; }
    std::string description() const override {
        return "Run a segmentation model on a raster; output format defaults to the manifest (probability stack, argmax labels, binary mask or confidence band).";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override { return RSOperatorMemoryPolicy::Streaming; }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value estimateExecution( const Json::Value &params ) const override;
    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override;
};

/// Object detection: tiled decode → NMS → tile dedup → georeferenced vector
/// (GPKG / GeoJSON / Shapefile by extension).
class RsDetectOperator : public RSOperator {
public:
    std::string name() const override { return "rs:detect"; }
    std::string displayName() const override { return "Object Detection (Model)"; }
    std::string group() const override { return "ml"; }
    std::string description() const override {
        return "Run a detection model (output.detection contract) over a raster; publishes georeferenced detection boxes as vector output with NMS tile dedup.";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override { return RSOperatorMemoryPolicy::Streaming; }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value estimateExecution( const Json::Value &params ) const override;
    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override;
};

/// Feature embedding: feeds the raster through an embedding model and writes
/// the C'-channel feature stack (optionally aggregated to a mean vector).
class RsEmbeddingOperator : public RSOperator {
public:
    std::string name() const override { return "rs:embedding"; }
    std::string displayName() const override { return "Feature Embedding (Model)"; }
    std::string group() const override { return "ml"; }
    std::string description() const override {
        return "Run an embedding model on a raster; writes the feature stack (aggregate=mean adds a per-scene mean feature vector to the result).";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override { return RSOperatorMemoryPolicy::Streaming; }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value estimateExecution( const Json::Value &params ) const override;
    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override;
};

// --- Platform 10.0 task families (typed artifact contracts) ------------------

/// Scene classification: ONE chip/scene, ONE forward pass, typed JSON
/// artifact (exp-rs-classification/1: predicted class + probability
/// distribution + model identity + input fingerprint). Requires a model
/// whose canonical EO task is "classification".
class RsClassifyOperator : public RSOperator {
public:
    std::string name() const override { return "rs:classify"; }
    std::string displayName() const override { return "Scene Classification (Model)"; }
    std::string group() const override { return "ml"; }
    std::string description() const override {
        return "Classify a scene/chip with a classification model (single forward pass); publishes a typed classification JSON artifact with the predicted class and probability distribution.";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override { return RSOperatorMemoryPolicy::Streaming; }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value estimateExecution( const Json::Value &params ) const override;
    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override;
};

/// Change detection: two co-registered dates through a siamese/change model
/// (multi-input seam), change-probability stack output. Requires a model
/// whose canonical EO task is "change_detection".
class RsChangeOperator : public RSOperator {
public:
    std::string name() const override { return "rs:change"; }
    std::string displayName() const override { return "Change Detection (Model)"; }
    std::string group() const override { return "ml"; }
    std::string description() const override {
        return "Detect change between two co-registered dates with a siamese/change model; publishes the change-probability stack (band semantics from the manifest classes).";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override { return RSOperatorMemoryPolicy::Streaming; }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value estimateExecution( const Json::Value &params ) const override;
    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override;
};

/// Regression: continuous-value raster (e.g. biomass, height, PM2.5
/// surface). Probability-stack single-band semantics over the same engine.
/// Requires a model whose canonical EO task is "regression".
class RsRegressOperator : public RSOperator {
public:
    std::string name() const override { return "rs:regress"; }
    std::string displayName() const override { return "Continuous Regression (Model)"; }
    std::string group() const override { return "ml"; }
    std::string description() const override {
        return "Run a regression model on a raster; publishes the continuous-value output band(s) (one per manifest output channel).";
    }
    RSOperatorMemoryPolicy memoryPolicy() const override { return RSOperatorMemoryPolicy::Streaming; }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value estimateExecution( const Json::Value &params ) const override;
    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override;
};

} // namespace sicnu::operators::rs
