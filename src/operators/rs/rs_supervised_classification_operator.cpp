/***************************************************************************
 * rs_supervised_classification_operator.cpp
 ***************************************************************************/
#include "rs_supervised_classification_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QFile>
#include <QIODevice>
#include <QString>

#include <memory>

#include <gdal.h>
#include <gdal_alg.h>
#include <ogr_api.h>
#include <cpl_string.h>

#include <opencv2/core.hpp>
#include <opencv2/ml.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <random>
#include <set>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

const std::vector<std::string> s_methods = {"svm", "normal_bayes"};

/**
 * Rasterize one OGR geometry into a Byte mask (1 = inside) matching the
 * reference raster geotransform/size.
 */
std::vector<uint8_t> rasterizeGeometry(OGRGeometryH geom, int width, int height,
                                       const double gt[6]) {
    std::vector<uint8_t> mask(static_cast<size_t>(width) * height, 0);

    GDALDriverH memDriver = GDALGetDriverByName("MEM");
    if (!memDriver) {
        throw RSOperatorError(ErrorCode::GdalError, "MEM driver unavailable");
    }

    GDALDatasetH memDs = GDALCreate(memDriver, "", width, height, 1, GDT_Byte, nullptr);
    if (!memDs) {
        throw RSOperatorError(ErrorCode::GdalError, "Failed to create MEM raster");
    }
    GDALSetGeoTransform(memDs, const_cast<double*>(gt));

    char** options = nullptr;
    options = CSLSetNameValue(options, "ALL_TOUCHED", "TRUE");

    int bandList[1] = {1};
    double burn[1] = {1.0};
    OGRGeometryH geoms[1] = {geom};

    const CPLErr err = GDALRasterizeGeometries(
        memDs, 1, bandList, 1, geoms, nullptr, nullptr, burn, options, nullptr, nullptr);
    CSLDestroy(options);

    if (err != CE_None) {
        GDALClose(memDs);
        throw RSOperatorError(ErrorCode::GdalError, "GDALRasterizeGeometries failed");
    }

    GDALRasterBandH band = GDALGetRasterBand(memDs, 1);
    if (GDALRasterIO(band, GF_Read, 0, 0, width, height, mask.data(), width, height,
                     GDT_Byte, 0, 0) != CE_None) {
        GDALClose(memDs);
        throw RSOperatorError(ErrorCode::GdalError, "Failed to read rasterized mask");
    }
    GDALClose(memDs);
    return mask;
}

cv::Ptr<cv::ml::StatModel> trainModel(const std::string& method,
                                      const cv::Mat& X, const cv::Mat& y) {
    if (method == "normal_bayes") {
        auto clf = cv::ml::NormalBayesClassifier::create();
        if (!clf->train(X, cv::ml::ROW_SAMPLE, y)) {
            throw RSOperatorError(ErrorCode::OpenCvError, "NormalBayes training failed");
        }
        return clf;
    }

    // default SVM
    auto svm = cv::ml::SVM::create();
    svm->setType(cv::ml::SVM::C_SVC);
    svm->setKernel(cv::ml::SVM::RBF);
    svm->setC(10.0);
    svm->setGamma(0.5);
    svm->setTermCriteria(cv::TermCriteria(cv::TermCriteria::MAX_ITER + cv::TermCriteria::EPS,
                                          1000, 1e-3));
    if (!svm->train(X, cv::ml::ROW_SAMPLE, y)) {
        throw RSOperatorError(ErrorCode::OpenCvError, "SVM training failed");
    }
    return svm;
}

