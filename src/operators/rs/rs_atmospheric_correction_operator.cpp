/***************************************************************************
 * rs_atmospheric_correction_operator.cpp  —  Atmospheric correction RSOperator
 ***************************************************************************/
#include "rs_atmospheric_correction_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/atmospheric_correction.h"
#include "processing/algorithms/radiometric_calibration.h"
#include "processing/algorithms/satellite_products.h"
#include "processing/framework/resource_estimation.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QString>
#include <QMap>

#include <cstdint>
#include <optional>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

const std::vector<std::string> s_methods = {
    "dn_to_radiance", "dos1", "dos2", "quac"
};

} // anonymous namespace

Json::Value RsAtmosphericCorrectionOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input optical raster");
    props["output"] = makeOutputParam("output", "Output corrected raster", "tif");
    props["band"] = makeIntegerParam("band", "1-based band number", 1);
    props["method"] = makeEnumParam("method", "Correction method", s_methods, "dos1");
    props["metadata_path"] = makeStringParam("metadata_path",
        "Path to Landsat *_MTL.txt or Sentinel-2 MTD_*.xml (optional; when omitted, "
        "auto-detected next to the input; used to resolve radiance gain/bias)", "");
    props["gain"] = makeNumberParam("gain", "Radiance gain (optional; when omitted, resolved from product metadata)", 1.0);
    props["bias"] = makeNumberParam("bias", "Radiance bias (optional; when omitted, resolved from product metadata)", 0.0);
    props["airmass"] = makeNumberParam("airmass", "Relative airmass for DOS2", 1.0);

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Output raster path");
    outputs["method"] = makeStringParam("method", "Applied method", "");
    outputs["band"] = makeIntegerParam("band", "Processed band", 0);

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output"});
    return root;
}

Json::Value RsAtmosphericCorrectionOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("atmospheric");
    meta["tags"].append("dos");
    meta["tags"].append("radiometric");
    meta["purpose"] = "Convert DN to radiance or surface reflectance before spectral analysis.";
    meta["workflowHints"].append("Apply before computing spectral indices.");
    meta["limitations"].append("Gain/bias are resolved from product metadata (MTL/MTD) when omitted; "
                               "explicit values always win.");
    meta["facadeOf"] = "rs:dn_to_radiance,rs:atmospheric_dos1,rs:atmospheric_dos2,rs:atmospheric_quac";
    return meta;
}

Json::Value RsAtmosphericCorrectionOperator::executionEstimate() const {
    // MultiPassStreaming: 256x256 stream tiles (kTile in
    // AtmosphericCorrection::processFile); a source tile and one radiance/output
    // tile buffer in flight plus a 1024-bin dark-object histogram (~8 KiB) ->
    // ~0.5 MiB per pass. QUAC is full-raster by design and dominates when
    // selected (see estimateExecution for the QUAC-aware dynamic estimate).
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 256;
    est["tileHeight"] = 256;
    est["estimatedRamBytes"] = 524288;
    return est;
}

