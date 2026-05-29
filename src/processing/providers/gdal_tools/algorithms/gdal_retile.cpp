// src/processing/providers/gdal_tools/algorithms/gdal_retile.cpp
#include "gdal_retile.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>

void GdalRetileAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addInputRasterLayerParameter("INPUT", "Input raster layer");
    addParameter(new QgsProcessingParameterString("TILE_SIZE", "Tile size (e.g. 256x256)",
                                                    "256x256", false));
    addParameter(new QgsProcessingParameterFolderDestination("OUTPUT_DIR", "Output directory"));
}

QStringList GdalRetileAlgorithm::buildArgs(const QVariantMap &parameters,
                                             QgsProcessingContext &context,
                                             QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;

    QString tileSize = parameters.value("TILE_SIZE", "256x256").toString();
    QStringList parts = tileSize.split("x");
    if (parts.size() == 2) {
        args << "-ps" << parts[0] << parts[1];
    }

    args << "-targetDir" << parameters.value("OUTPUT_DIR").toString();

    QVariant inputVar = parameters.value("INPUT");
    QString inputPath;
    if (inputVar.canConvert<QgsRasterLayer *>()) {
        inputPath = inputVar.value<QgsRasterLayer *>()->source();
    } else {
        inputPath = inputVar.toString();
    }
    args << inputPath;

    return args;
}
