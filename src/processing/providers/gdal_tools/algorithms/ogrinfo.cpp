// src/processing/providers/gdal_tools/algorithms/ogrinfo.cpp
#include "ogrinfo.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsvectorlayer.h>

void OgrInfoAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addInputVectorLayerParameter("INPUT", "Input vector layer");
    addParameter(new QgsProcessingParameterFileDestination("OUTPUT", "Info output (text file)",
                                                           "Text files (*.txt)"));
}

QStringList OgrInfoAlgorithm::buildArgs(const QVariantMap &parameters,
                                         QgsProcessingContext &context,
                                         QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;
    args << "-al";

    QVariant inputVar = parameters.value("INPUT");
    QString inputPath;
    if (inputVar.canConvert<QgsVectorLayer *>()) {
        inputPath = inputVar.value<QgsVectorLayer *>()->source();
    } else {
        inputPath = inputVar.toString();
    }
    args << inputPath;

    return args;
}
