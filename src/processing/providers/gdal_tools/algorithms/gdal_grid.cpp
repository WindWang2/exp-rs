// src/processing/providers/gdal_tools/algorithms/gdal_grid.cpp
#include "gdal_grid.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsvectorlayer.h>

void GdalGridAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addInputVectorLayerParameter("INPUT", "Input point vector layer");

    QStringList algorithms;
    algorithms << "nearest" << "average" << "invdist" << "minimum" << "maximum" << "range" << "count";
    addParameter(new QgsProcessingParameterEnum("ALGORITHM", "Gridding algorithm",
                                                algorithms, false, 2));

    addParameter(new QgsProcessingParameterNumber("POWER", "Power (for invdist)",
                                                   Qgis::ProcessingNumberParameterType::Double,
                                                   2.0, false, 0.0));

    addParameter(new QgsProcessingParameterNumber("SMOOTH", "Smoothing",
                                                   Qgis::ProcessingNumberParameterType::Double,
                                                   0.0, false, 0.0));

    addParameter(new QgsProcessingParameterNumber("NODATA", "Nodata value for output raster",
                                                   Qgis::ProcessingNumberParameterType::Double,
                                                   -9999.0, true));

    addParameter(new QgsProcessingParameterExtent("EXTENT", "Output extent"));

    addParameter(new QgsProcessingParameterNumber("PIXEL_SIZE", "Output pixel size",
                                                   Qgis::ProcessingNumberParameterType::Double,
                                                   0.0, false, 0.0));

    addParameter(new QgsProcessingParameterString("EXTRA", "Additional GDAL arguments",
                                                   QVariant(), false, true));

    addOutputRasterLayerParameter("OUTPUT", "Output raster grid");
}

QStringList GdalGridAlgorithm::buildArgs(const QVariantMap &parameters,
                                           QgsProcessingContext &context,
                                           QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;

    QStringList algorithms;
    algorithms << "nearest" << "average" << "invdist" << "minimum" << "maximum" << "range" << "count";
    int algoIndex = parameters.value("ALGORITHM", 2).toInt();
    QString algorithm = algorithms.value(algoIndex, "invdist");

    double power = parameters.value("POWER", 2.0).toDouble();
    double smooth = parameters.value("SMOOTH", 0.0).toDouble();

    QString algoArg;
    if (algorithm == "invdist") {
        algoArg = QString("invdist:power=%1:smoothing=%2").arg(power).arg(smooth);
    } else if (algorithm == "average") {
        algoArg = QString("average:smoothing=%1").arg(smooth);
    } else {
        algoArg = algorithm;
    }
    args << "-a" << algoArg;

    if (parameters.contains("NODATA") && !parameters.value("NODATA").toString().isEmpty()) {
        double nodata = parameters.value("NODATA").toDouble();
        args << "-a_nodata" << QString::number(nodata);
    }

    if (parameters.contains("EXTENT") && !parameters.value("EXTENT").toString().isEmpty()) {
        // Parse extent string: "xmin,xmax,ymin,ymax"
        QString extentStr = parameters.value("EXTENT").toString();
        QStringList parts = extentStr.split(",");
        if (parts.size() == 4) {
            args << "-txe" << parts[0].trimmed() << parts[1].trimmed();
            args << "-tye" << parts[2].trimmed() << parts[3].trimmed();
        }
    }

    if (parameters.contains("PIXEL_SIZE") && !parameters.value("PIXEL_SIZE").toString().isEmpty()) {
        double pixelSize = parameters.value("PIXEL_SIZE").toDouble();
        if (pixelSize > 0) {
            args << "-outsize" << QString::number(pixelSize) << QString::number(pixelSize);
        }
    }

    args << vectorLayerSource(parameters.value("INPUT"));

    args << parameters.value("OUTPUT").toString();

    if (parameters.contains("EXTRA") && !parameters.value("EXTRA").toString().isEmpty()) {
        args << QProcess::splitCommand(parameters.value("EXTRA").toString());
    }

    return args;
}
