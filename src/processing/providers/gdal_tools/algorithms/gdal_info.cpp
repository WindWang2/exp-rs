// src/processing/providers/gdal_tools/algorithms/gdal_info.cpp
#include "gdal_info.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>

void GdalInfoAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addInputRasterLayerParameter("INPUT", "Input raster layer");
    addParameter(new QgsProcessingParameterFileDestination("OUTPUT", "Info output (text file)",
                                                           "Text files (*.txt)"));
}

QStringList GdalInfoAlgorithm::buildArgs(const QVariantMap &parameters,
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

    return args;
}
