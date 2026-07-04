// src/processing/providers/otb_tools/algorithms/otb_multivariate_alteration_detector.cpp
#include "otb_multivariate_alteration_detector.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>

void OtbMultivariateAlterationDetectorAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    Q_UNUSED(configuration);

    addParameter(new QgsProcessingParameterRasterLayer("INPUT1", QObject::tr("Input raster (before)")));
    addParameter(new QgsProcessingParameterRasterLayer("INPUT2", QObject::tr("Input raster (after)")));
    addParameter(new QgsProcessingParameterRasterDestination("OUTPUT", QObject::tr("Output raster (change maps)")));
}

QStringList OtbMultivariateAlterationDetectorAlgorithm::buildArgs(const QVariantMap &parameters,
                                                                   QgsProcessingContext &context,
                                                                   QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;
    args << "-in1" << rasterLayerSource(parameters.value("INPUT1"));
    args << "-in2" << rasterLayerSource(parameters.value("INPUT2"));
    args << "-out" << parameters.value("OUTPUT").toString();

    return args;
}