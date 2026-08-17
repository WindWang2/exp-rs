// src/processing/providers/otb_tools/algorithms/otb_concatenate_images.cpp
#include "otb_concatenate_images.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>

void OtbConcatenateImagesAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addParameter(new QgsProcessingParameterRasterLayer("INPUT", "Input rasters (multiple)"));
    addParameter(new QgsProcessingParameterRasterDestination("OUTPUT", "Output raster"));
}

QStringList OtbConcatenateImagesAlgorithm::buildArgs(const QVariantMap &parameters,
                                                     QgsProcessingContext &context,
                                                     QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;
    args << "-il";
    QVariant inputVar = parameters.value("INPUT");
    if (inputVar.userType() == QMetaType::QVariantList) {
        const QVariantList list = inputVar.toList();
        for (const QVariant &v : list) {
            args << rasterLayerSource(v);
        }
    } else if (inputVar.userType() == QMetaType::QStringList) {
        const QStringList list = inputVar.toStringList();
        for (const QString &s : list) {
            args << rasterLayerSource(s);
        }
    } else {
        QString str = inputVar.toString();
        if (str.contains(";")) {
            for (const QString &s : str.split(";", Qt::SkipEmptyParts)) {
                args << rasterLayerSource(s.trimmed());
            }
        } else {
            args << rasterLayerSource(inputVar);
        }
    }
    args << "-out" << parameters.value("OUTPUT").toString();

    return args;
}
