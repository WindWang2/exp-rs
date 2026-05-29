// src/processing/providers/otb_tools/algorithms/otb_band_math.cpp
#include "otb_band_math.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>

void OtbBandMathAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addParameter(new QgsProcessingParameterRasterLayer("INPUT", "Input raster (multi-band)"));
    addParameter(new QgsProcessingParameterString("EXPRESSION", "Mathematical expression (e.g., (b1-b2)/(b1+b2))", QVariant(), false, false));
    addParameter(new QgsProcessingParameterRasterDestination("OUTPUT", "Output raster"));
}

QStringList OtbBandMathAlgorithm::buildArgs(const QVariantMap &parameters,
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
    args << "-in" << inputPath;
    args << "-exp" << parameters.value("EXPRESSION").toString();
    args << "-out" << parameters.value("OUTPUT").toString();

    return args;
}
