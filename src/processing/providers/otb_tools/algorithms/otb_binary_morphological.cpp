// src/processing/providers/otb_tools/algorithms/otb_binary_morphological.cpp
#include "otb_binary_morphological.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>

void OtbBinaryMorphologicalAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addParameter(new QgsProcessingParameterRasterLayer("INPUT", "Input raster (binary)"));
    addParameter(new QgsProcessingParameterNumber("RADIUS", "Structuring element radius",
                                                   Qgis::ProcessingNumberParameterType::Integer, 3, false, 1));

    QStringList operators;
    operators << "dilate" << "erode" << "opening" << "closing";
    addParameter(new QgsProcessingParameterEnum("OPERATOR", "Morphological operator", operators, false, 0));

    addParameter(new QgsProcessingParameterRasterDestination("OUTPUT", "Output raster"));
}

QStringList OtbBinaryMorphologicalAlgorithm::buildArgs(const QVariantMap &parameters,
                                                       QgsProcessingContext &context,
                                                       QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;
    args << "-in" << rasterLayerSource(parameters.value("INPUT"));
    args << "-struct" << QString::number(parameters.value("RADIUS").toInt());

    QStringList operators = {"dilate", "erode", "opening", "closing"};
    QString selectedOp = operators.value(parameters.value("OPERATOR").toInt(), "dilate");
    args << "-filter" << selectedOp;

    args << "-out" << parameters.value("OUTPUT").toString();

    return args;
}
