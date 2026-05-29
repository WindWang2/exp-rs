// src/processing/providers/otb_tools/algorithms/otb_feature_extraction.cpp
#include "otb_feature_extraction.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>

void OtbFeatureExtractionAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addParameter(new QgsProcessingParameterRasterLayer("INPUT", "Input raster"));
    addParameter(new QgsProcessingParameterRasterDestination("OUTPUT", "Output raster (features)"));
}

QStringList OtbFeatureExtractionAlgorithm::buildArgs(const QVariantMap &parameters,
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
    args << "-out" << parameters.value("OUTPUT").toString();

    return args;
}
