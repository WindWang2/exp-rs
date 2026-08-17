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

    // Filter selection (meanshift, watershed, mprofiles, cc)
    QStringList filters = {"meanshift", "watershed", "mprofiles", "cc", "meanshift"};
    int modeIdx = parameters.value("MODE").toInt();
    QString selectedFilter = (modeIdx >= 0 && modeIdx < filters.size()) ? filters[modeIdx] : "meanshift";
    args << "-filter" << selectedFilter;

    // Filter-specific parameters
    if (selectedFilter == "meanshift")
    {
        args << "-filter.meanshift.spatialr"
             << QString::number(parameters.value("SPATIAL_RADIUS").toInt());
        args << "-filter.meanshift.ranger"
             << QString::number(parameters.value("RANGE_RADIUS").toDouble(), 'f', 2);
        args << "-filter.meanshift.minsize"
             << QString::number(parameters.value("MIN_REGION_SIZE").toInt());
        args << "-filter.meanshift.maxiter"
             << QString::number(parameters.value("MAX_ITERATION").toInt());
    }
    else if (selectedFilter == "watershed")
    {
        args << "-filter.watershed.threshold"
             << QString::number(parameters.value("THRESHOLD").toDouble(), 'f', 4);
    }
    else
    {
        args << "-filter." + selectedFilter + ".threshold"
             << QString::number(parameters.value("THRESHOLD").toDouble(), 'f', 4);
    }

    // Output mode & path
    QString vectorOutput = parameters.value("OUTPUT").toString();
    QString labelOutput = parameters.value("OUTPUT_RASTER").toString();

    if (!vectorOutput.isEmpty())
    {
        args << "-mode" << "vector" << "-mode.vector.out" << vectorOutput;
        if (!labelOutput.isEmpty())
        {
            args << "-mode.raster.out" << labelOutput;
        }
    }
    else if (!labelOutput.isEmpty())
    {
        args << "-mode" << "raster" << "-mode.raster.out" << labelOutput;
    }

    return args;
}
