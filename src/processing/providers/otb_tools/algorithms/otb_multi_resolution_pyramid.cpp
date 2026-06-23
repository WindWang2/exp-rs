// src/processing/providers/otb_tools/algorithms/otb_multi_resolution_pyramid.cpp
#include "otb_multi_resolution_pyramid.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>

void OtbMultiResolutionPyramidAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    Q_UNUSED(configuration);

    addParameter(new QgsProcessingParameterRasterLayer("INPUT", "Input raster"));
    addParameter(new QgsProcessingParameterNumber("LEVELS", "Number of resolution levels",
                                                  Qgis::ProcessingNumberParameterType::Integer, 5, false, 1, 20));

    QStringList methods;
    methods << "nearest" << "gaussian" << "mean";
    addParameter(new QgsProcessingParameterEnum("METHOD", "Resampling method", methods, false, 0));

    addParameter(new QgsProcessingParameterRasterDestination("OUTPUT", "Output raster (pyramid)"));
}

QStringList OtbMultiResolutionPyramidAlgorithm::buildArgs(const QVariantMap &parameters,
                                                           QgsProcessingContext &context,
                                                           QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;
    args << "-in" << rasterLayerSource(parameters.value("INPUT"));

    args << "-levels" << QString::number(parameters.value("LEVELS").toInt());

    QStringList methods = {"nearest", "gaussian", "mean"};
    QString selectedMethod = methods.value(parameters.value("METHOD").toInt(), "nearest");
    args << "-method" << selectedMethod;

    args << "-out" << parameters.value("OUTPUT").toString();

    return args;
}
