// src/processing/providers/gdal_tools/algorithms/gdal_fillnodata.cpp
#include "gdal_fillnodata.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>

void GdalFillNodataAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addInputRasterLayerParameter("INPUT", "Input raster layer");

    addParameter(new QgsProcessingParameterNumber("MAX_DISTANCE", "Maximum search distance (pixels)",
                                                   Qgis::ProcessingNumberParameterType::Double,
                                                   QVariant(), false, 0.0));

    addParameter(new QgsProcessingParameterNumber("SMOOTH_ITERATIONS", "Number of smoothing iterations",
                                                   Qgis::ProcessingNumberParameterType::Integer,
                                                   0, false, 0));

    addParameter(new QgsProcessingParameterString("EXTRA", "Additional GDAL arguments",
                                                   QVariant(), false, true));

    addOutputRasterLayerParameter("OUTPUT", "Output filled raster");
}

QStringList GdalFillNodataAlgorithm::buildArgs(const QVariantMap &parameters,
                                                 QgsProcessingContext &context,
                                                 QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;

    if (parameters.contains("MAX_DISTANCE") && !parameters.value("MAX_DISTANCE").toString().isEmpty()) {
        double maxDist = parameters.value("MAX_DISTANCE").toDouble();
        if (maxDist > 0) {
            args << "-md" << QString::number(maxDist);
        }
    }

    if (parameters.contains("SMOOTH_ITERATIONS") && !parameters.value("SMOOTH_ITERATIONS").toString().isEmpty()) {
        int smoothIters = parameters.value("SMOOTH_ITERATIONS").toInt();
        if (smoothIters > 0) {
            args << "-si" << QString::number(smoothIters);
        }
    }

    args << rasterLayerSource(parameters.value("INPUT"));

    args << parameters.value("OUTPUT").toString();

    if (parameters.contains("EXTRA") && !parameters.value("EXTRA").toString().isEmpty()) {
        args << parameters.value("EXTRA").toString().split(" ");
    }

    return args;
}
