// src/processing/providers/otb_tools/algorithms/otb_lsms.cpp
#include "otb_lsms.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>

void OtbLsmsAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addParameter(new QgsProcessingParameterRasterLayer("INPUT", "Input raster"));
    addParameter(new QgsProcessingParameterNumber("SPATIAL_RADIUS", "Spatial radius",
                                                   Qgis::ProcessingNumberParameterType::Integer, 5, false, 1));
    addParameter(new QgsProcessingParameterNumber("RANGE_RADIUS", "Range radius",
                                                   Qgis::ProcessingNumberParameterType::Double, 15.0, false, 0.01));
    addParameter(new QgsProcessingParameterRasterDestination("OUTPUT", "Output raster"));
}

QStringList OtbLsmsAlgorithm::buildArgs(const QVariantMap &parameters,
                                        QgsProcessingContext &context,
                                        QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;
    args << "-in" << rasterLayerSource(parameters.value("INPUT"));
    args << "-spatialr" << QString::number(parameters.value("SPATIAL_RADIUS").toInt());
    args << "-ranger" << QString::number(parameters.value("RANGE_RADIUS").toDouble(), 'f', 2);
    args << "-minsize" << QString::number(parameters.value("MIN_REGION_SIZE", 100).toInt());
    args << "-out" << parameters.value("OUTPUT").toString();

    return args;
}
