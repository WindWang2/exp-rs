// src/processing/providers/otb_tools/algorithms/otb_local_statistic_extraction.cpp
#include "otb_local_statistic_extraction.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>

void OtbLocalStatisticExtractionAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    Q_UNUSED(configuration);

    addParameter(new QgsProcessingParameterRasterLayer("INPUT", QObject::tr("Input raster")));

    addParameter(new QgsProcessingParameterNumber(
        "CHANNEL", QObject::tr("Selected channel"),
        Qgis::ProcessingNumberParameterType::Integer, 1, false, 1));

    addParameter(new QgsProcessingParameterNumber(
        "RADIUS", QObject::tr("Neighborhood radius"),
        Qgis::ProcessingNumberParameterType::Integer, 3, false, 1));

    addParameter(new QgsProcessingParameterRasterDestination("OUTPUT", QObject::tr("Output raster (statistics)")));
}

QStringList OtbLocalStatisticExtractionAlgorithm::buildArgs(const QVariantMap &parameters,
                                                              QgsProcessingContext &context,
                                                              QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;
    args << "-in" << rasterLayerSource(parameters.value("INPUT"));
    args << "-channel" << QString::number(parameters.value("CHANNEL").toInt());
    args << "-radius" << QString::number(parameters.value("RADIUS").toInt());
    args << "-out" << parameters.value("OUTPUT").toString();

    return args;
}