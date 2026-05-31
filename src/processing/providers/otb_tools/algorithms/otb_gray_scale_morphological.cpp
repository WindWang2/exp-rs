// src/processing/providers/otb_tools/algorithms/otb_gray_scale_morphological.cpp
#include "otb_gray_scale_morphological.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>

void OtbGrayScaleMorphologicalAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    Q_UNUSED(configuration);

    addParameter(new QgsProcessingParameterRasterLayer("INPUT", "Input raster"));
    addParameter(new QgsProcessingParameterNumber("RADIUS", "Structuring element radius",
                                                  Qgis::ProcessingNumberParameterType::Integer, 3, false, 1));

    QStringList operators;
    operators << "dilate" << "erode" << "opening" << "closing";
    addParameter(new QgsProcessingParameterEnum("OPERATOR", "Morphological operator", operators, false, 0));

    addParameter(new QgsProcessingParameterRasterDestination("OUTPUT", "Output raster"));
}

QStringList OtbGrayScaleMorphologicalAlgorithm::buildArgs(const QVariantMap &parameters,
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
    args << "-struct" << QString::number(parameters.value("RADIUS").toInt());

    QStringList operators = {"dilate", "erode", "opening", "closing"};
    QString selectedOp = operators.value(parameters.value("OPERATOR").toInt(), "dilate");
    args << "-filter" << selectedOp;

    args << "-out" << parameters.value("OUTPUT").toString();

    return args;
}
