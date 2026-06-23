// src/processing/providers/gdal_tools/algorithms/gdal_rasterize.cpp
#include "gdal_rasterize.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsvectorlayer.h>

void GdalRasterizeAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addInputVectorLayerParameter("INPUT", "Input vector layer");

    addParameter(new QgsProcessingParameterRasterLayer("RASTER_TEMPLATE", "Raster template (optional)",
                                                        QVariant(), true));

    addParameter(new QgsProcessingParameterString("FIELD", "Attribute field for burn value",
                                                   QVariant(), false, true));

    addParameter(new QgsProcessingParameterNumber("BURN_VALUE", "Fixed burn value",
                                                   Qgis::ProcessingNumberParameterType::Double,
                                                   1.0, false, 0.0));

    addParameter(new QgsProcessingParameterExtent("EXTENT", "Output extent (used when no template)"));

    addParameter(new QgsProcessingParameterNumber("WIDTH", "Output raster width (pixels)",
                                                   Qgis::ProcessingNumberParameterType::Integer,
                                                   0, false, 0));

    addParameter(new QgsProcessingParameterNumber("HEIGHT", "Output raster height (pixels)",
                                                   Qgis::ProcessingNumberParameterType::Integer,
                                                   0, false, 0));

    addParameter(new QgsProcessingParameterString("EXTRA", "Additional GDAL arguments",
                                                   QVariant(), false, true));

    addOutputRasterLayerParameter("OUTPUT", "Output raster layer");
}

QStringList GdalRasterizeAlgorithm::buildArgs(const QVariantMap &parameters,
                                                QgsProcessingContext &context,
                                                QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;

    // Burn value: either from field or fixed value
    if (parameters.contains("FIELD") && !parameters.value("FIELD").toString().isEmpty()) {
        args << "-a" << parameters.value("FIELD").toString();
    } else {
        double burnValue = parameters.value("BURN_VALUE", 1.0).toDouble();
        args << "-burn" << QString::number(burnValue);
    }

    // Template raster
    if (parameters.contains("RASTER_TEMPLATE") && !parameters.value("RASTER_TEMPLATE").toString().isEmpty()) {
        args << "-at" << "-ts" << rasterLayerSource(parameters.value("RASTER_TEMPLATE"));
    } else {
        // Use extent and size
        if (parameters.contains("EXTENT") && !parameters.value("EXTENT").toString().isEmpty()) {
            QString extentStr = parameters.value("EXTENT").toString();
            QStringList parts = extentStr.split(",");
            if (parts.size() == 4) {
                args << "-te" << parts[0].trimmed() << parts[2].trimmed()
                     << parts[1].trimmed() << parts[3].trimmed();
            }
        }

        int width = parameters.value("WIDTH", 0).toInt();
        int height = parameters.value("HEIGHT", 0).toInt();
        if (width > 0 && height > 0) {
            args << "-ts" << QString::number(width) << QString::number(height);
        }
    }

    args << vectorLayerSource(parameters.value("INPUT"));

    args << parameters.value("OUTPUT").toString();

    if (parameters.contains("EXTRA") && !parameters.value("EXTRA").toString().isEmpty()) {
        args << parameters.value("EXTRA").toString().split(" ");
    }

    return args;
}
