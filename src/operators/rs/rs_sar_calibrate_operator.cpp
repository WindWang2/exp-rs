/***************************************************************************
 * rs_sar_calibrate_operator.cpp — SAR radiometric calibration (Platform 3.0)
 ***************************************************************************/
#include "rs_sar_calibrate_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/nodata_utils.h"
#include "processing/algorithms/sar/sar_calibration.h"
#include "processing/algorithms/sar/sar_metadata.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h"

#include <QDir>
#include <QFileInfo>
#include <QString>

#include <limits>
#include <string>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

const std::vector<std::string> s_domains = { "linear_power", "db" };

Json::Value makeSarInputContract() {
    Json::Value c(Json::objectValue);
    c["modality"] = "sar";
    return c;
}

} // anonymous namespace

Json::Value RsSarCalibrateOperator::schema() const {
    using namespace schema;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Input SAR raster (DN or amplitude)");
    props["input"]["x-rs-contract"] = makeSarInputContract();
    props["output"] = makeOutputParam("output", "Output calibrated sigma0 raster (Float32)", "tif");
    props["band"] = makeIntegerParam("band", "1-based input band (0 = all bands calibrated independently)", 1);
    props["calibrationA"] = makeNumberParam("calibrationA", "Calibration constant A (sigma0 = DN²/A²; use the product's A value). Inert on the LUT path", 1.0);
    props["calibrationLut"] = makeStringParam("calibrationLut", "Per-row calibration LUT sidecar path: one finite positive constant per input row. Overrides the declared SICNU_SAR_CALIBRATION_LUT (resolved relative to the input raster and confined to its directory); missing or malformed is a typed refusal, never a constant fallback", "");
    props["noiseLinear"] = makeNumberParam("noiseLinear", "Additive noise power to subtract before scaling (linear, 0 disables)", 0.0);
    props["outputDomain"] = makeEnumParam("outputDomain", "Output numeric domain", s_domains, "linear_power");
    props["polarizations"] = makeStringParam("polarizations", "Comma-separated polarizations (e.g. VV,VH) recorded on the output", "");
    props["sensor"] = makeStringParam("sensor", "Sensor/instrument id recorded on the output", "");
    props["incidenceDeg"] = makeNumberParam("incidenceDeg", "Scene incidence angle (degrees) recorded as metadata", 0.0);

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeRasterParam("output", "Calibrated raster path");
    outputs["calibration"] = makeStringParam("calibration", "Calibration state of the output (sigma0)");
    outputs["domain"] = makeStringParam("domain", "Output numeric domain");
    outputs["bands"] = makeIntegerParam("bands", "Number of output bands");

    Json::Value root = makeRootSchema(displayName(), description(), props, outputs);
    root["required"] = makeRequired({"input", "output"});
    return root;
}

Json::Value RsSarCalibrateOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append("sar");
    meta["tags"].append("calibration");
    meta["tags"].append("radiometry");
    meta["purpose"] = "Convert SAR digital numbers to calibrated sigma0 backscatter "
                      "in linear power or dB with an explicit, recorded numeric domain.";
    meta["prerequisites"].append("SAR amplitude/DN raster; the calibration constant A must "
                                 "match the product convention (Sentinel-1 GRD: per-beam "
                                 "constant from the annotation, simplified here to one "
                                 "constant per run), or a per-row calibration LUT sidecar "
                                 "(one constant per input row).");
    meta["workflowHints"].append("Calibrate before speckle filtering or change detection: "
                                 "rs:sar_calibrate -> rs:sar_speckle.");
    meta["workflowHints"].append("dB output is 10·log10(power); nonpositive power becomes NoData.");
    meta["limitations"].append("LUT calibration reads a per-row sidecar (one constant per "
                               "input row, no interpolation); annotation-XML LUTs are not "
                               "parsed and a missing LUT is a typed refusal, never a "
                               "constant-A fallback.");
    Json::Value contract(Json::objectValue);
    contract["modality"] = "sar";
    meta["x-rs-contract"] = contract;
    return meta;
}

Json::Value RsSarCalibrateOperator::executionEstimate() const {
    Json::Value est(Json::objectValue);
    est["tileWidth"] = 256;
    est["tileHeight"] = 256;
    est["estimatedRamBytes"] = Json::Value::UInt64( 2ULL * 256ULL * 256ULL * 4ULL );
    return est;
}

