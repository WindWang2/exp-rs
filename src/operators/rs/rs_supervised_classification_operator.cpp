/***************************************************************************
 * rs_supervised_classification_operator.cpp
 *
 * ADR 0019 slice S3 — thin JSON adapter over the analysis-layer
 * classification modules:
 *
 *   train mode:      params → RsTrainingDataExtraction (OGR vector) →
 *                    optional RsClassificationSplit stratified holdout →
 *                    optional RsFeatureScaler fit → RsClassificationPipeline
 *   predict-only:    params → loadModelSidecar + backend->load(modelIn) →
 *                    RsClassificationPipeline
 *
 * Tiled prediction, dtype escalation (no 0-255 clamp), NoData/ignore policy,
 * scaler transform, accuracy assessment and the superset .meta.json model
 * sidecar all come from the pipeline (parity with the GUI path by
 * construction). Backends are constructed via the analysis-layer
 * RsClassifierBackendFactory (ADR 0061 — the single method-name → backend
 * mapping), so hyperparameters cannot drift from the GUI again.
 ***************************************************************************/
#include "rs_supervised_classification_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include "rs_classification_pipeline.h"
#include "rs_classification_split.h"
#include "rs_classifier_backend_factory.h"
#include "rs_training_data_extraction.h"

#include <QString>
#include <QVector>
#include <QList>

#include <gdal.h>
#include <ogr_api.h>

#include <opencv2/core.hpp>

#include <algorithm>
#include <string>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

const std::vector<std::string> s_methods = {"svm", "normal_bayes"};

/// Map a failed pipeline run back onto the operator's stable error codes.
[[noreturn]] void throwPipelineError(const RsClassificationPipelineResult& res,
                                     const std::string& method) {
    using E = RsClassificationPipelineResult::Error;
    const std::string msg = res.errorMessage.toStdString();
    switch (res.error) {
        case E::Cancelled:
            throw RSOperatorError(ErrorCode::Cancelled, "Cancelled");
        case E::TrainingFailed:
            throw RSOperatorError(ErrorCode::OpenCvError,
                                  method == "normal_bayes" ? "NormalBayes training failed"
                                                           : "SVM training failed");
        case E::ModelSaveFailed:
        case E::SidecarSaveFailed:
            throw RSOperatorError(ErrorCode::FileNotWritable, msg);
        case E::NotFittedNoTrainingData:
            throw RSOperatorError(ErrorCode::InvalidInputData, msg);
        case E::InvalidBand:
            throw RSOperatorError(ErrorCode::OutOfRange, msg);
        case E::OutputDriverUnavailable:
            throw RSOperatorError(ErrorCode::GdalError, "GTiff driver not available");
        case E::OutputCreateFailed:
            throw RSOperatorError(ErrorCode::GdalError, "Failed to create output");
        case E::PredictionFailed:
        case E::PredictionSizeMismatch:
            throw RSOperatorError(ErrorCode::OpenCvError, msg);
        case E::RasterOpenFailed:
        case E::RasterReadFailed:
            throw RSOperatorError(ErrorCode::GdalError, msg);
        case E::VectorOpenFailed:
            throw RSOperatorError(ErrorCode::GdalError, msg);
        case E::VectorNoLayers:
            throw RSOperatorError(ErrorCode::InvalidInputData, msg);
        case E::ClassFieldNotFound:
            throw RSOperatorError(ErrorCode::InvalidParameter, msg);
        case E::NoValidPixels:
            throw RSOperatorError(ErrorCode::InvalidInputData,
                                  "No valid training pixels extracted (check CRS overlap and classField)");
        case E::InsufficientSamples:
            throw RSOperatorError(ErrorCode::InvalidInputData, "Insufficient training samples");
        case E::ModelOpenFailed:
            throw RSOperatorError(ErrorCode::FileNotReadable, msg);
        default:
            throw RSOperatorError(ErrorCode::ComputationError, msg);
    }
}

} // namespace

