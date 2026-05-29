// src/processing/providers/otb_tools/algorithms/otb_convert.cpp
#include "otb_convert.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>

void OtbConvertAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addParameter(new QgsProcessingParameterRasterLayer("INPUT", "Input raster"));

    QStringList types;
    types << "uint8" << "uint16" << "int16" << "float" << "double";
    addParameter(new QgsProcessingParameterEnum("OUTPUT_TYPE", "Output pixel type", types, false, 0));

    addParameter(new QgsProcessingParameterRasterDestination("OUTPUT", "Output raster"));
}

QStringList OtbConvertAlgorithm::buildArgs(const QVariantMap &parameters,
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

    QStringList types = {"uint8", "uint16", "int16", "float", "double"};
    QString selectedType = types.value(parameters.value("OUTPUT_TYPE").toInt(), "uint8");
    args << "-type" << selectedType;

    args << "-out" << parameters.value("OUTPUT").toString();

    return args;
}
