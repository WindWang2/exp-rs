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

    if (parameters.contains("EXTENT") && !parameters.value("EXTENT").isNull()) {
        QVariant val = parameters.value("EXTENT");
        QgsRectangle rect;
        if (val.canConvert<QgsRectangle>()) {
            rect = val.value<QgsRectangle>();
        } else {
            QString str = val.toString();
            QStringList extent = str.split(",");
            if (extent.size() == 4) {
                // QGIS extent string order: xmin, xmax, ymin, ymax
                double xmin = extent[0].toDouble();
                double xmax = extent[1].toDouble();
                double ymin = extent[2].toDouble();
                double ymax = extent[3].toDouble();
                rect = QgsRectangle(xmin, ymin, xmax, ymax);
            }
        }

        if (!rect.isNull() && !rect.isEmpty()) {
            args << "-mode" << "extent"
                 << "-mode.extent.unit" << "phy"
                 << "-mode.extent.ulx" << QString::number(rect.xMinimum(), 'f', 6)
                 << "-mode.extent.uly" << QString::number(rect.yMaximum(), 'f', 6)
                 << "-mode.extent.lrx" << QString::number(rect.xMaximum(), 'f', 6)
                 << "-mode.extent.lry" << QString::number(rect.yMinimum(), 'f', 6);
        }
    }

    args << "-out" << parameters.value("OUTPUT").toString();

    return args;
}