Json::Value RsSupervisedClassificationOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Multi-band image to classify");
    props["training"] = makeVectorParam("training",
                                        "Training polygons (required unless modelIn is set)");
    props["training"]["required"] = false;
    props["output"] = makeOutputParam("output", "Output class map GeoTIFF", "tif");
    props["method"] = makeEnumParam("method", "Classifier", s_methods, "svm");
    props["classField"] = makeStringParam("classField", "Integer class id field", "class_id");
    props["modelIn"] = makeStringParam("modelIn", "Load OpenCV model (predict-only mode)", "");
    props["modelIn"]["required"] = false;
    props["modelOut"] = makeStringParam("modelOut", "Optional path to save OpenCV model", "");
    props["modelOut"]["required"] = false;
    props["probabilityOutput"] = makeStringParam(
        "probabilityOutput",
        "Optional per-pixel best-class probability raster (Float32, NoData -1). "
        "Requires method=normal_bayes (SVM does not support probabilities)",
        "");
    props["probabilityOutput"]["required"] = false;
    props["maxSamplesPerClass"] = makeIntegerParam(
        "maxSamplesPerClass", "Cap training samples per class (0 = unlimited)", 5000);
    props["scale"] = makeBooleanParam(
        "scale",
        "Fit a feature scaler (standardization) on the training split and embed it in the "
        "model sidecar. In predict-only mode the sidecar's scaler is always applied when "
        "present, regardless of this flag.",
        true);
    Json::Value testSplit = makeNumberParam(
        "testSplit",
        "Stratified holdout fraction (0 = off, train mode only). When > 0, samples are "
        "split per class with a deterministic seed and accuracy metrics "
        "(overallAccuracy, kappa, confusionMatrix) are returned.",
        0.0);
    setRange(testSplit, 0.0, 0.9);
    props["testSplit"] = testSplit;

    Json::Value bands = makeStringParam("bands", "Optional 1-based band indices", "");
    bands["type"] = "array";
    bands["items"] = Json::Value(Json::objectValue);
    bands["items"]["type"] = "integer";
    bands["required"] = false;
    props["bands"] = bands;

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeOutputParam("output", "Class map", "tif");
    outputs["trainSamples"] = makeIntegerParam("trainSamples", "Training samples used", 0);
    outputs["classes"] = makeIntegerParam("classes", "Unique class count", 0);
    outputs["mode"] = makeStringParam("mode", "train_predict or predict_only", "");
    outputs["overallAccuracy"] = makeNumberParam(
        "overallAccuracy", "Held-out overall accuracy (only when testSplit > 0)", 0.0);
    outputs["kappa"] = makeNumberParam(
        "kappa", "Held-out Cohen's kappa (only when testSplit > 0)", 0.0);
    outputs["perClassMetrics"] = makeStringParam(
        "perClassMetrics", "Per-class validation metrics (producer/user accuracy, F1)", "");
    outputs["trainSamplesByClass"] = makeStringParam(
        "trainSamplesByClass", "Per-class training sample counts", "");
    outputs["imbalanceWarnings"] = makeStringParam(
        "imbalanceWarnings", "Class-imbalance warnings (classes under 10% of the largest)", "");
    outputs["meanConfidence"] = makeNumberParam(
        "meanConfidence", "Mean best-class probability over valid pixels (probabilityOutput only)", 0.0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output"});
    return root;
}

Json::Value RsSupervisedClassificationOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["provider"] = "rs";
    meta["tags"] = Json::Value(Json::arrayValue);
    meta["tags"].append("supervised");
    meta["tags"].append("classification");
    meta["tags"].append("svm");
    meta["tags"].append("model");
    meta["purpose"] = "Classify multi-band imagery using labeled polygons or a saved model";
    meta["useCases"] = Json::Value(Json::arrayValue);
    meta["useCases"].append("Land-cover mapping from ROI polygons");
    meta["useCases"].append("Apply a previously trained model (modelIn) to a new scene");
    meta["useCases"].append("Held-out accuracy assessment via testSplit");
    meta["prerequisites"] = Json::Value(Json::arrayValue);
    meta["prerequisites"].append("Train mode: training polygons must overlap the raster");
    meta["prerequisites"].append("Predict-only: modelIn must match method and band set");
    return meta;
}

