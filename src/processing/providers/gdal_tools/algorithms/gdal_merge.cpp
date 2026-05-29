// src/processing/providers/gdal_tools/algorithms/gdal_merge.cpp
#include "gdal_merge.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>

void GdalMergeAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addParameter(new QgsProcessingParameterMultipleLayers("INPUT", "Input raster layers",
                                                           Qgis::ProcessingSourceType::Raster));
    addOutputRasterLayerParameter("OUTPUT", "Output raster layer");
}

QStringList GdalMergeAlgorithm::buildArgs(const QVariantMap &parameters,
                                           QgsProcessingContext &context,
                                           QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;
    args << "-o" << parameters.value("OUTPUT").toString();

    QList<QgsMapLayer *> layers = parameterMultipleLayers(parameters, "INPUT", context);
    for (QgsMapLayer *layer : layers) {
        if (QgsRasterLayer *raster = qobject_cast<QgsRasterLayer *>(layer)) {
            args << raster->source();
        }
    }

    return args;
}
