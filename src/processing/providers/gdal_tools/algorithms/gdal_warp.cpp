// src/processing/providers/gdal_tools/algorithms/gdal_warp.cpp
#include "gdal_warp.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>

void GdalWarpAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addInputRasterLayerParameter("INPUT", "Input raster layer");
    addCrsParameter("TARGET_CRS", "Target CRS (leave empty to skip reprojection)");
    addExtentParameter("EXTENT");
    addParameter(new QgsProcessingParameterString("EXTRA", "Additional arguments", QVariant(), false, true));
    addOutputRasterLayerParameter("OUTPUT", "Output raster layer");
}

QStringList GdalWarpAlgorithm::buildArgs(const QVariantMap &parameters,
                                          QgsProcessingContext &context,
                                          QgsProcessingFeedback *feedback)
{
    Q_UNUSED(feedback);

    QStringList args;
    args << QStringLiteral( "-overwrite" );

    // A CRS parameter arrives either as a QgsCoordinateReferenceSystem QVariant
    // (the processing dialog's QgsProcessingCrsWidgetWrapper) or as a string
    // (MCP/CLI). QVariant::toString() on the former yields an empty string, so
    // -t_srs was silently dropped and the output kept the source CRS while the
    // run reported a successful reprojection; valueAsString() is the
    // contract-compliant conversion (#2221).
    bool targetCrsOk = false;
    const QString targetCrs = parameterDefinition(QStringLiteral("TARGET_CRS"))
                                  ->valueAsString(parameters.value(QStringLiteral("TARGET_CRS")), context, targetCrsOk);
    if (targetCrsOk && !targetCrs.isEmpty()) {
        args << "-t_srs" << targetCrs;
    }

    if (parameters.contains("EXTENT") && !parameters.value("EXTENT").toString().isEmpty()) {
        // QgsProcessingParameterExtent string is "xmin,xmax,ymin,ymax" — -te expects xmin ymin xmax ymax
        QStringList extent = parameters.value("EXTENT").toString().split(",");
        if (extent.size() == 4) {
            args << "-te" << extent[0].trimmed() << extent[2].trimmed() << extent[1].trimmed() << extent[3].trimmed();
        }
    }

    if (parameters.contains("EXTRA") && !parameters.value("EXTRA").toString().isEmpty()) {
        args << QProcess::splitCommand(parameters.value("EXTRA").toString());
    }

    args << rasterLayerSource(parameters.value("INPUT"));
    args << parameters.value("OUTPUT").toString();

    return args;
}
