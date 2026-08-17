// src/processing/providers/gdal_tools/algorithms/gdalmanage.cpp
#include "gdalmanage.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>

void GdalManageAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addInputRasterLayerParameter("INPUT", "Input raster layer");

    QStringList actions;
    actions << "identify" << "copy" << "rename" << "delete";
    addParameter(new QgsProcessingParameterEnum("ACTION", "Action to perform", actions, false, 0));
    addParameter(new QgsProcessingParameterString("NEWNAME", "New dataset name / destination path (for copy/rename)", QString(), false, true));
}

QStringList GdalManageAlgorithm::buildArgs(const QVariantMap &parameters,
                                             QgsProcessingContext &context,
                                             QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;

    QStringList actions;
    actions << "identify" << "copy" << "rename" << "delete";
    int actionIndex = parameters.value("ACTION").toInt();
    QString action = actions.value(actionIndex, "identify");
    args << action;

    args << rasterLayerSource(parameters.value("INPUT"));

    if (action == "copy" || action == "rename") {
        QString newName = parameters.value("NEWNAME").toString();
        if (newName.isEmpty()) {
            throw QgsProcessingException("Action '" + action + "' requires NEWNAME to be specified.");
        }
        args << newName;
    }

    return args;
}
