/***************************************************************************
 * gdal_polygonize_operator.cpp
 ***************************************************************************/
#include "gdal_polygonize_operator.h"
#include "gdal_operator_utils.h"

#include "operators/framework/rs_schema.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QFile>
#include <QFileInfo>
#include <QString>

#include <gdal.h>
#include <gdal_alg.h>
#include <ogr_api.h>
#include <ogr_srs_api.h>
#include <cpl_string.h>

#include <cmath>

namespace sicnu::operators::gdal {

namespace {

std::string vectorDriverName(const std::string& path) {
    const QString p = QString::fromStdString(path).toLower();
    if (p.endsWith(QLatin1String(".gpkg")))
        return "GPKG";
    if (p.endsWith(QLatin1String(".geojson")) || p.endsWith(QLatin1String(".json")))
        return "GeoJSON";
    return "ESRI Shapefile";
}

} // namespace

Json::Value GdalPolygonizeOperator::schema() const {
    using namespace schema;
    using namespace util;
    Json::Value props(Json::objectValue);
    props["input"] = makeRasterParam("input", "Single-band label or class raster");
    props["output"] = makeOutputParam("output", "Output polygon vector (.shp/.gpkg)", "shp");
    props["band"] = makeIntegerParam("band", "1-based band index", 1);
    props["field"] = makeStringParam("field", "Attribute field for pixel values", "DN");
    props["connected8"] = makeBooleanParam("connected8", "Use 8-connected components", true);

    Json::Value outputs(Json::objectValue);
    outputs["output"] = makeOutputParam("output", "Polygon dataset", "shp");
    outputs["features"] = makeIntegerParam("features", "Polygon feature count", 0);

    Json::Value root = makeRootSchema("gdal:polygonize", description(), props, outputs);
    root["required"] = makeRequired({"input", "output"});
    return root;
}

Json::Value GdalPolygonizeOperator::metadata() const {
    Json::Value meta(Json::objectValue);
    meta["group"] = group();
    meta["provider"] = "gdal";
    meta["tags"] = Json::Value(Json::arrayValue);
    meta["tags"].append("polygonize");
    meta["tags"].append("vectorize");
    meta["tags"].append("segments");
    meta["purpose"] = "Export class maps or segment labels as polygon vectors";
    meta["useCases"] = Json::Value(Json::arrayValue);
    meta["useCases"].append("Vectorize OBIA segment labels for map production");
    meta["useCases"].append("Export supervised classification results as shapefile");
    return meta;
}

Json::Value GdalPolygonizeOperator::run(const Json::Value& params,
                                        RSOperatorContext& context) {
    using namespace util;

    const std::string inputPath = requireString(params, "input");
    const std::string outputPath = requireString(params, "output");
    const int band = getInt(params, "band", 1);
    const std::string fieldName = getString(params, "field", "DN");
    const bool connected8 = getBool(params, "connected8", true);

    if (!fileExists(inputPath)) {
        throw RSOperatorError(ErrorCode::FileNotFound, "Input not found: " + inputPath);
    }

    ensureGdalInit();
    GDALAllRegister();
    OGRRegisterAll();

    GDALDatasetH srcDs = GDALOpen(inputPath.c_str(), GA_ReadOnly);
    if (!srcDs) {
        throw RSOperatorError(ErrorCode::GdalError, "Failed to open raster: " + inputPath);
    }

    const int bandCount = GDALGetRasterCount(srcDs);
    if (band < 1 || band > bandCount) {
        GDALClose(srcDs);
        throw RSOperatorError(ErrorCode::InvalidParameter, "Band out of range");
    }

    GDALRasterBandH srcBand = GDALGetRasterBand(srcDs, band);
    if (!srcBand) {
        GDALClose(srcDs);
        throw RSOperatorError(ErrorCode::GdalError, "Failed to get band");
    }

    context.reportProgress(0.1, "Creating output vector");

    // Remove existing output if shapefile
    {
        QFileInfo fi(QString::fromStdString(outputPath));
        if (fi.exists()) {
            // Best-effort remove for overwrite
            QFile::remove(QString::fromStdString(outputPath));
            if (fi.suffix().toLower() == QLatin1String("shp")) {
                const QString base = fi.path() + QLatin1Char('/') + fi.completeBaseName();
                QFile::remove(base + QStringLiteral(".dbf"));
                QFile::remove(base + QStringLiteral(".shx"));
                QFile::remove(base + QStringLiteral(".prj"));
                QFile::remove(base + QStringLiteral(".cpg"));
            }
        }
    }

    const std::string driverName = vectorDriverName(outputPath);
    GDALDriverH drv = GDALGetDriverByName(driverName.c_str());
    if (!drv) {
        GDALClose(srcDs);
        throw RSOperatorError(ErrorCode::GdalError, "Vector driver not available: " + driverName);
    }

    GDALDatasetH dstDs = GDALCreate(drv, outputPath.c_str(), 0, 0, 0, GDT_Unknown, nullptr);
    if (!dstDs) {
        GDALClose(srcDs);
        throw RSOperatorError(ErrorCode::GdalError, "Failed to create vector: " + outputPath);
    }

    // Copy SRS from raster
    const char* proj = GDALGetProjectionRef(srcDs);
    OGRSpatialReferenceH srs = nullptr;
    if (proj && proj[0] != '\0') {
        srs = OSRNewSpatialReference(nullptr);
        if (OSRSetFromUserInput(srs, proj) != OGRERR_NONE) {
            OSRDestroySpatialReference(srs);
            srs = nullptr;
        }
    }

    OGRLayerH layer = GDALDatasetCreateLayer(dstDs, "polygons", srs, wkbPolygon, nullptr);
    if (srs)
        OSRDestroySpatialReference(srs);
    if (!layer) {
        GDALClose(dstDs);
        GDALClose(srcDs);
        throw RSOperatorError(ErrorCode::GdalError, "Failed to create layer");
    }

    OGRFieldDefnH field = OGR_Fld_Create(fieldName.c_str(), OFTInteger64);
    if (OGR_L_CreateField(layer, field, TRUE) != OGRERR_NONE) {
        OGR_Fld_Destroy(field);
        GDALClose(dstDs);
        GDALClose(srcDs);
        throw RSOperatorError(ErrorCode::GdalError, "Failed to create attribute field");
    }
    OGR_Fld_Destroy(field);

    char** papszOptions = nullptr;
    if (connected8)
        papszOptions = CSLSetNameValue(papszOptions, "8CONNECTED", "8");

    context.reportProgress(0.3, "Running GDALPolygonize");
    context.throwIfCancelled();

    const int fieldIndex = 0;
    const CPLErr err = GDALPolygonize(srcBand, nullptr, layer, fieldIndex, papszOptions,
                                      util::gdalProgressCallback, &context);
    CSLDestroy(papszOptions);

    const GIntBig featureCount = OGR_L_GetFeatureCount(layer, TRUE);

    GDALClose(dstDs);
    GDALClose(srcDs);

    if (err != CE_None) {
        throw RSOperatorError(ErrorCode::GdalError, "GDALPolygonize failed for: " + inputPath);
    }

    context.reportProgress(1.0, "Polygonize complete");

    Json::Value result(Json::objectValue);
    result["output"] = outputPath;
    result["features"] = static_cast<Json::Int64>(featureCount);
    result["field"] = fieldName;
    result["driver"] = driverName;
    return result;
}

} // namespace sicnu::operators::gdal
