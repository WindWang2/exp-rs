// src/processing/providers/gdal_tools/algorithms/gdal_edit.cpp
#include "gdal_edit.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>

void GdalEditAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    Q_UNUSED(configuration);

    addInputRasterLayerParameter("INPUT", QObject::tr("Input raster (edited in place)"));

    addCrsParameter("TARGET_CRS", QObject::tr("Assign SRS (-a_srs)"));

    addParameter(new QgsProcessingParameterNumber(
        "NODATA", QObject::tr("Assign nodata value (-a_nodata)"),
        Qgis::ProcessingNumberParameterType::Double, QVariant(), true));

    addParameter(new QgsProcessingParameterNumber(
        "X_RES", QObject::tr("Pixel width (-tr)"), Qgis::ProcessingNumberParameterType::Double,
        QVariant(), true));
    addParameter(new QgsProcessingParameterNumber(
        "Y_RES", QObject::tr("Pixel height (-tr)"), Qgis::ProcessingNumberParameterType::Double,
        QVariant(), true));

    addParameter(new QgsProcessingParameterString(
        "UNITS", QObject::tr("Assign raster units (-units)"), QVariant(), false, true));

    addParameter(new QgsProcessingParameterBoolean(
        "READ_ONLY", QObject::tr("Open in read-only mode (-ro)"), false));

    addParameter(new QgsProcessingParameterBoolean(
        "STATS", QObject::tr("Compute raster statistics (-stats)"), false));

    addParameter(new QgsProcessingParameterString(
        "EXTRA", QObject::tr("Additional GDAL arguments"), QVariant(), false, true));
}

QStringList GdalEditAlgorithm::buildArgs(const QVariantMap &parameters,
                                         QgsProcessingContext &context,
                                         QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;

    if (parameters.value("READ_ONLY", false).toBool()) {
        args << "-ro";
    }

    const QString targetCrs = parameters.value("TARGET_CRS").toString();
    if (!targetCrs.isEmpty()) {
        args << "-a_srs" << targetCrs;
    }

    if (parameters.contains("NODATA") && !parameters.value("NODATA").isNull()) {
        args << "-a_nodata" << QString::number(parameters.value("NODATA").toDouble(), 'g', 15);
    }

    const bool hasXRes = parameters.contains("X_RES") && !parameters.value("X_RES").isNull();
    const bool hasYRes = parameters.contains("Y_RES") && !parameters.value("Y_RES").isNull();
    if (hasXRes != hasYRes) {
        throw QgsProcessingException(
            QObject::tr("Both X_RES and Y_RES must be specified together for -tr"));
    }
    if (hasXRes && hasYRes) {
        args << "-tr"
             << QString::number(parameters.value("X_RES").toDouble(), 'g', 15)
             << QString::number(parameters.value("Y_RES").toDouble(), 'g', 15);
    }

    const QString units = parameters.value("UNITS").toString();
    if (!units.isEmpty()) {
        args << "-units" << units;
    }

    if (parameters.value("STATS", false).toBool()) {
        args << "-stats";
    }

    if (parameters.contains("EXTRA") && !parameters.value("EXTRA").toString().isEmpty()) {
        args << QProcess::splitCommand(parameters.value("EXTRA").toString());
    }

    args << rasterLayerSource(parameters.value("INPUT"));

    return args;
}