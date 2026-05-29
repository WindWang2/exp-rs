// src/processing/providers/gdal_tools/algorithms/gdaltindex.cpp
#include "gdaltindex.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>

void GdalTindexAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addParameter(new QgsProcessingParameterMultipleLayers("INPUT", "Input raster layers",
                                                           Qgis::ProcessingSourceType::Raster));
    addOutputVectorLayerParameter("OUTPUT", "Output tile index (vector layer)");
}

QStringList GdalTindexAlgorithm::buildArgs(const QVariantMap &parameters,
                                             QgsProcessingContext &context,
                                             QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;

    args << parameters.value("OUTPUT").toString();

    QList<QgsMapLayer *> layers = parameterMultipleLayers(parameters, "INPUT", context);
    for (QgsMapLayer *layer : layers) {
        if (QgsRasterLayer *raster = qobject_cast<QgsRasterLayer *>(layer)) {
            args << raster->source();
        }
    }

    return args;
}