namespace atmospheric_detail {

Json::Value estimateAtmosphericCorrectionRam(const std::string& method, const Json::Value& params) {
    if (method == "quac" && params.isObject()
        && params.isMember("input") && params["input"].isString())
    {
        GdalDatasetWrapper probe;
        if (probe.open(QString::fromStdString(params["input"].asString()))
            && probe.width() > 0 && probe.height() > 0 && probe.bandCount() > 0)
        {
            std::optional<std::uint64_t> pixels =
                sicnu::processing::checkedMul(
                    static_cast<std::uint64_t>(probe.width()),
                    static_cast<std::uint64_t>(probe.height()));
            if (pixels)
            {
                // Input + output Float32 buffers ≈ 2 x pixels x bands x 4 B.
                std::optional<std::uint64_t> ram =
                    sicnu::processing::checkedMulN(
                        { *pixels, static_cast<std::uint64_t>(probe.bandCount()),
                          static_cast<std::uint64_t>(sizeof(float)), 2ULL });
                if (ram)
                {
                    Json::Value est(Json::objectValue);
                    est["tileWidth"] = 0; // full-raster
                    est["tileHeight"] = 0;
                    est["estimatedRamBytes"] = Json::Value::UInt64(*ram);
                    est["basis"] = "dynamic";
                    return est;
                }
            }
        }
    }
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 256;
    est["tileHeight"] = 256;
    est["estimatedRamBytes"] = 524288;
    return est;
}

Json::Value runAtmosphericCorrectionCore(const std::string& defaultMethod,
                                         const Json::Value& params,
                                         RSOperatorContext& context) {
    if (!params.isObject()) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "Operator parameters must be a JSON object");
    }

    const std::string inputPath = requireString(params, "input");
    const std::string outputPath = requireString(params, "output");

    if (!fileExists(inputPath)) {
        throw RSOperatorError(ErrorCode::FileNotFound,
                              "Input raster not found: " + inputPath);
    }

    const int band = getInt(params, "band", 1);
    const std::string method = params.isMember("method")
                                   ? getEnum(params, "method", s_methods, defaultMethod)
                                   : defaultMethod;
    const bool hasGain = params.isMember("gain");
    const bool hasBias = params.isMember("bias");
    float gain = static_cast<float>(getDouble(params, "gain", 1.0));
    float bias = static_cast<float>(getDouble(params, "bias", 0.0));
    const float airmass = static_cast<float>(getDouble(params, "airmass", 1.0));

    int methodCode = AtmosphericCorrection::Dos1;
    if (method == "dn_to_radiance") methodCode = AtmosphericCorrection::DnToRadiance;
    else if (method == "dos2") methodCode = AtmosphericCorrection::Dos2;
    else if (method == "quac") methodCode = AtmosphericCorrection::Quac;

    // P1: guard against double correction. The radiometric state recorded by
    // importers/calibration operators (SICNU_RADIOMETRIC_STATE) tells us what
    // the input already is; correcting an already-corrected raster is a silent
    // science error, so reject it instead of compounding it.
    {
        const QString inputState = SatelliteProducts::readRadiometricState(
            QString::fromStdString(inputPath));
        if ( !inputState.isEmpty() )
        {
            const bool toSurface =
                ( methodCode == AtmosphericCorrection::Dos1
                  || methodCode == AtmosphericCorrection::Dos2
                  || methodCode == AtmosphericCorrection::Quac );
            if ( toSurface
                 && inputState == QLatin1String( SatelliteProducts::kRadiometricStateSurfaceReflectance ) )
            {
                throw RSOperatorError(
                    ErrorCode::InvalidInputData,
                    "Input is already surface reflectance (" + inputState.toStdString()
                    + "); atmospheric correction would double-correct it. "
                    "Use a raw (DN) or TOA reflectance product as input." );
            }
            if ( methodCode == AtmosphericCorrection::DnToRadiance
                 && ( inputState == QLatin1String( SatelliteProducts::kRadiometricStateRadiance )
                      || inputState == QLatin1String( SatelliteProducts::kRadiometricStateSurfaceReflectance )
                      || inputState == QLatin1String( SatelliteProducts::kRadiometricStateToaReflectance ) ) )
            {
                throw RSOperatorError(
                    ErrorCode::InvalidInputData,
                    "Input is already " + inputState.toStdString()
                    + "; dn_to_radiance requires a raw digital-number product." );
            }
            if ( !inputState.isEmpty() )
                context.logInfo( "Input radiometric state: " + inputState.toStdString() );
        }
    }

    // DOS1/DOS2 now run in TOA-reflectance space (#610): they require
    // reflectance-capable coefficients (Landsat REFLECTANCE_MULT/ADD plus
    // SUN_ELEVATION; Sentinel-2 QUANTIFICATION_VALUE/offsets) resolved from
    // product metadata. Producing haze-corrected radiance while labeling it
    // surface reflectance was the silent science bug this replaces.
    if (methodCode == AtmosphericCorrection::Dos1 || methodCode == AtmosphericCorrection::Dos2) {
        QString dosMetaPath = QString::fromStdString(getString(params, "metadata_path", ""));
        if (dosMetaPath.isEmpty()) {
            QString detectError;
            dosMetaPath = RadiometricCalibration::autoDetectMetadataFile(
                QString::fromStdString(inputPath), &detectError);
            // Ambiguous sibling metadata must fail closed, not guess (#699).
            if (dosMetaPath.isEmpty() && !detectError.isEmpty()) {
                throw RSOperatorError(ErrorCode::InvalidInputData,
                                      detectError.toStdString());
            }
        }
        if (!dosMetaPath.isEmpty()) {
            GdalDatasetWrapper ds;
            QMap<int, QString> bandNames;
            if (ds.open(QString::fromStdString(inputPath))) {
                for (int b = 1; b <= ds.bandCount(); ++b) {
                    const QString desc = ds.bandDescription(b);
                    bandNames.insert(b, desc.isEmpty() ? QStringLiteral("B%1").arg(b) : desc);
                }
            }
            RadiometricCalibration::CalibrationMetadata meta;
            QString metaError;
            if (RadiometricCalibration::loadMetadata(QString::fromStdString(inputPath),
                                                     dosMetaPath, bandNames, &meta,
                                                     &metaError)
                && meta.bands.contains(band)) {
                if (meta.sensor == RadiometricCalibration::SensorType::Landsat
                    && !meta.hasSunElevation) {
                    throw RSOperatorError(
                        ErrorCode::InvalidInputData,
                        "DOS surface-reflectance correction requires SUN_ELEVATION in "
                        "the metadata (refusing to default to 90 degrees): " + dosMetaPath.toStdString());
                }
                const auto &c = meta.bands.value(band);
                context.logInfo("DOS in reflectance space using " + dosMetaPath.toStdString());
                QString dosError;
                if (!AtmosphericCorrection::processFileDos(
                        QString::fromStdString(inputPath),
                        QString::fromStdString(outputPath),
                        band, methodCode, c, meta.sensor, meta.sunElevationDeg,
                        airmass, &dosError)) {
                    throw RSOperatorError(ErrorCode::ComputationError,
                                          "Atmospheric correction failed: " + dosError.toStdString());
                }
                context.throwIfCancelled();
                context.reportProgress(1.0, "Atmospheric correction complete");
                const char *dosState = SatelliteProducts::kRadiometricStateSurfaceReflectance;
                QString dosStateError;
                if (!SatelliteProducts::setRadiometricState(
                        QString::fromStdString(outputPath), dosState, &dosStateError))
                    context.logWarning(dosStateError.toStdString());
                Json::Value dosResult(Json::objectValue);
                dosResult["output"] = outputPath;
                dosResult["method"] = method;
                dosResult["band"] = band;
                return dosResult;
            }
        }
        {
            throw RSOperatorError(
                ErrorCode::InvalidParameter,
                "DOS1/DOS2 surface-reflectance correction requires product metadata with "
                "reflectance coefficients (Landsat MTL REFLECTANCE_MULT/ADD + SUN_ELEVATION, "
                "or Sentinel-2 MTD QUANTIFICATION_VALUE). Provide metadata_path or place "
                "MTL/MTD next to the input. Use method dn_to_radiance for plain radiance.");
        }
    }

    // Resolve radiance gain/bias from product metadata (explicit MTL/MTD path
    // or auto-detected sibling) when the caller did not supply them — the
    // "sensor metadata populates parameters automatically" workflow.
    if (methodCode == AtmosphericCorrection::DnToRadiance && (!hasGain || !hasBias)) {
        QString metadataPath = QString::fromStdString(getString(params, "metadata_path", ""));
        if (metadataPath.isEmpty()) {
            QString detectError;
            metadataPath = RadiometricCalibration::autoDetectMetadataFile(
                QString::fromStdString(inputPath), &detectError);
            // Ambiguous sibling metadata must fail closed, not guess (#699).
            if (metadataPath.isEmpty() && !detectError.isEmpty()) {
                throw RSOperatorError(ErrorCode::InvalidInputData,
                                      detectError.toStdString());
            }
        }
        bool resolved = false;
        if (!metadataPath.isEmpty()) {
            GdalDatasetWrapper ds;
            QMap<int, QString> bandNames;
            if (ds.open(QString::fromStdString(inputPath))) {
                // Full map (descriptions first, synthetic B%1 fallback) so MTL
                // coefficients resolve through the name mapping instead of the
                // empty-map identity auto-discovery (#448).
                for (int b = 1; b <= ds.bandCount(); ++b) {
                    const QString desc = ds.bandDescription(b);
                    bandNames.insert(b, desc.isEmpty() ? QStringLiteral("B%1").arg(b) : desc);
                }
            }
            RadiometricCalibration::CalibrationMetadata meta;
            QString metaError;
            if (RadiometricCalibration::loadMetadata(QString::fromStdString(inputPath),
                                                     metadataPath, bandNames, &meta,
                                                     &metaError)
                && meta.bands.contains(band)) {
                const auto &c = meta.bands.value(band);
                if (!hasGain) {
                    gain = static_cast<float>(c.radianceGain);
                    context.logInfo("Resolved radiance gain from " + metadataPath.toStdString());
                }
                if (!hasBias) {
                    bias = static_cast<float>(c.radianceBias);
                    context.logInfo("Resolved radiance bias from " + metadataPath.toStdString());
                }
                resolved = true;
            }
        }
        if (!resolved && (!hasGain || !hasBias)) {
            throw RSOperatorError(
                ErrorCode::InvalidParameter,
                "Radiance gain and bias must be specified or resolved from metadata (MTL/MTD)");
        }
    }

    context.logInfo("Applying " + method + " to " + inputPath);
    context.reportProgress(0.2, "Running atmospheric correction");

    QString errorMessage;
    bool ok = false;
    if (methodCode == AtmosphericCorrection::Quac) {
        // QUAC processes all bands jointly (image-statistics based).
        ok = AtmosphericCorrection::processFileMultiBand(
            QString::fromStdString(inputPath),
            QString::fromStdString(outputPath),
            methodCode, &errorMessage,
            [&](double frac, const QString &msg) {
                context.reportProgress(0.2 + 0.75 * frac, msg.toStdString());
                context.throwIfCancelled();
            });
    } else {
        ok = AtmosphericCorrection::processFile(QString::fromStdString(inputPath),
                                                QString::fromStdString(outputPath),
                                                band, methodCode, gain, bias, airmass,
                                                &errorMessage);
    }
    if (!ok) {
        throw RSOperatorError(ErrorCode::ComputationError,
                              "Atmospheric correction failed: " + errorMessage.toStdString());
    }

    context.throwIfCancelled();
    context.reportProgress(1.0, "Atmospheric correction complete");

    // Record the radiometric state so change detection can verify
    // comparability (ADR 0114): DOS/QUAC output surface reflectance;
    // dn_to_radiance output radiance.
    const char *state = SatelliteProducts::kRadiometricStateSurfaceReflectance;
    if ( method == "dn_to_radiance" )
        state = SatelliteProducts::kRadiometricStateRadiance;
    QString stateError;
    if ( !SatelliteProducts::setRadiometricState(
           QString::fromStdString( outputPath ), state, &stateError ) )
        context.logWarning( stateError.toStdString() );

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["method"] = method;
    result["band"] = band;
    return result;
}

} // namespace atmospheric_detail

Json::Value RsAtmosphericCorrectionOperator::estimateExecution(const Json::Value& params) const {
    std::string method = "dos1";
    if (params.isObject() && params.isMember("method") && params["method"].isString())
        method = params["method"].asString();
    return atmospheric_detail::estimateAtmosphericCorrectionRam(method, params);
}

Json::Value RsAtmosphericCorrectionOperator::run(const Json::Value& params,
                                                 RSOperatorContext& context) {
    return atmospheric_detail::runAtmosphericCorrectionCore("dos1", params, context);
}

} // namespace sicnu::operators::rs
