// src/processing/providers/otb_tools/algorithms/otb_superimpose.cpp
#include "otb_superimpose.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>

void OtbSuperimposeAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addParameter(new QgsProcessingParameterRasterLayer("INPUT", "Input raster"));
    addParameter(new QgsProcessingParameterRasterLayer("REFERENCE", "Reference raster"));
    addParameter(new QgsProcessingParameterRasterDestination("OUTPUT", "Output raster"));
}

QStringList OtbSuperimposeAlgorithm::buildArgs(const QVariantMap &parameters,
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

    QVariant refVar = parameters.value("REFERENCE");
    QString refPath;
    if (refVar.canConvert<QgsRasterLayer *>()) {
        refPath = refVar.value<QgsRasterLayer *>()->source();
    } else {
        refPath = refVar.toString();
    }
    args << "-ref" << refPath;
    args << "-out" << parameters.value("OUTPUT").toString();

    return args;
}
