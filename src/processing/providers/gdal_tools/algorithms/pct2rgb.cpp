// src/processing/providers/gdal_tools/algorithms/pct2rgb.cpp
#include "pct2rgb.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>

void Pct2RgbAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    Q_UNUSED(configuration);

    addInputRasterLayerParameter("INPUT", QObject::tr("Input paletted raster"));

    QStringList formats;
    formats << "GTiff" << "HFA" << "ENVI" << "PNG" << "JPEG";
    addParameter(new QgsProcessingParameterEnum("FORMAT", QObject::tr("Output format (-of)"),
                                                formats, false, 0));

    addParameter(new QgsProcessingParameterBoolean(
        "RGBA", QObject::tr("Generate RGBA output (-rgba)"), false));

    addParameter(new QgsProcessingParameterNumber(
        "BAND", QObject::tr("Band to convert (-b)"), Qgis::ProcessingNumberParameterType::Integer,
        1, false, 1));

    addParameter(new QgsProcessingParameterFile(
        "PCT", QObject::tr("External palette file (-pct)"),
        Qgis::ProcessingFileParameterBehavior::File, QString(), QVariant(), true,
        QObject::tr("Raster files (*.tif *.tiff)")));

    addOutputRasterLayerParameter("OUTPUT", QObject::tr("Output RGB raster"));
}

QStringList Pct2RgbAlgorithm::buildArgs(const QVariantMap &parameters,
                                        QgsProcessingContext &context,
                                        QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;
    QStringList formats = { "GTiff", "HFA", "ENVI", "PNG", "JPEG" };
    int formatIdx = parameters.value("FORMAT").toInt();
    QString format = (formatIdx >= 0 && formatIdx < formats.size()) ? formats[formatIdx] : QStringLiteral("GTiff");
    args << "-of" << format;

    if (parameters.value("RGBA", false).toBool()) {
        args << "-rgba";
    }

    args << "-b" << QString::number(parameters.value("BAND", 1).toInt());

    const QString pct = parameters.value("PCT").toString();
    if (!pct.isEmpty()) {
        args << "-pct" << pct;
    }

    args << rasterLayerSource(parameters.value("INPUT"));
    args << parameters.value("OUTPUT").toString();

    return args;
}