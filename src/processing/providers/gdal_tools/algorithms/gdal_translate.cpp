// src/processing/providers/gdal_tools/algorithms/gdal_translate.cpp
#include "gdal_translate.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>

void GdalTranslateAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addInputRasterLayerParameter("INPUT", "Input raster layer");

    QStringList formats;
    formats << "GTiff" << "HFA" << "ENVI" << "AAIGrid" << "PNG" << "JPEG" << "NetCDF";
    addParameter(new QgsProcessingParameterEnum("FORMAT", "Output format", formats, false, 0));

    addParameter(new QgsProcessingParameterString("EXTRA", "Additional GDAL arguments", QVariant(), false, true));
    addOutputRasterLayerParameter("OUTPUT", "Output raster layer");
}

QStringList GdalTranslateAlgorithm::buildArgs(const QVariantMap &parameters,
                                                QgsProcessingContext &context,
                                                QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;
    QStringList formats = { "GTiff", "HFA", "ENVI", "AAIGrid", "PNG", "JPEG", "NetCDF" };
    int formatIdx = parameters.value("FORMAT").toInt();
    QString formatStr = (formatIdx >= 0 && formatIdx < formats.size()) ? formats[formatIdx] : QStringLiteral("GTiff");
    args << "-of" << formatStr;

    if (parameters.contains("EXTRA") && !parameters.value("EXTRA").toString().isEmpty()) {
        args << QProcess::splitCommand(parameters.value("EXTRA").toString());
    }

    args << rasterLayerSource(parameters.value("INPUT"));

    args << parameters.value("OUTPUT").toString();

    return args;
}
