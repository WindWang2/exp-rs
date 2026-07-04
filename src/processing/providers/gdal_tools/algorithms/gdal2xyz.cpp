// src/processing/providers/gdal_tools/algorithms/gdal2xyz.cpp
#include "gdal2xyz.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>
#include <QRegularExpression>

void Gdal2XyzAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    Q_UNUSED(configuration);

    addInputRasterLayerParameter("INPUT", QObject::tr("Input raster"));

    addParameter(new QgsProcessingParameterBoolean(
        "CSV", QObject::tr("Use comma delimiter (-csv)"), false));

    addParameter(new QgsProcessingParameterBoolean(
        "SKIPNODATA", QObject::tr("Skip nodata pixels (-skipnodata)"), false));

    addParameter(new QgsProcessingParameterBoolean(
        "ALL_BANDS", QObject::tr("Export all bands (-allbands)"), false));

    addParameter(new QgsProcessingParameterNumber(
        "BAND", QObject::tr("Band to export (-b)"), Qgis::ProcessingNumberParameterType::Integer,
        1, true, 1));

    addParameter(new QgsProcessingParameterNumber(
        "SKIP", QObject::tr("Rows/cols to skip (-skip)"), Qgis::ProcessingNumberParameterType::Integer,
        QVariant(), true, 0));

    addParameter(new QgsProcessingParameterString(
        "SRCWIN", QObject::tr("Source window in pixels: xoff yoff xsize ysize (-srcwin)"),
        QVariant(), false, true));

    addParameter(new QgsProcessingParameterNumber(
        "SRC_NODATA", QObject::tr("Source nodata value (-srcnodata)"),
        Qgis::ProcessingNumberParameterType::Double, QVariant(), true));

    addParameter(new QgsProcessingParameterNumber(
        "DST_NODATA", QObject::tr("Destination nodata value (-dstnodata)"),
        Qgis::ProcessingNumberParameterType::Double, QVariant(), true));

    addParameter(new QgsProcessingParameterFileDestination(
        "OUTPUT", QObject::tr("XYZ output file"), QObject::tr("XYZ files (*.xyz);;Text files (*.txt)")));
}

QStringList Gdal2XyzAlgorithm::buildArgs(const QVariantMap &parameters,
                                         QgsProcessingContext &context,
                                         QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;

    if (parameters.value("CSV", false).toBool()) {
        args << "-csv";
    }

    if (parameters.value("SKIPNODATA", false).toBool()) {
        args << "-skipnodata";
    }

    if (parameters.value("ALL_BANDS", false).toBool()) {
        args << "-allbands";
    } else if (parameters.contains("BAND") && !parameters.value("BAND").isNull()) {
        args << "-b" << QString::number(parameters.value("BAND").toInt());
    }

    if (parameters.contains("SKIP") && !parameters.value("SKIP").isNull()) {
        args << "-skip" << QString::number(parameters.value("SKIP").toInt());
    }

    const QString srcWin = parameters.value("SRCWIN").toString();
    if (!srcWin.isEmpty()) {
        const QStringList parts = srcWin.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (parts.size() == 4) {
            args << "-srcwin" << parts;
        }
    }

    if (parameters.contains("SRC_NODATA") && !parameters.value("SRC_NODATA").isNull()) {
        args << "-srcnodata" << QString::number(parameters.value("SRC_NODATA").toDouble(), 'g', 15);
    }

    if (parameters.contains("DST_NODATA") && !parameters.value("DST_NODATA").isNull()) {
        args << "-dstnodata" << QString::number(parameters.value("DST_NODATA").toDouble(), 'g', 15);
    }

    args << rasterLayerSource(parameters.value("INPUT"));
    args << parameters.value("OUTPUT").toString();

    return args;
}