cv::Ptr<cv::ml::StatModel> loadModel(const std::string& method, const std::string& path) {
    try {
        if (method == "normal_bayes") {
            auto m = cv::Algorithm::load<cv::ml::NormalBayesClassifier>(path);
            if (m.empty())
                throw RSOperatorError(ErrorCode::FileNotReadable, "Failed to load NormalBayes: " + path);
            return m;
        }
        auto m = cv::Algorithm::load<cv::ml::SVM>(path);
        if (m.empty())
            throw RSOperatorError(ErrorCode::FileNotReadable, "Failed to load SVM: " + path);
        return m;
    } catch (const RSOperatorError&) {
        throw;
    } catch (const cv::Exception& e) {
        throw RSOperatorError(ErrorCode::OpenCvError,
                              std::string("Model load failed: ") + e.what());
    }
}

void writeModelMeta(const std::string& modelPath, const std::string& method,
                    const std::vector<int>& bands) {
    const std::string metaPath = modelPath + ".meta.json";
    Json::Value meta(Json::objectValue);
    meta["method"] = method;
    meta["bands"] = Json::Value(Json::arrayValue);
    for (int b : bands)
        meta["bands"].append(b);
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    const std::string body = Json::writeString(builder, meta);
    QFile f(QString::fromStdString(metaPath));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(body.c_str(), static_cast<qint64>(body.size()));
    }
}

