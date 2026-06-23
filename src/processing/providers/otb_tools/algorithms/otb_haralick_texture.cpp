// src/processing/providers/otb_tools/algorithms/otb_haralick_texture.cpp
#include "otb_haralick_texture.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>

void OtbHaralickTextureAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addParameter(new QgsProcessingParameterRasterLayer("INPUT", "Input raster"));
    addParameter(new QgsProcessingParameterNumber("RADIUS", "Texture radius",
                                                   Qgis::ProcessingNumberParameterType::Integer, 3, false, 1));
    addParameter(new QgsProcessingParameterRasterDestination("OUTPUT", "Output raster (textures)"));
}

QStringList OtbHaralickTextureAlgorithm::buildArgs(const QVariantMap &parameters,
                                                   QgsProcessingContext &context,
                                                   QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;
    args << "-in" << rasterLayerSource(parameters.value("INPUT"));
    args << "-parameters.radius" << QString::number(parameters.value("RADIUS").toInt());
    args << "-out" << parameters.value("OUTPUT").toString();

    return args;
}
