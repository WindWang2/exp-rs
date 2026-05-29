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

    QVariant inputVar = parameters.value("INPUT");
    QString inputPath;
    if (inputVar.canConvert<QgsRasterLayer *>()) {
        inputPath = inputVar.value<QgsRasterLayer *>()->source();
    } else {
        inputPath = inputVar.toString();
    }
    args << "-in" << inputPath;

    QVariant panVar = parameters.value("PANCHROMATIC");
    QString panPath;
    if (panVar.canConvert<QgsRasterLayer *>()) {
        panPath = panVar.value<QgsRasterLayer *>()->source();
    } else {
        panPath = panVar.toString();
    }
    args << "-pan" << panPath;
    args << "-out" << parameters.value("OUTPUT").toString();

    return args;
}