std::string readMethodFromMeta(const std::string& modelPath, const std::string& fallback) {
    const std::string metaPath = modelPath + ".meta.json";
    QFile f(QString::fromStdString(metaPath));
    if (!f.open(QIODevice::ReadOnly))
        return fallback;
    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errs;
    const QByteArray data = f.readAll();
    const char* begin = data.constData();
    const char* end = begin + data.size();
    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    if (!reader->parse(begin, end, &root, &errs))
        return fallback;
    if (root.isMember("method") && root["method"].isString())
        return root["method"].asString();
    return fallback;
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
    props["maxSamplesPerClass"] = makeIntegerParam(
        "maxSamplesPerClass", "Cap training samples per class (0 = unlimited)", 5000);

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
    const std::string trainingPath = getString(params, "training", "");
    const bool predictOnly = !modelIn.empty();
    std::string method;
    if (predictOnly && !params.isMember("method")) {
        method = readMethodFromMeta(modelIn, "svm");
        std::transform(method.begin(), method.end(), method.begin(), ::tolower);
        if (std::find(s_methods.begin(), s_methods.end(), method) == s_methods.end())
            method = "svm";
    } else {
        method = getEnum(params, "method", s_methods, "svm");
    }
    const std::string classField = getString(params, "classField", "class_id");
    const int maxPerClass = getInt(params, "maxSamplesPerClass", 5000);

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
    const auto gtArr = ds.geoTransform();
    double gt[6] = {gtArr[0], gtArr[1], gtArr[2], gtArr[3], gtArr[4], gtArr[5]};
    const std::vector<int> bands = parseBands(params, bandCount);
    const int nFeat = static_cast<int>(bands.size());
    const size_t nPix = static_cast<size_t>(width) * static_cast<size_t>(height);

    context.reportProgress(0.05, "Reading raster bands");
    std::vector<std::vector<float>> bandData(static_cast<size_t>(nFeat));
    for (int i = 0; i < nFeat; ++i) {
        bandData[static_cast<size_t>(i)].resize(nPix);
        if (!ds.readBandData(bands[static_cast<size_t>(i)],
                             bandData[static_cast<size_t>(i)].data(), width, height)) {
            throw RSOperatorError(ErrorCode::GdalError,
                                  "Failed to read band " + std::to_string(bands[static_cast<size_t>(i)]));
        }
        context.throwIfCancelled();
    }

    cv::Ptr<cv::ml::StatModel> model;
    int nSamples = 0;
    int nClasses = 0;
    int featureCount = 0;
    std::string mode = "train_predict";

    if (predictOnly) {
        mode = "predict_only";
        context.reportProgress(0.35, "Loading model " + modelIn);
        model = loadModel(method, modelIn);
        context.logInfo("Loaded model (" + method + ") from " + modelIn);
    } else {
        context.reportProgress(0.25, "Collecting training samples from " + trainingPath);

        GDALDatasetH vecDs = GDALOpenEx(trainingPath.c_str(), GDAL_OF_VECTOR, nullptr, nullptr, nullptr);
        if (!vecDs) {
            throw RSOperatorError(ErrorCode::GdalError, "Failed to open training vector: " + trainingPath);
        }

        OGRLayerH layer = GDALDatasetGetLayer(vecDs, 0);
        if (!layer) {
            GDALClose(vecDs);
            throw RSOperatorError(ErrorCode::InvalidInputData, "Training dataset has no layers");
        }

        OGRFeatureDefnH defn = OGR_L_GetLayerDefn(layer);
        int fieldIdx = OGR_FD_GetFieldIndex(defn, classField.c_str());
        if (fieldIdx < 0)
            fieldIdx = OGR_FD_GetFieldIndex(defn, "class");
        if (fieldIdx < 0)
            fieldIdx = OGR_FD_GetFieldIndex(defn, "id");
        if (fieldIdx < 0) {
            GDALClose(vecDs);
            throw RSOperatorError(ErrorCode::InvalidParameter,
                                  "Class field not found: " + classField +
                                      " (also tried 'class', 'id')");
        }

        std::map<int, std::vector<size_t>> classPixels;
        OGR_L_ResetReading(layer);
        OGRFeatureH feat = nullptr;
        while ((feat = OGR_L_GetNextFeature(layer)) != nullptr) {
            ++featureCount;
            context.throwIfCancelled();

            const int classId = OGR_F_GetFieldAsInteger(feat, fieldIdx);
            if (classId <= 0) {
                OGR_F_Destroy(feat);
                continue;
            }

            OGRGeometryH geom = OGR_F_GetGeometryRef(feat);
            if (!geom) {
                OGR_F_Destroy(feat);
                continue;
            }

            std::vector<uint8_t> mask = rasterizeGeometry(geom, width, height, gt);
            auto& pixels = classPixels[classId];
            for (size_t i = 0; i < mask.size(); ++i) {
                if (mask[i])
                    pixels.push_back(i);
            }
            OGR_F_Destroy(feat);
        }
        GDALClose(vecDs);

        if (classPixels.empty()) {
            throw RSOperatorError(ErrorCode::InvalidInputData,
                                  "No valid training pixels extracted (check CRS overlap and classField)");
        }

        std::mt19937 rng(42);
        std::vector<size_t> sampleIdx;
        std::vector<int> sampleY;
        for (auto& [cid, pixels] : classPixels) {
            if (maxPerClass > 0 && static_cast<int>(pixels.size()) > maxPerClass) {
                std::shuffle(pixels.begin(), pixels.end(), rng);
                pixels.resize(static_cast<size_t>(maxPerClass));
            }
            for (size_t pix : pixels) {
                sampleIdx.push_back(pix);
                sampleY.push_back(cid);
            }
        }

        nSamples = static_cast<int>(sampleIdx.size());
        nClasses = static_cast<int>(classPixels.size());
        if (nSamples < 2) {
            throw RSOperatorError(ErrorCode::InvalidInputData, "Insufficient training samples");
        }

        context.reportProgress(0.45,
                               "Training " + method + " on " + std::to_string(nSamples) +
                                   " samples, " + std::to_string(nClasses) + " classes");

        cv::Mat trainX(nSamples, nFeat, CV_32F);
        cv::Mat trainY(nSamples, 1, CV_32S);
        for (int r = 0; r < nSamples; ++r) {
            const size_t pix = sampleIdx[static_cast<size_t>(r)];
            for (int c = 0; c < nFeat; ++c) {
                trainX.at<float>(r, c) = bandData[static_cast<size_t>(c)][pix];
            }
            trainY.at<int>(r, 0) = sampleY[static_cast<size_t>(r)];
        }

        model = trainModel(method, trainX, trainY);

        if (!modelOut.empty()) {
            try {
                model->save(modelOut);
                writeModelMeta(modelOut, method, bands);
                context.logInfo("Saved model to " + modelOut);
            } catch (const cv::Exception& e) {
                context.logWarning(std::string("Model save failed: ") + e.what());
            }
        }
    }

    context.reportProgress(0.6, "Classifying full raster");
    context.throwIfCancelled();

    std::vector<uint8_t> classMap(nPix, 0);
    // Process in row chunks to limit peak temp memory for predict matrix
    constexpr int chunkRows = 64;
    for (int row0 = 0; row0 < height; row0 += chunkRows) {
        const int rows = std::min(chunkRows, height - row0);
        const int n = rows * width;
        cv::Mat chunk(n, nFeat, CV_32F);
        for (int r = 0; r < rows; ++r) {
            for (int col = 0; col < width; ++col) {
                const size_t pix = static_cast<size_t>(row0 + r) * width + col;
                const int sample = r * width + col;
                for (int f = 0; f < nFeat; ++f) {
                    chunk.at<float>(sample, f) = bandData[static_cast<size_t>(f)][pix];
                }
            }
        }

        cv::Mat pred;
        try {
            model->predict(chunk, pred);
        } catch (const cv::Exception& e) {
            throw RSOperatorError(ErrorCode::OpenCvError,
                                  std::string("predict failed: ") + e.what());
        }

        for (int i = 0; i < n; ++i) {
            float v = (pred.cols >= 1) ? pred.at<float>(i, 0) : pred.at<float>(i);
            // NormalBayes may return float class ids; SVM returns float labels
            int label = static_cast<int>(std::lround(v));
            if (label < 0) label = 0;
            if (label > 255) label = 255;
            const size_t pix = static_cast<size_t>(row0) * width + static_cast<size_t>(i);
            classMap[pix] = static_cast<uint8_t>(label);
        }

        context.throwIfCancelled();
        context.reportProgress(0.6 + 0.3 * static_cast<double>(row0 + rows) / height,
                               "Classifying");
    }

    context.reportProgress(0.92, "Writing output");

    GDALDriverH driver = GDALGetDriverByName("GTiff");
    if (!driver) {
        throw RSOperatorError(ErrorCode::GdalError, "GTiff driver not available");
    }
    char** opts = nullptr;
    opts = CSLSetNameValue(opts, "COMPRESS", "LZW");
    GDALDatasetH outDs = GDALCreate(driver, outputPath.c_str(), width, height, 1, GDT_Byte, opts);
    CSLDestroy(opts);
    if (!outDs) {
        throw RSOperatorError(ErrorCode::GdalError, "Failed to create output");
    }
    GDALSetGeoTransform(outDs, gt);
    const QString proj = ds.projection();
    if (!proj.isEmpty())
        GDALSetProjection(outDs, proj.toUtf8().constData());

    GDALRasterBandH outBand = GDALGetRasterBand(outDs, 1);
    if (GDALRasterIO(outBand, GF_Write, 0, 0, width, height, classMap.data(), width, height,
                     GDT_Byte, 0, 0) != CE_None) {
        GDALClose(outDs);
        throw RSOperatorError(ErrorCode::GdalError, "Failed to write class map");
    }
    GDALSetRasterNoDataValue(outBand, 0);
    GDALClose(outDs);

    context.reportProgress(1.0, "Supervised classification complete");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["method"] = method;
    result["mode"] = mode;
    result["trainSamples"] = nSamples;
    result["classes"] = nClasses;
    result["features"] = nFeat;
    result["width"] = width;
    result["height"] = height;
    result["featuresExtracted"] = featureCount;
    if (predictOnly)
        result["modelIn"] = modelIn;
    if (!modelOut.empty())
        result["modelOut"] = modelOut;
    return result;
}

} // namespace sicnu::operators::rs
