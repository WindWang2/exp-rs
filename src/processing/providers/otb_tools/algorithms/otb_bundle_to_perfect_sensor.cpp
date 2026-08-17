// src/processing/providers/otb_tools/algorithms/otb_bundle_to_perfect_sensor.cpp
#include "otb_bundle_to_perfect_sensor.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>

void OtbBundleToPerfectSensorAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addParameter(new QgsProcessingParameterRasterLayer("INPUT", "Input raster (multispectral)"));
    addParameter(new QgsProcessingParameterRasterLayer("PANCHROMATIC", "Panchromatic raster"));
    addParameter(new QgsProcessingParameterRasterDestination("OUTPUT", "Output raster"));
}

QStringList OtbBundleToPerfectSensorAlgorithm::buildArgs(const QVariantMap &parameters,
                                                         QgsProcessingContext &context,
                                                         QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;
    args << "-inxs" << rasterLayerSource(parameters.value("INPUT"));
    args << "-inp" << rasterLayerSource(parameters.value("PANCHROMATIC"));
    args << "-out" << parameters.value("OUTPUT").toString();

    return args;
}
