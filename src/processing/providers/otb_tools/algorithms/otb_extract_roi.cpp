// src/processing/providers/otb_tools/algorithms/otb_extract_roi.cpp
#include "otb_extract_roi.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>

void OtbExtractRoiAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addParameter(new QgsProcessingParameterRasterLayer("INPUT", "Input raster"));
    addParameter(new QgsProcessingParameterExtent("EXTENT", "Region of interest extent"));
    addParameter(new QgsProcessingParameterRasterDestination("OUTPUT", "Output raster"));
}

QStringList OtbExtractRoiAlgorithm::buildArgs(const QVariantMap &parameters,
                                               QgsProcessingContext &context,
                                               QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;
    args << "-in" << rasterLayerSource(parameters.value("INPUT"));

    if (parameters.contains("EXTENT") && !parameters.value("EXTENT").toString().isEmpty()) {
        QStringList extent = parameters.value("EXTENT").toString().split(",");
        if (extent.size() == 4) {
            args << "-mode" << "extent"
                 << "-mode.extent.ulx" << extent[0]
                 << "-mode.extent.uly" << extent[3]
                 << "-mode.extent.lrx" << extent[2]
                 << "-mode.extent.lry" << extent[1];
        }
    }

    args << "-out" << parameters.value("OUTPUT").toString();

    return args;
}
