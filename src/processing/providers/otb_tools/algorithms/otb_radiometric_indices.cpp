// src/processing/providers/otb_tools/algorithms/otb_radiometric_indices.cpp
#include "otb_radiometric_indices.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>

void OtbRadiometricIndicesAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addParameter(new QgsProcessingParameterRasterLayer("INPUT", "Input raster"));
    addParameter(new QgsProcessingParameterString("LIST", "Indices list (e.g. NDVI)", "NDVI", false, false));
    addParameter(new QgsProcessingParameterRasterDestination("OUTPUT", "Output raster (indices)"));
}

QStringList OtbRadiometricIndicesAlgorithm::buildArgs(const QVariantMap &parameters,
                                                      QgsProcessingContext &context,
                                                      QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;
    args << "-in" << rasterLayerSource(parameters.value("INPUT"));

    QString list = parameters.value("LIST", "NDVI").toString().trimmed();
    if (!list.isEmpty()) {
        if (!list.contains(":")) {
            // Default to Vegetation category for common indices
            if (list.compare("NDWI", Qt::CaseInsensitive) == 0 ||
                list.compare("NDTI", Qt::CaseInsensitive) == 0) {
                list = "Water:" + list.toUpper();
            } else {
                list = "Vegetation:" + list.toUpper();
            }
        }
        args << "-list" << list;
    }

    int blue = parameters.value("BLUE_CHANNEL", 1).toInt();
    int green = parameters.value("GREEN_CHANNEL", 2).toInt();
    int red = parameters.value("RED_CHANNEL", 3).toInt();
    int nir = parameters.value("NIR_CHANNEL", 4).toInt();
    int mir = parameters.value("MIR_CHANNEL", 5).toInt();
    args << "-channels.blue" << QString::number(blue)
         << "-channels.green" << QString::number(green)
         << "-channels.red" << QString::number(red)
         << "-channels.nir" << QString::number(nir)
         << "-channels.mir" << QString::number(mir);

    args << "-out" << parameters.value("OUTPUT").toString();

    return args;
}
