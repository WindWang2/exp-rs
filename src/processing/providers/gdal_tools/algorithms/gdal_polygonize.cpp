// src/processing/providers/gdal_tools/algorithms/gdal_polygonize.cpp
#include "gdal_polygonize.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>

void GdalPolygonizeAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addInputRasterLayerParameter("INPUT", "Input raster layer");
    addOutputVectorLayerParameter("OUTPUT", "Output vector layer (polygons)");
}

QStringList GdalPolygonizeAlgorithm::buildArgs(const QVariantMap &parameters,
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
    args << inputPath;
    args << parameters.value("OUTPUT").toString();

    return args;
}
