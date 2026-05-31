// src/processing/providers/gdal_tools/algorithms/gdal_proximity.cpp
#include "gdal_proximity.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>

void GdalProximityAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addInputRasterLayerParameter("INPUT", "Input raster layer");

    addParameter(new QgsProcessingParameterNumber("MAX_DISTANCE", "Maximum distance (pixels)",
                                                   Qgis::ProcessingNumberParameterType::Double,
                                                   QVariant(), false, 0.0));

    addParameter(new QgsProcessingParameterString("TARGET_VALUE", "Target pixel value",
                                                   QVariant(), false, true));

    addParameter(new QgsProcessingParameterString("EXTRA", "Additional GDAL arguments",
                                                   QVariant(), false, true));

    addOutputRasterLayerParameter("OUTPUT", "Output proximity raster");
}

QStringList GdalProximityAlgorithm::buildArgs(const QVariantMap &parameters,
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

    if (parameters.contains("MAX_DISTANCE") && !parameters.value("MAX_DISTANCE").toString().isEmpty()) {
        double maxDist = parameters.value("MAX_DISTANCE").toDouble();
        if (maxDist > 0) {
            args << "-maxdist" << QString::number(maxDist);
        }
    }

    if (parameters.contains("TARGET_VALUE") && !parameters.value("TARGET_VALUE").toString().isEmpty()) {
        args << "-targetvalue" << parameters.value("TARGET_VALUE").toString();
    }

    if (parameters.contains("EXTRA") && !parameters.value("EXTRA").toString().isEmpty()) {
        args << parameters.value("EXTRA").toString().split(" ");
    }

    return args;
}
