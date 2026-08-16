// src/processing/providers/otb_tools/algorithms/otb_ortho_rectification.cpp
#include "otb_ortho_rectification.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>

void OtbOrthoRectificationAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addParameter(new QgsProcessingParameterRasterLayer("INPUT", "Input raster"));
    addParameter(new QgsProcessingParameterNumber("ELEVATION", "Average elevation (meters)",
                                                   Qgis::ProcessingNumberParameterType::Double, 0.0, false));
    addParameter(new QgsProcessingParameterRasterDestination("OUTPUT", "Output raster (rectified)"));
}

QStringList OtbOrthoRectificationAlgorithm::buildArgs(const QVariantMap &parameters,
                                                      QgsProcessingContext &context,
                                                      QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;
    args << "-io.in" << rasterLayerSource(parameters.value("INPUT"));
    args << "-elev.default" << QString::number(parameters.value("ELEVATION").toDouble());
    args << "-io.out" << parameters.value("OUTPUT").toString();

    return args;
}
