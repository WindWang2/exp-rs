// src/processing/providers/otb_tools/algorithms/otb_superimpose.cpp
#include "otb_superimpose.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>

void OtbSuperimposeAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addParameter(new QgsProcessingParameterRasterLayer("INPUT", "Input raster"));
    addParameter(new QgsProcessingParameterRasterLayer("REFERENCE", "Reference raster"));
    addParameter(new QgsProcessingParameterRasterDestination("OUTPUT", "Output raster"));
}

QStringList OtbSuperimposeAlgorithm::buildArgs(const QVariantMap &parameters,
                                               QgsProcessingContext &context,
                                               QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;
    args << "-in" << rasterLayerSource(parameters.value("INPUT"));
    args << "-ref" << rasterLayerSource(parameters.value("REFERENCE"));
    args << "-out" << parameters.value("OUTPUT").toString();

    return args;
}
