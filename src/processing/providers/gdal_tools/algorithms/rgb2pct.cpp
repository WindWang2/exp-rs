// src/processing/providers/gdal_tools/algorithms/rgb2pct.cpp
#include "rgb2pct.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>

void Rgb2PctAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    Q_UNUSED(configuration);

    addInputRasterLayerParameter("INPUT", QObject::tr("Input RGB raster"));

    QStringList formats;
    formats << "GTiff" << "HFA" << "ENVI" << "PNG";
    addParameter(new QgsProcessingParameterEnum("FORMAT", QObject::tr("Output format (-of)"),
                                                formats, false, 0));

    addParameter(new QgsProcessingParameterNumber(
        "COLORS", QObject::tr("Number of colors (-n)"), Qgis::ProcessingNumberParameterType::Integer,
        256, false, 2, 256));

    addParameter(new QgsProcessingParameterFile(
        "PCT", QObject::tr("External palette file (-pct)"),
        Qgis::ProcessingFileParameterBehavior::File, QString(), QVariant(), true,
        QObject::tr("Raster files (*.tif *.tiff)")));

    addParameter(new QgsProcessingParameterString(
        "CREATION_OPTIONS", QObject::tr("GeoTIFF creation options (--co)"),
        QVariant(), false, true));

    addOutputRasterLayerParameter("OUTPUT", QObject::tr("Output paletted raster"));
}

QStringList Rgb2PctAlgorithm::buildArgs(const QVariantMap &parameters,
                                        QgsProcessingContext &context,
                                        QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;

    const QString format = parameters.value("FORMAT").toString();
    if (!format.isEmpty()) {
        args << "-of" << format;
    }

    args << "-n" << QString::number(parameters.value("COLORS", 256).toInt());

    const QString pct = parameters.value("PCT").toString();
    if (!pct.isEmpty()) {
        args << "-pct" << pct;
    }

    const QString creationOptions = parameters.value("CREATION_OPTIONS").toString();
    if (!creationOptions.isEmpty()) {
        for (const QString &option : creationOptions.split(",", Qt::SkipEmptyParts)) {
            args << "--co" << option.trimmed();
        }
    }

    args << rasterLayerSource(parameters.value("INPUT"));
    args << parameters.value("OUTPUT").toString();

    return args;
}