Json::Value RsSupervisedClassificationOperator::run(const Json::Value& params,
                                                    RSOperatorContext& context) {
    const std::string inputPath = requireString(params, "input");
    const std::string outputPath = requireString(params, "output");
    const std::string modelIn = getString(params, "modelIn", "");
    const std::string modelOut = getString(params, "modelOut", "");
    const std::string probabilityOutput = getString(params, "probabilityOutput", "");
    const std::string trainingPath = getString(params, "training", "");
    const bool predictOnly = !modelIn.empty();
    const bool scale = getBool(params, "scale", true);
    const double testSplit = getDouble(params, "testSplit", 0.0);
    if (testSplit < 0.0 || testSplit > 0.9) {
        throw RSOperatorError(ErrorCode::OutOfRange, "testSplit must be in [0, 0.9]");
    }
    const std::string classField = getString(params, "classField", "class_id");
    const int maxPerClass = getInt(params, "maxSamplesPerClass", 5000);
    const std::string method = getEnum(params, "method", s_methods, "svm");
    if (!probabilityOutput.empty() && method != "normal_bayes") {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Probability outputs require method=normal_bayes; "
                              "SVM does not support them");
    }

    // Parameter completeness first (stable error codes for Agent/tests)
    if (!predictOnly && trainingPath.empty()) {
        throw RSOperatorError(ErrorCode::MissingRequiredParameter,
                              "Provide 'training' polygons or 'modelIn' for predict-only mode");
    }
    if (!fileExists(inputPath)) {
        throw RSOperatorError(ErrorCode::FileNotFound, "Input not found: " + inputPath);
    }
    if (predictOnly) {
        if (!fileExists(modelIn)) {
            throw RSOperatorError(ErrorCode::FileNotFound, "Model not found: " + modelIn);
        }
    } else if (!fileExists(trainingPath)) {
        throw RSOperatorError(ErrorCode::FileNotFound, "Training not found: " + trainingPath);
    }

    ensureGdalInit();
    GDALAllRegister();
    OGRRegisterAll();

    GdalDatasetWrapper ds;
    if (!ds.open(QString::fromStdString(inputPath))) {
        throw RSOperatorError(ErrorCode::GdalError, "Failed to open input raster");
    }

    const int width = ds.width();
    const int height = ds.height();
    const int bandCount = ds.bandCount();
    const std::vector<int> bands = parseBands(params, bandCount);
    const int nFeat = static_cast<int>(bands.size());

    RsClassificationPipeline::Config cfg;
    cfg.sourceRaster = QString::fromStdString(inputPath);
    cfg.outputRaster = QString::fromStdString(outputPath);
    cfg.bandIndices = QVector<int>(bands.begin(), bands.end());
    cfg.methodName = QString::fromStdString(method);
    cfg.fitScaler = scale;
    cfg.testSplit = testSplit;
    cfg.probabilityOutput = QString::fromStdString( probabilityOutput );

    if (predictOnly) {
        cfg.modelLoadPath = QString::fromStdString(modelIn);
    } else {
        cfg.trainingVector = QString::fromStdString(trainingPath);
        cfg.classField = QString::fromStdString(classField);
        cfg.maxSamplesPerClass = maxPerClass;
        cfg.backend = RsClassifierBackendFactory::create(QString::fromStdString(method));
        if (!modelOut.empty())
            cfg.modelSavePath = QString::fromStdString(modelOut);
    }

    const RsClassificationPipelineResult res = RsClassificationPipeline::run(
        std::move(cfg),
        [&context](double fraction, const QString& message) {
            if (context.isCancelled())
                return false;
            const std::string msg = message == QStringLiteral("Predicting tiles")
                                        ? "Classifying"
                                        : message.toStdString();
            context.reportProgress(0.1 + 0.88 * fraction, msg);
            return true;
        });

    if (!res.ok) {
        throwPipelineError(res, method);
    }

    context.reportProgress(1.0, "Supervised classification complete");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["method"] = method;
    result["mode"] = predictOnly ? "predict_only" : "train_predict";
    result["trainSamples"] = res.trainSamples;
    result["classes"] = res.classCount;
    result["features"] = nFeat;
    result["width"] = width;
    result["height"] = height;
    result["featuresExtracted"] = res.featuresExtracted;
    if (!probabilityOutput.empty()) {
        result["probabilityOutput"] = probabilityOutput;
        result["meanConfidence"] = res.meanConfidence;
    }
    if ( !res.trainSamplesByClass.isEmpty() )
    {
        // Per-class training sample counts + class-imbalance warnings
        // (classes with < 10% of the largest class's samples).
        Json::Value byClass(Json::arrayValue);
        int maxSamples = 0;
        for ( int count : res.trainSamplesByClass )
            maxSamples = std::max( maxSamples, count );
        QList<int> ids = res.trainSamplesByClass.keys();
        std::sort( ids.begin(), ids.end() );
        for ( int id : ids )
        {
            Json::Value entry(Json::objectValue);
            entry["classId"] = id;
            entry["samples"] = res.trainSamplesByClass.value( id );
            byClass.append( entry );
        }
        result["trainSamplesByClass"] = byClass;
        if ( res.trainSamplesByClass.size() > 1 && maxSamples >= 20 )
        {
            Json::Value warnings(Json::arrayValue);
            for ( int id : ids )
            {
                const int count = res.trainSamplesByClass.value( id );
                if ( count < 0.1 * maxSamples )
                {
                    warnings.append(
                        ( "Class " + std::to_string( id ) + " has only " +
                          std::to_string( count ) + " training samples vs " +
                          std::to_string( maxSamples ) + " for the largest class (" +
                          std::to_string( static_cast<int>( 100.0 * count / maxSamples ) ) +
                          "%); accuracy for this class may be unreliable. Consider "
                          "collecting more samples." ) );
                }
            }
            if ( !warnings.empty() )
                result["imbalanceWarnings"] = warnings;
        }
    }
    if (predictOnly)
        result["modelIn"] = modelIn;
    if (!modelOut.empty())
        result["modelOut"] = modelOut;
    if (res.accuracy.confusion.rows > 0) {
        result["overallAccuracy"] = res.accuracy.overallAccuracy;
        result["kappa"] = res.accuracy.kappa;
        Json::Value cm(Json::arrayValue);
        for (int r = 0; r < res.accuracy.confusion.rows; ++r) {
            Json::Value row(Json::arrayValue);
            for (int c = 0; c < res.accuracy.confusion.cols; ++c)
                row.append(res.accuracy.confusion.at<int>(r, c));
            cm.append(row);
        }
        result["confusionMatrix"] = cm;
        Json::Value classes(Json::arrayValue);
        for (int id : res.accuracy.classIds)
            classes.append(id);
        result["confusionClasses"] = classes;
        // Per-class validation metrics (producer's / user's accuracy, F1).
        Json::Value perClass(Json::arrayValue);
        for (int id : res.accuracy.classIds)
        {
            Json::Value entry(Json::objectValue);
            entry["classId"] = id;
            entry["producerAccuracy"] = res.accuracy.producerAcc.value( id, 0.0 );
            entry["userAccuracy"] = res.accuracy.userAcc.value( id, 0.0 );
            entry["f1"] = res.accuracy.f1.value( id, 0.0 );
            perClass.append( entry );
        }
        result["perClassMetrics"] = perClass;
    }
    return result;
}

} // namespace sicnu::operators::rs
