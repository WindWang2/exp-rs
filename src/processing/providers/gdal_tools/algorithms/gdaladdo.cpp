// src/processing/providers/gdal_tools/algorithms/gdaladdo.cpp
#include "gdaladdo.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>

void GdalAddoAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    Q_UNUSED(configuration);

    addInputRasterLayerParameter("INPUT", QObject::tr("Input raster"));

    addParameter(new QgsProcessingParameterNumber(
        "LEVELS", QObject::tr("Overview level"),
        Qgis::ProcessingNumberParameterType::Integer, 2, false, 1, 1024));

    addParameter(new QgsProcessingParameterEnum("RESAMPLING", QObject::tr("Resampling"),
        QStringList{"NEAREST", "AVERAGE", "BILINEAR", "CUBIC"}, false, 0));
}

QStringList GdalAddoAlgorithm::buildArgs(const QVariantMap &parameters,
                                           QgsProcessingContext &context,
                                           QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    static const QStringList resamplingMethods{"nearest", "average", "bilinear", "cubic"};

    QStringList args;
    args << "-r" << resamplingMethods.value(parameters.value("RESAMPLING").toInt(), "nearest");
    args << rasterLayerSource(parameters.value("INPUT"));
    args << QString::number(parameters.value("LEVELS").toInt());

    return args;
}