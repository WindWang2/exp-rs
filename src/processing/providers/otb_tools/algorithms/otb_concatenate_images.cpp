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
