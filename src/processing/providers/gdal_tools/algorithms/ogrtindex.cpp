// src/processing/providers/gdal_tools/algorithms/ogrtindex.cpp
#include "ogrtindex.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsvectorlayer.h>

void OgrTindexAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addParameter(new QgsProcessingParameterMultipleLayers("INPUT", "Input vector layers",
                                                           Qgis::ProcessingSourceType::Vector));
    addOutputVectorLayerParameter("OUTPUT", "Output tile index (vector layer)");
}

QStringList OgrTindexAlgorithm::buildArgs(const QVariantMap &parameters,
                                            QgsProcessingContext &context,
                                            QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;

    args << parameters.value("OUTPUT").toString();

    QList<QgsMapLayer *> layers = parameterAsLayerList(parameters, "INPUT", context);
    for (QgsMapLayer *layer : layers) {
        if (QgsVectorLayer *vector = qobject_cast<QgsVectorLayer *>(layer)) {
            args << vector->source();
        }
    }

    return args;
}
