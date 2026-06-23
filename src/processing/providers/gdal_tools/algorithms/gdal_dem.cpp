// src/processing/providers/gdal_tools/algorithms/gdal_dem.cpp
#include "gdal_dem.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>

void GdalDemAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addInputRasterLayerParameter("INPUT", "Input raster layer");

    QStringList modes;
    modes << "hillshade" << "slope" << "aspect" << "TRI" << "TPI" << "roughness";
    addParameter(new QgsProcessingParameterEnum("MODE", "Analysis mode", modes, false, 0));

    addOutputRasterLayerParameter("OUTPUT", "Output raster layer");
}

QStringList GdalDemAlgorithm::buildArgs(const QVariantMap &parameters,
                                         QgsProcessingContext &context,
                                         QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;

    QStringList modes;
    modes << "hillshade" << "slope" << "aspect" << "TRI" << "TPI" << "roughness";
    int modeIndex = parameters.value("MODE").toInt();
    QString mode = modes.value(modeIndex, "hillshade");
    args << mode;

    args << rasterLayerSource(parameters.value("INPUT"));
    args << parameters.value("OUTPUT").toString();

    return args;
}
