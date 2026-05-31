// src/processing/providers/otb_tools/algorithms/otb_band_math_x.cpp
#include "otb_band_math_x.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>

void OtbBandMathXAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    Q_UNUSED(configuration);

    addParameter(new QgsProcessingParameterMultipleLayers("INPUT", "Input raster layers",
                                                          Qgis::ProcessingSourceType::Raster));
    addParameter(new QgsProcessingParameterString("EXPRESSION", "Mathematical expression (use im1b1, im2b1, etc.)",
                                                  QVariant(), false, false));
    addParameter(new QgsProcessingParameterRasterDestination("OUTPUT", "Output raster"));
}

QStringList OtbBandMathXAlgorithm::buildArgs(const QVariantMap &parameters,
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
            if (v.canConvert<QgsRasterLayer *>()) {
                inputPaths << v.value<QgsRasterLayer *>()->source();
            } else {
                inputPaths << v.toString();
            }
        }
    } else if (inputVar.canConvert<QgsRasterLayer *>()) {
        inputPaths << inputVar.value<QgsRasterLayer *>()->source();
    } else {
        inputPaths << inputVar.toString();
    }

    for (const QString &path : inputPaths) {
        args << "-il" << path;
    }

    args << "-exp" << parameters.value("EXPRESSION").toString();
    args << "-out" << parameters.value("OUTPUT").toString();

    return args;
}
