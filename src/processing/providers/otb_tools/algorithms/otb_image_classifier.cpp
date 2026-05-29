// src/processing/providers/otb_tools/algorithms/otb_image_classifier.cpp
#include "otb_image_classifier.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>

void OtbImageClassifierAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addParameter(new QgsProcessingParameterRasterLayer("INPUT", "Input raster"));
    addParameter(new QgsProcessingParameterFile("MODEL", "Trained model file",
                                                 QgsProcessingParameterFile::File,
                                                 "Model files (*.xml *.txt)"));
    addParameter(new QgsProcessingParameterRasterDestination("OUTPUT", "Output raster (classified)"));
}

QStringList OtbImageClassifierAlgorithm::buildArgs(const QVariantMap &parameters,
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
    args << "-model" << parameters.value("MODEL").toString();
    args << "-out" << parameters.value("OUTPUT").toString();

    return args;
}
