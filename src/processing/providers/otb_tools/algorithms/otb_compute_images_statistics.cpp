// src/processing/providers/otb_tools/algorithms/otb_compute_images_statistics.cpp
#include "otb_compute_images_statistics.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>

void OtbComputeImagesStatisticsAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    Q_UNUSED(configuration);

    addParameter(new QgsProcessingParameterMultipleLayers("INPUT", "Input raster layers",
                                                          Qgis::ProcessingSourceType::Raster));
    addParameter(new QgsProcessingParameterBoolean("RAM", "Use RAM estimation", false));
    addParameter(new QgsProcessingParameterFileDestination("STATS", "Output statistics file",
                                                           "XML files (*.xml)", QVariant(), true));
}

QStringList OtbComputeImagesStatisticsAlgorithm::buildArgs(const QVariantMap &parameters,
                                                            QgsProcessingContext &context,
                                                            QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;

    // Process multiple input layers
    QVariant inputVar = parameters.value("INPUT");
    QStringList inputPaths;

    if (inputVar.canConvert<QVariantList>()) {
        const QVariantList inputList = inputVar.toList();
        for (const QVariant &v : inputList) {
            inputPaths << rasterLayerSource(v);
        }
    } else {
        inputPaths << rasterLayerSource(inputVar);
    }

    for (const QString &path : inputPaths) {
        args << "-il" << path;
    }

    if (parameters.value("RAM").toBool()) {
        args << "-ram" << "256";
    }

    QString statsPath = parameters.value("STATS").toString();
    if (!statsPath.isEmpty()) {
        args << "-out" << statsPath;
    }

    return args;
}
