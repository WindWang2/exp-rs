// src/processing/providers/gdal_tools/algorithms/gdal_sieve.cpp
#include "gdal_sieve.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>

void GdalSieveAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addInputRasterLayerParameter("INPUT", "Input raster layer");

    addParameter(new QgsProcessingParameterNumber("THRESHOLD", "Minimum polygon size (pixels)",
                                                   Qgis::ProcessingNumberParameterType::Integer,
                                                   2, false, 0));

    addParameter(new QgsProcessingParameterString("EXTRA", "Additional GDAL arguments",
                                                   QVariant(), false, true));

    addOutputRasterLayerParameter("OUTPUT", "Output sieved raster");
}

QStringList GdalSieveAlgorithm::buildArgs(const QVariantMap &parameters,
                                            QgsProcessingContext &context,
                                            QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;

    int threshold = parameters.value("THRESHOLD", 2).toInt();
    args << "-st" << QString::number(threshold);

    args << rasterLayerSource(parameters.value("INPUT"));

    args << parameters.value("OUTPUT").toString();

    if (parameters.contains("EXTRA") && !parameters.value("EXTRA").toString().isEmpty()) {
        args << QProcess::splitCommand(parameters.value("EXTRA").toString());
    }

    return args;
}
