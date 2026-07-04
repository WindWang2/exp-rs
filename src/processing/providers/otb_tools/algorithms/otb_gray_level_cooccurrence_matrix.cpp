// src/processing/providers/otb_tools/algorithms/otb_gray_level_cooccurrence_matrix.cpp
#include "otb_gray_level_cooccurrence_matrix.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>

void OtbGrayLevelCooccurrenceMatrixAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    Q_UNUSED(configuration);

    addParameter(new QgsProcessingParameterRasterLayer("INPUT", QObject::tr("Input raster")));

    addParameter(new QgsProcessingParameterNumber(
        "CHANNEL", QObject::tr("Selected channel"),
        Qgis::ProcessingNumberParameterType::Integer, 1, false, 1));

    addParameter(new QgsProcessingParameterNumber(
        "XRAD", QObject::tr("X radius"),
        Qgis::ProcessingNumberParameterType::Integer, 2, false, 1));

    addParameter(new QgsProcessingParameterNumber(
        "YRAD", QObject::tr("Y radius"),
        Qgis::ProcessingNumberParameterType::Integer, 2, false, 1));

    addParameter(new QgsProcessingParameterNumber(
        "XOFF", QObject::tr("X offset"),
        Qgis::ProcessingNumberParameterType::Integer, 1, false, 0));

    addParameter(new QgsProcessingParameterNumber(
        "YOFF", QObject::tr("Y offset"),
        Qgis::ProcessingNumberParameterType::Integer, 1, false, 0));

    addParameter(new QgsProcessingParameterNumber(
        "MIN", QObject::tr("Image minimum"),
        Qgis::ProcessingNumberParameterType::Double, 0.0, false));

    addParameter(new QgsProcessingParameterNumber(
        "MAX", QObject::tr("Image maximum"),
        Qgis::ProcessingNumberParameterType::Double, 255.0, false));

    addParameter(new QgsProcessingParameterNumber(
        "NBBIN", QObject::tr("Histogram number of bins"),
        Qgis::ProcessingNumberParameterType::Integer, 8, false, 2, 256));

    addParameter(new QgsProcessingParameterRasterDestination("OUTPUT", QObject::tr("Output raster (GLCM)")));
}

QStringList OtbGrayLevelCooccurrenceMatrixAlgorithm::buildArgs(const QVariantMap &parameters,
                                                                QgsProcessingContext &context,
                                                                QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;
    args << "-in" << rasterLayerSource(parameters.value("INPUT"));
    args << "-channel" << QString::number(parameters.value("CHANNEL").toInt());
    args << "-parameters.xrad" << QString::number(parameters.value("XRAD").toInt());
    args << "-parameters.yrad" << QString::number(parameters.value("YRAD").toInt());
    args << "-parameters.xoff" << QString::number(parameters.value("XOFF").toInt());
    args << "-parameters.yoff" << QString::number(parameters.value("YOFF").toInt());
    args << "-parameters.min" << QString::number(parameters.value("MIN").toDouble(), 'f', 2);
    args << "-parameters.max" << QString::number(parameters.value("MAX").toDouble(), 'f', 2);
    args << "-parameters.nbbin" << QString::number(parameters.value("NBBIN").toInt());
    args << "-out" << parameters.value("OUTPUT").toString();

    return args;
}