Json::Value RsSarCalibrateOperator::run(const Json::Value& params,
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
    const double calibrationA = getDouble(params, "calibrationA", 1.0);
    const std::string calibrationLutPath = getString(params, "calibrationLut", "");
    const double noiseLinear = getDouble(params, "noiseLinear", 0.0);
    const std::string domainStr = getEnum(params, "outputDomain", s_domains, "linear_power");
    const sicnu::sar::SarDomain domain = domainStr == "db"
                                             ? sicnu::sar::SarDomain::Decibels
                                             : sicnu::sar::SarDomain::LinearPower;
    const QString polarizations =
        QString::fromStdString( getString( params, "polarizations", "" ) );
    const QString sensor = QString::fromStdString( getString( params, "sensor", "" ) );
    const double incidenceDeg = getDouble(params, "incidenceDeg", 0.0);

    GdalDatasetWrapper src;
    if (!src.open(QString::fromStdString(inputPath))) {
        throw RSOperatorError(ErrorCode::InvalidInputData,
                              "Cannot open input raster: " + inputPath);
    }

    // #1147: declared-domain preflight — the DN formula sigma0 =
    // (DN² − noise)/A² is a linear-power kernel; a dB-domain product
    // (internal or legacy external) would be exponentially mis-scaled while
    // the output confidently declares linear_power. Same refusal the six
    // downstream family operators already implement.
    if ( sicnu::sar::readDomain( src ) == QLatin1String( "db" ) )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "input declares SICNU_SAR_DOMAIN=db; this operator's DN formula "
                               "is linear power — convert with rs:sar_backscatter "
                               "inputDomain=db first" );

    // Declared-contract preflight: this operator applies the DN formula
    // sigma0 = (DN² − noise)/A². Re-applying it to a product that already
    // declares a calibrated state would double-scale the radiometry, a derived
    // product (pair metric / texture) carries no backscatter at all, and an
    // unreadable declared token must not be silently treated as DN — refuse
    // all three. A declared `dn` (or an absent declaration) is accepted.
    const sicnu::sar::SarStateRead declared =
        sicnu::sar::readDeclaredSarState( src );
    if ( declared.conflict )
    {
        throw RSOperatorError(
            ErrorCode::InvalidParameter,
            "input declares conflicting SICNU_SAR_CALIBRATION='" +
                declared.calibration.toStdString() + "' and SICNU_RADIOMETRIC_STATE='" +
                declared.state.toStdString() +
                "'; refusing to guess the radiometric state" );
    }
    if ( !declared.token.isEmpty() )
    {
        const QString normalized = sicnu::sar::normalizeCalibration( declared.token );
        if ( normalized.isEmpty() )
        {
            if ( sicnu::sar::isSarDerivedState( declared.token ) )
            {
                throw RSOperatorError(
                    ErrorCode::InvalidParameter,
                    "input declares the derived SAR product '" + declared.token.toStdString() +
                        "' (pair metric / texture), which carries no backscatter "
                        "calibration; rs:sar_calibrate applies the DN formula and cannot "
                        "recalibrate a derived product" );
            }
            throw RSOperatorError(
                ErrorCode::InvalidParameter,
                "input declares unrecognized SICNU_SAR_CALIBRATION='" +
                    declared.token.toStdString() +
                    "'; refusing to guess the radiometric state" );
        }
        if ( normalized != QLatin1String( "dn" ) )
        {
            throw RSOperatorError(
                ErrorCode::InvalidParameter,
                "input already declares SICNU_SAR_CALIBRATION=" +
                    normalized.toStdString() +
                    "; rs:sar_calibrate applies the DN formula and would double-scale "
                    "an already-calibrated product. Use rs:sar_backscatter to convert "
                    "between calibrated states." );
        }
    }

    // LUT resolution (Radiometric State 13.0): an explicit calibrationLut
    // parameter overrides the declared SICNU_SAR_CALIBRATION_LUT sidecar,
    // which resolves relative to the input raster. Missing, malformed or
    // row-count-mismatched LUTs are typed refusals — the constant A is never
    // substituted for an unreadable calibration contract.
    std::vector<double> lutRowA;
    const std::vector<double> *lutRowAPtr = nullptr;
    {
        QString lutPath;
        if ( !calibrationLutPath.empty() )
        {
            // Explicit parameter: used verbatim (CWD-relative or absolute).
            lutPath = QString::fromStdString( calibrationLutPath );
        }
        else
        {
            const QString declaredLut =
                sicnu::sar::datasetMeta( src, sicnu::sar::kCalibrationLutKey ).trimmed();
            if ( !declaredLut.isEmpty() )
            {
                // Declared metadata is data-controlled: resolve relative to
                // the declaring raster and refuse anything that escapes the
                // raster's directory (no traversal, no absolute detours). The
                // comparison uses canonical paths where they resolve (symlinks
                // included) and falls back to the lexical form otherwise; a
                // raster at the filesystem root has no containing subtree and
                // is refused rather than special-cased.
                const QString baseDir =
                    QFileInfo( QString::fromStdString( inputPath ) ).absolutePath();
                lutPath = QFileInfo( declaredLut ).isAbsolute()
                              ? QDir::cleanPath( declaredLut )
                              : QDir::cleanPath( QDir( baseDir ).filePath( declaredLut ) );
                const QString canonicalBase = QDir( baseDir ).canonicalPath();
                const QString canonicalLut = QFileInfo( lutPath ).canonicalFilePath();
                // #1164: the canonical comparison GOVERNS wherever it can be
                // computed (a symlink inside the directory pointing outside
                // resolves outside — refused); the lexical form is a FALLBACK
                // for a LUT that does not exist yet / is on a dead link,
                // never an alternative acceptance route.
                bool contained = false;
                if ( baseDir != QDir::rootPath() )
                {
                    if ( !canonicalBase.isEmpty() && !canonicalLut.isEmpty() )
                        contained =
                            canonicalLut.startsWith( canonicalBase + QLatin1Char( '/' ) );
                    else
                        contained = lutPath.startsWith( baseDir + QLatin1Char( '/' ) );
                }
                if ( !contained )
                {
                    throw RSOperatorError(
                        ErrorCode::InvalidParameter,
                        "declared SICNU_SAR_CALIBRATION_LUT '" + declaredLut.toStdString() +
                            "' escapes the input raster's directory; refusing to read a "
                            "calibration sidecar from outside the product" );
                }
            }
        }
        if ( !lutPath.isEmpty() )
        {
            QString lutError;
            if ( !sicnu::sar::parseCalibrationLut( lutPath, src.height(), &lutRowA, &lutError ) )
            {
                throw RSOperatorError( ErrorCode::InvalidParameter,
                                       "calibration LUT refused: " + lutError.toStdString() );
            }
            lutRowAPtr = &lutRowA;
        }
    }

    // calibrationA is only consulted on the constant path; on the LUT path it
    // is inert and must not refuse an otherwise valid run.
    if ( !lutRowAPtr && calibrationA <= 0.0 )
    {
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "calibrationA must be > 0 (sigma0 = DN²/A²)" );
    }

    const int bandCount = band > 0 ? 1 : src.bandCount();
    const int firstBand = band > 0 ? band : 1;
    if (firstBand < 1 || firstBand > src.bandCount()) {
        throw RSOperatorError(ErrorCode::InvalidParameter,
                              "band out of range: " + std::to_string(firstBand));
    }

    // One per-row LUT describes one calibration vector: real dual-pol products
    // carry a different vector per polarization, so a multi-band run reuses
    // this vector for every band — warn instead of silently mis-calibrating.
    if ( lutRowAPtr && bandCount > 1 )
    {
        context.logWarning( "one calibration LUT is applied to every output band; per-band "
                            "calibration vectors must be calibrated one band at a time "
                            "(band=1..n)" );
    }

    // Sentinel declared on the analysis band (NaN when undeclared).
    const float nodata = sicnu::rs::bandNoDataSentinel(src, firstBand);

    context.reportProgress(0.05, "Calibrating SAR raster");
    GdalStreamingOutput dst(QString::fromStdString(outputPath), src.width(), src.height(),
                            bandCount, GDT_Float32, src.geoTransform(), src.projection());
    if (!dst.isOpen()) {
        throw RSOperatorError(ErrorCode::GdalError, "Cannot create output raster");
    }
    dst.setNoDataValue(std::numeric_limits<float>::quiet_NaN());

    // Single-band fast path streams through the calibrated kernel directly;
    // multi-band runs calibrate each band independently (same constants, and
    // the same per-row LUT — one grid, one row mapping).
    bool ok = true;
    for (int b = 0; b < bandCount && ok; ++b) {
        context.throwIfCancelled();
        ok = sicnu::sar::calibrateRaster(src, firstBand + b, calibrationA, noiseLinear, domain,
                                         nodata, dst, 256, b + 1, polarizations, sensor,
                                         incidenceDeg, 0.0, lutRowAPtr);
        context.reportProgress(0.1 + 0.85 * (b + 1) / bandCount, "Calibrated band");
    }
    if (!ok) {
        dst.abandon();
        throw RSOperatorError(ErrorCode::GdalError, "SAR calibration failed while streaming");
    }
    // Radiometric state in the shared vocabulary.
    dst.setMetadataItem("SICNU_RADIOMETRIC_STATE", "sigma0");

    QString error;
    if (!dst.closeWithError(&error)) {
        throw RSOperatorError(ErrorCode::GdalError, "Failed to finalize output: " +
                                                        error.toStdString());
    }

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["calibration"] = "sigma0";
    result["domain"] = domainStr;
    result["bands"] = bandCount;
    context.reportProgress(1.0, "SAR calibration complete");
    return result;
}

} // namespace sicnu::operators::rs
