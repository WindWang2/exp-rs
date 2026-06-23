// src/processing/providers/otb_tools/algorithms/otb_concatenate_images.cpp
#include "otb_concatenate_images.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>

void OtbConcatenateImagesAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addParameter(new QgsProcessingParameterRasterLayer("INPUT", "Input rasters (multiple)"));
    addParameter(new QgsProcessingParameterRasterDestination("OUTPUT", "Output raster"));
}

QStringList OtbConcatenateImagesAlgorithm::buildArgs(const QVariantMap &parameters,
                                                     QgsProcessingContext &context,
                                                     QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;
    args << "-in" << rasterLayerSource(parameters.value("INPUT"));
    args << "-out" << parameters.value("OUTPUT").toString();

    return args;
}
