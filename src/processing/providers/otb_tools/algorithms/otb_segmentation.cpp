// src/processing/providers/otb_tools/algorithms/otb_segmentation.cpp
//
// Phase 10B.2 — Enhanced OTB Segmentation wrapper with full MeanShift
// parameters (spatial radius, range radius, min region size, max iterations)
// and optional label image raster output.
#include "otb_segmentation.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>

void OtbSegmentationAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addParameter(new QgsProcessingParameterRasterLayer("INPUT", QObject::tr("Input raster")));

    QStringList modes;
    modes << "meanshift" << "watershed" << "mprofiles" << "cc" << "lsms";
    addParameter(new QgsProcessingParameterEnum("MODE", QObject::tr("Segmentation mode"), modes, false, 0));

    // MeanShift parameters (Phase 10B.2)
    addParameter(new QgsProcessingParameterNumber(
        "SPATIAL_RADIUS", QObject::tr("Spatial radius (MeanShift)"),
        Qgis::ProcessingNumberParameterType::Integer, 5, false, 1, 100));

    addParameter(new QgsProcessingParameterNumber(
        "RANGE_RADIUS", QObject::tr("Range radius (MeanShift)"),
        Qgis::ProcessingNumberParameterType::Double, 15.0, false, 0.1, 1000.0));

    addParameter(new QgsProcessingParameterNumber(
        "MIN_REGION_SIZE", QObject::tr("Minimum region size"),
        Qgis::ProcessingNumberParameterType::Integer, 100, false, 1, 100000));

    addParameter(new QgsProcessingParameterNumber(
        "MAX_ITERATION", QObject::tr("Maximum iterations"),
        Qgis::ProcessingNumberParameterType::Integer, 100, false, 1, 10000));

    // ISSUE 6 fix: threshold is only valid for watershed/mprofiles/cc modes,
    // not for meanshift/lsms. Kept for other modes.
    addParameter(new QgsProcessingParameterNumber(
        "THRESHOLD", QObject::tr("Segmentation threshold (watershed/mprofiles/cc)"),
        Qgis::ProcessingNumberParameterType::Double, 0.1, false, 0.001, 10.0));

    // Output: vector polygons (existing)
    addParameter(new QgsProcessingParameterVectorDestination("OUTPUT", QObject::tr("Output vector (polygons)")));

    // Output: label image raster (Phase 10B.2 — for OBIA pipeline)
    auto labelOutput = new QgsProcessingParameterRasterDestination(
        "OUTPUT_RASTER", QObject::tr("Output label image (raster)"), QVariant(), true);
    labelOutput->setFlags(labelOutput->flags() | Qgis::ProcessingParameterFlag::Optional);
    addParameter(labelOutput);
}

QStringList OtbSegmentationAlgorithm::buildArgs(const QVariantMap &parameters,
                                                 QgsProcessingContext &context,
                                                 QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;

    // Input raster
    args << "-in" << rasterLayerSource(parameters.value("INPUT"));

    // Mode selection
    QStringList modes = {"meanshift", "watershed", "mprofiles", "cc", "lsms"};
    QString selectedMode = modes.value(parameters.value("MODE").toInt(), "meanshift");
    args << "-mode" << selectedMode;

    // Mode-specific parameters
    if (selectedMode == "meanshift")
    {
        // ISSUE 6 fix: MeanShift uses spatialr/ranger/minsize/maxiter only
        // No threshold parameter for meanshift mode
        args << "-mode.meanshift.spatialr"
             << QString::number(parameters.value("SPATIAL_RADIUS").toInt());
        args << "-mode.meanshift.ranger"
             << QString::number(parameters.value("RANGE_RADIUS").toDouble(), 'f', 2);
        args << "-mode.meanshift.minsize"
             << QString::number(parameters.value("MIN_REGION_SIZE").toInt());
        args << "-mode.meanshift.maxiter"
             << QString::number(parameters.value("MAX_ITERATION").toInt());
    }
    else if (selectedMode == "lsms")
    {
        // LSMS also uses spatialr/ranger/minsize/maxiter
        args << "-mode.lsms.spatialr"
             << QString::number(parameters.value("SPATIAL_RADIUS").toInt());
        args << "-mode.lsms.ranger"
             << QString::number(parameters.value("RANGE_RADIUS").toDouble(), 'f', 2);
        args << "-mode.lsms.minsize"
             << QString::number(parameters.value("MIN_REGION_SIZE").toInt());
        args << "-mode.lsms.maxiter"
             << QString::number(parameters.value("MAX_ITERATION").toInt());
    }
    else
    {
        // For watershed/mprofiles/cc — threshold is valid
        args << "-mode." + selectedMode + ".threshold"
             << QString::number(parameters.value("THRESHOLD").toDouble(), 'f', 4);
    }

    // Output vector
    QString vectorOutput = parameters.value("OUTPUT").toString();

    // ISSUE 5 fix: OTB Segmentation -out format for raster+vector is:
    //   -out vector.shp label_image.tif uint32
    // When OUTPUT_RASTER is specified, include both in -out
    QString labelOutput = parameters.value("OUTPUT_RASTER").toString();
    if (!labelOutput.isEmpty())
    {
        args << "-out" << vectorOutput << labelOutput << "uint32";
    }
    else
    {
        args << "-out" << vectorOutput;
    }

    return args;
}
