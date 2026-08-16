// src/processing/providers/gdal_tools/algorithms/gdal_warp.cpp
#include "gdal_warp.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>

void GdalWarpAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addInputRasterLayerParameter("INPUT", "Input raster layer");
    addCrsParameter("TARGET_CRS", "Target CRS (leave empty to skip reprojection)");
    addExtentParameter("EXTENT");
    addParameter(new QgsProcessingParameterString("EXTRA", "Additional arguments", QVariant(), false, true));
    addOutputRasterLayerParameter("OUTPUT", "Output raster layer");
}

QStringList GdalWarpAlgorithm::buildArgs(const QVariantMap &parameters,
                                          QgsProcessingContext &context,
                                          QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;

    if (parameters.contains("TARGET_CRS") && !parameters.value("TARGET_CRS").toString().isEmpty()) {
        args << "-t_srs" << parameters.value("TARGET_CRS").toString();
    }

    if (parameters.contains("EXTENT") && !parameters.value("EXTENT").toString().isEmpty()) {
        QStringList extent = parameters.value("EXTENT").toString().split(",");
        if (extent.size() == 4) {
            args << "-te" << extent[0].trimmed() << extent[2].trimmed() << extent[1].trimmed() << extent[3].trimmed();
        }
    }

    if (parameters.contains("EXTRA") && !parameters.value("EXTRA").toString().isEmpty()) {
        args << QProcess::splitCommand(parameters.value("EXTRA").toString());
    }

    args << rasterLayerSource(parameters.value("INPUT"));
    args << parameters.value("OUTPUT").toString();

    return args;
}
