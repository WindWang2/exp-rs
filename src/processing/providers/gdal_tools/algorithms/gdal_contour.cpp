// src/processing/providers/gdal_tools/algorithms/gdal_contour.cpp
#include "gdal_contour.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>

void GdalContourAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addInputRasterLayerParameter("INPUT", "Input raster layer");
    addParameter(new QgsProcessingParameterNumber("INTERVAL", "Contour interval",
                                                   Qgis::ProcessingNumberParameterType::Double, 10.0, false, 0.000001));
    addOutputVectorLayerParameter("OUTPUT", "Output vector layer (contour lines)");
}

QStringList GdalContourAlgorithm::buildArgs(const QVariantMap &parameters,
                                             QgsProcessingContext &context,
                                             QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;
    args << "-a" << "elevation";

    double interval = parameters.value("INTERVAL", 10.0).toDouble();
    if (interval <= 0.0) {
        throw QgsProcessingException("Contour interval must be strictly positive (> 0).");
    }
    args << "-i" << QString::number(interval);

    args << rasterLayerSource(parameters.value("INPUT"));
    args << parameters.value("OUTPUT").toString();

    return args;
}
