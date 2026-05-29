// src/processing/providers/gdal_tools/algorithms/gdalmanage.cpp
#include "gdalmanage.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>

void GdalManageAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addInputRasterLayerParameter("INPUT", "Input raster layer");

    QStringList actions;
    actions << "info" << "copy" << "rename" << "delete";
    addParameter(new QgsProcessingParameterEnum("ACTION", "Action to perform", actions, false, 0));
}

QStringList GdalManageAlgorithm::buildArgs(const QVariantMap &parameters,
                                             QgsProcessingContext &context,
                                             QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;

    QStringList actions;
    actions << "info" << "copy" << "rename" << "delete";
    int actionIndex = parameters.value("ACTION").toInt();
    QString action = actions.value(actionIndex, "info");
    args << action;

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
