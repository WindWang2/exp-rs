// src/processing/providers/otb_tools/algorithms/otb_radiometric_indices.cpp
#include "otb_radiometric_indices.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>

void OtbRadiometricIndicesAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addParameter(new QgsProcessingParameterRasterLayer("INPUT", "Input raster"));
    addParameter(new QgsProcessingParameterString("LIST", "Indices list (e.g. NDVI)", "NDVI", false, false));
    addParameter(new QgsProcessingParameterRasterDestination("OUTPUT", "Output raster (indices)"));
}

QStringList OtbRadiometricIndicesAlgorithm::buildArgs(const QVariantMap &parameters,
                                                      QgsProcessingContext &context,
                                                      QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;
    args << "-in" << rasterLayerSource(parameters.value("INPUT"));
    args << "-list" << parameters.value("LIST").toString();
    args << "-out" << parameters.value("OUTPUT").toString();

    return args;
}
