// src/processing/providers/gdal_tools/algorithms/ogr2ogr.cpp
#include "ogr2ogr.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsvectorlayer.h>

void Ogr2OgrAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addInputVectorLayerParameter("INPUT", "Input vector layer");

    QStringList formats;
    formats << "GPKG" << "ESRI Shapefile" << "GeoJSON" << "KML" << "GML";
    addParameter(new QgsProcessingParameterEnum("FORMAT", "Output format", formats, false, 0));

    addOutputVectorLayerParameter("OUTPUT", "Output vector layer");
}

QStringList Ogr2OgrAlgorithm::buildArgs(const QVariantMap &parameters,
                                          QgsProcessingContext &context,
                                          QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;

    QStringList formats;
    formats << "GPKG" << "ESRI Shapefile" << "GeoJSON" << "KML" << "GML";
    int formatIndex = parameters.value("FORMAT").toInt();
    QString format = formats.value(formatIndex, "GPKG");
    args << "-f" << format;

    args << parameters.value("OUTPUT").toString();

    QVariant inputVar = parameters.value("INPUT");
    QString inputPath;
    if (inputVar.canConvert<QgsVectorLayer *>()) {
        inputPath = inputVar.value<QgsVectorLayer *>()->source();
    } else {
        inputPath = inputVar.toString();
    }
    args << inputPath;

    return args;
}
