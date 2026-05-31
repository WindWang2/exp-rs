// src/processing/providers/gdal_tools/algorithms/ogr2ogr.cpp
#include "ogr2ogr.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsvectorlayer.h>

void Ogr2OgrAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    Q_UNUSED(configuration);

    addInputVectorLayerParameter("INPUT", "Input vector layer");

    QStringList formats;
    formats << "GPKG" << "ESRI Shapefile" << "GeoJSON" << "KML" << "GML"
            << "CSV" << "SQLite" << "FlatGeobuf";
    addParameter(new QgsProcessingParameterEnum("FORMAT", "Output format", formats, false, 0));

    // Layer selection
    addParameter(new QgsProcessingParameterString(
        "SRC_LAYER", "Source layer name (empty for default)", QVariant(), false, true));
    addParameter(new QgsProcessingParameterString(
        "DST_LAYER", "Destination layer name", QVariant(), false, true));

    // Spatial filtering (bbox as "xmin,ymin,xmax,ymax")
    addParameter(new QgsProcessingParameterString(
        "SPAT", "Spatial filter extent (xmin,ymin,xmax,ymax)", QVariant(), false, true));

    // Attribute filtering
    addParameter(new QgsProcessingParameterString(
        "WHERE", "Attribute filter (SQL WHERE clause)", QVariant(), false, true));

    // Coordinate transformation
    addCrsParameter("TARGET_CRS", "Target CRS (reproject output)");

    // Geometry type
    QStringList geomTypes;
    geomTypes << "Default" << "POINT" << "LINESTRING" << "POLYGON"
              << "MULTIPOINT" << "MULTILINESTRING" << "MULTIPOLYGON"
              << "GEOMETRYCOLLECTION" << "NONE";
    addParameter(new QgsProcessingParameterEnum(
        "GEOMETRY_TYPE", "Override geometry type", geomTypes, false, 0));

    // SQL query
    addParameter(new QgsProcessingParameterString(
        "SQL", "SQL query to execute on input", QVariant(), false, true));

    // Field selection
    addParameter(new QgsProcessingParameterString(
        "SELECT_FIELDS", "Fields to select (comma-separated, empty for all)",
        QVariant(), false, true));

    // Feature limit
    addParameter(new QgsProcessingParameterNumber(
        "LIMIT", "Maximum number of features to export (0 = no limit)",
        Qgis::ProcessingNumberParameterType::Integer, 0, false, 0));

    // Extra arguments
    addParameter(new QgsProcessingParameterString(
        "EXTRA", "Additional ogr2ogr arguments", QVariant(), false, true));

    // Overwrite option
    addParameter(new QgsProcessingParameterBoolean(
        "OVERWRITE", "Overwrite existing output file", true));

    addOutputVectorLayerParameter("OUTPUT", "Output vector layer");
}

QStringList Ogr2OgrAlgorithm::buildArgs(const QVariantMap &parameters,
                                          QgsProcessingContext &context,
                                          QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;

    // Output format
    QStringList formats;
    formats << "GPKG" << "ESRI Shapefile" << "GeoJSON" << "KML" << "GML"
            << "CSV" << "SQLite" << "FlatGeobuf";
    int formatIndex = parameters.value("FORMAT").toInt();
    QString format = formats.value(formatIndex, "GPKG");
    args << "-f" << format;

    // Overwrite
    if (parameters.value("OVERWRITE", true).toBool()) {
        args << "-overwrite";
    }

    // Coordinate transformation
    if (parameters.contains("TARGET_CRS") && !parameters.value("TARGET_CRS").toString().isEmpty()) {
        args << "-t_srs" << parameters.value("TARGET_CRS").toString();
    }

    // Spatial filtering (bbox)
    if (parameters.contains("SPAT") && !parameters.value("SPAT").toString().isEmpty()) {
        QStringList extent = parameters.value("SPAT").toString().split(",");
        if (extent.size() == 4) {
            args << "-spat" << extent[0] << extent[1] << extent[2] << extent[3];
        }
    }

    // Attribute filtering
    if (parameters.contains("WHERE") && !parameters.value("WHERE").toString().isEmpty()) {
        args << "-where" << parameters.value("WHERE").toString();
    }

    // Geometry type override
    QStringList geomTypes;
    geomTypes << "" << "POINT" << "LINESTRING" << "POLYGON"
              << "MULTIPOINT" << "MULTILINESTRING" << "MULTIPOLYGON"
              << "GEOMETRYCOLLECTION" << "NONE";
    int geomIndex = parameters.value("GEOMETRY_TYPE", 0).toInt();
    if (geomIndex > 0 && geomIndex < geomTypes.size()) {
        args << "-nlt" << geomTypes[geomIndex];
    }

    // Field selection
    if (parameters.contains("SELECT_FIELDS") && !parameters.value("SELECT_FIELDS").toString().isEmpty()) {
        args << "-select" << parameters.value("SELECT_FIELDS").toString();
    }

    // SQL query
    if (parameters.contains("SQL") && !parameters.value("SQL").toString().isEmpty()) {
        args << "-sql" << parameters.value("SQL").toString();
    }

    // Feature limit
    int limit = parameters.value("LIMIT", 0).toInt();
    if (limit > 0) {
        args << "-limit" << QString::number(limit);
    }

    // Extra arguments
    if (parameters.contains("EXTRA") && !parameters.value("EXTRA").toString().isEmpty()) {
        args << parameters.value("EXTRA").toString().split(" ");
    }

    // Destination layer name
    if (parameters.contains("DST_LAYER") && !parameters.value("DST_LAYER").toString().isEmpty()) {
        args << "-nln" << parameters.value("DST_LAYER").toString();
    }

    // Output file
    args << parameters.value("OUTPUT").toString();

    // Input file
    QVariant inputVar = parameters.value("INPUT");
    QString inputPath;
    if (inputVar.canConvert<QgsVectorLayer *>()) {
        inputPath = inputVar.value<QgsVectorLayer *>()->source();
    } else {
        inputPath = inputVar.toString();
    }
    args << inputPath;

    // Source layer name
    if (parameters.contains("SRC_LAYER") && !parameters.value("SRC_LAYER").toString().isEmpty()) {
        args << parameters.value("SRC_LAYER").toString();
    }

    return args;
}
