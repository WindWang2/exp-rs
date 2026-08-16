// src/processing/providers/otb_tools/algorithms/otb_rescale.cpp
#include "otb_rescale.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>

void OtbRescaleAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addParameter(new QgsProcessingParameterRasterLayer("INPUT", "Input raster"));
    addParameter(new QgsProcessingParameterNumber("OUTPUT_MIN", "Output minimum value",
                                                   Qgis::ProcessingNumberParameterType::Double, 0.0, false));
    addParameter(new QgsProcessingParameterNumber("OUTPUT_MAX", "Output maximum value",
                                                   Qgis::ProcessingNumberParameterType::Double, 255.0, false));
    addParameter(new QgsProcessingParameterRasterDestination("OUTPUT", "Output raster"));
}

QStringList OtbRescaleAlgorithm::buildArgs(const QVariantMap &parameters,
                                           QgsProcessingContext &context,
                                           QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;
    args << "-in" << rasterLayerSource(parameters.value("INPUT"));
    args << "-outmin" << QString::number(parameters.value("OUTPUT_MIN").toDouble());
    args << "-outmax" << QString::number(parameters.value("OUTPUT_MAX").toDouble());
    args << "-out" << parameters.value("OUTPUT").toString();

    return args;
}
