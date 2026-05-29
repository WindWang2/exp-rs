// src/processing/providers/otb_tools/algorithms/otb_segmentation.cpp
#include "otb_segmentation.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>

void OtbSegmentationAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addParameter(new QgsProcessingParameterRasterLayer("INPUT", "Input raster"));

    QStringList modes;
    modes << "meanshift" << "watershed" << "mprofiles" << "cc" << "lsms";
    addParameter(new QgsProcessingParameterEnum("MODE", "Segmentation mode", modes, false, 0));

    addParameter(new QgsProcessingParameterNumber("TRESHOLD", "Segmentation threshold",
                                                   Qgis::ProcessingNumberParameterType::Double, 0.1, false, 0.0));

    addParameter(new QgsProcessingParameterVectorDestination("OUTPUT", "Output vector (polygons)"));
}

QStringList OtbSegmentationAlgorithm::buildArgs(const QVariantMap &parameters,
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

    QStringList modes = {"meanshift", "watershed", "mprofiles", "cc", "lsms"};
    QString selectedMode = modes.value(parameters.value("MODE").toInt(), "meanshift");
    args << "-mode" << selectedMode;
    args << "-mode." + selectedMode + ".threshold"
         << QString::number(parameters.value("TRESHOLD").toDouble());

    args << "-out" << parameters.value("OUTPUT").toString();

    return args;
}
