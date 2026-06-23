// src/processing/providers/otb_tools/algorithms/otb_kmeans_classification.cpp
#include "otb_kmeans_classification.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>

void OtbKMeansClassificationAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addParameter(new QgsProcessingParameterRasterLayer("INPUT", "Input raster"));
    addParameter(new QgsProcessingParameterNumber("NUM_CLASSES", "Number of classes",
                                                   Qgis::ProcessingNumberParameterType::Integer, 5, false, 2));
    addParameter(new QgsProcessingParameterRasterDestination("OUTPUT", "Output raster (classified)"));
}

QStringList OtbKMeansClassificationAlgorithm::buildArgs(const QVariantMap &parameters,
                                                        QgsProcessingContext &context,
                                                        QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;
    args << "-in" << rasterLayerSource(parameters.value("INPUT"));
    args << "-nc" << QString::number(parameters.value("NUM_CLASSES").toInt());
    args << "-out" << parameters.value("OUTPUT").toString();

    return args;
}
