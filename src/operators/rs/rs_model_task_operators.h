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

} // namespace sicnu::operators::rs
