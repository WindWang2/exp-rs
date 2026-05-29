// src/processing/providers/gdal_tools/algorithms/gdalbuildvrt.cpp
#include "gdalbuildvrt.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>

void GdalBuildVrtAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addParameter(new QgsProcessingParameterMultipleLayers("INPUT", "Input raster layers",
                                                           Qgis::ProcessingSourceType::Raster));
    addOutputRasterLayerParameter("OUTPUT", "Output VRT file");
}

QStringList GdalBuildVrtAlgorithm::buildArgs(const QVariantMap &parameters,
                                              QgsProcessingContext &context,
                                              QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;

    args << parameters.value("OUTPUT").toString();

    QList<QgsMapLayer *> layers = parameterAsLayerList(parameters, "INPUT", context);
    for (QgsMapLayer *layer : layers) {
        if (QgsRasterLayer *raster = qobject_cast<QgsRasterLayer *>(layer)) {
            args << raster->source();
        }
    }

    return args;
}
