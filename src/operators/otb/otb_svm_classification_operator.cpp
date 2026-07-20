/***************************************************************************
 * otb_svm_classification_operator.cpp  —  OTB SVM training argument builder
 ***************************************************************************/
#include "otb_svm_classification_operator.h"

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"

#include <QFile>
#include <QString>

namespace sicnu::operators::otb {

namespace {

const std::vector<std::string> s_kernels = {"linear", "rbf", "poly", "sigmoid"};

} // anonymous namespace

Json::Value OtbSvmClassificationOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input raster file path");
    props["vector"] = makeVectorParam("vector", "Training vector layer with class labels");
    props["labelField"] = makeStringParam("labelField",
                                          "Vector attribute field containing class labels",
                                          "Class");
    props["stats"] = makeStringParam("stats",
                                     "Optional image statistics XML file from ComputeImagesStatistics");
    props["stats"]["required"] = false;
    props["kernel"] = makeEnumParam("kernel", "SVM kernel type", s_kernels, "linear");
    props["C"] = makeNumberParam("C", "SVM cost parameter C", 1.0);
    setRange(props["C"], 0.0, 1e12);
    props["output"] = makeOutputParam("output", "Output model file path (.txt or .xml)", "txt");

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeStringParam("output", "Output model file path");

    return buildSchema(displayName(), description(), props, outputs,
                       {"input", "vector", "output"});
}

Json::Value OtbSvmClassificationOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("classification");
    meta["tags"].append("svm");
    meta["tags"].append("machine-learning");
    meta["tags"].append("otb");
    meta["purpose"] = "Train a supervised Support Vector Machine classifier for remote sensing images.";
    meta["prerequisites"].append("Input raster must overlap the training vector layer.");
    meta["prerequisites"].append("Vector layer must contain a numeric/text field with class labels.");
    meta["workflowHints"].append("Compute image statistics first and pass via 'stats' for better normalization.");
    meta["workflowHints"].append("Use otb:image_classifier to apply the trained model to new imagery.");
    meta["limitations"].append("Training time grows with image size and number of samples.");
    return meta;
}

QStringList OtbSvmClassificationOperator::buildOtbArgs(const Json::Value& params,
                                                       RSOperatorContext& context) const {
    Q_UNUSED(context);

    // Validate algorithm choice before I/O so tests and Agents get a typed error.
    const std::string kernel = getEnum(params, "kernel", s_kernels, "linear");

    const std::string inputPath = requireString(params, "input");
    const std::string vectorPath = requireString(params, "vector");
    const std::string labelField = getString(params, "labelField", "Class");
    if (labelField.empty()) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Parameter 'labelField' must be a non-empty string");
    }

    if (!QFile::exists(QString::fromStdString(inputPath))) {
        throw RSOperatorError(ErrorCode::FileNotFound,
                              "Input raster not found: " + inputPath);
    }
    if (!QFile::exists(QString::fromStdString(vectorPath))) {
        throw RSOperatorError(ErrorCode::FileNotFound,
                              "Training vector not found: " + vectorPath);
    }
    const double C = getDouble(params, "C", 1.0);
    const std::string outputPath = requireString(params, "output");

    QStringList args;
    args << "-io.il" << QString::fromStdString(inputPath);
    args << "-io.vd" << QString::fromStdString(vectorPath);
    args << "-sample.vfn" << QString::fromStdString(labelField);

    if (params.isMember("stats") && params["stats"].isString() && !params["stats"].asString().empty()) {
        const std::string statsPath = params["stats"].asString();
        if (!QFile::exists(QString::fromStdString(statsPath))) {
            throw RSOperatorError(ErrorCode::FileNotFound,
                                  "Statistics file not found: " + statsPath);
        }
        args << "-io.imstat" << QString::fromStdString(statsPath);
    }

    args << "-classifier" << QStringLiteral("libsvm");
    args << "-classifier.libsvm.k" << QString::fromStdString(kernel);
    args << "-classifier.libsvm.c" << QString::number(C, 'f', 4);
    args << "-io.out" << QString::fromStdString(outputPath);

    return args;
}

} // namespace sicnu::operators::otb
