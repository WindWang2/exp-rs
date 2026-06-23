// src/processing/providers/gdal_tools/algorithms/gdal_calc.cpp
#include "gdal_calc.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsrasterlayer.h>

void GdalCalcAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    addInputRasterLayerParameter("INPUT", "Input raster layer (band A)");
    addParameter(new QgsProcessingParameterString("EXPRESSION", "Calculation expression (e.g. A*2)",
                                                    QVariant(), false));
    addOutputRasterLayerParameter("OUTPUT", "Output raster layer");
}

QStringList GdalCalcAlgorithm::buildArgs(const QVariantMap &parameters,
                                           QgsProcessingContext &context,
                                           QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;

    args << "-A" << rasterLayerSource(parameters.value("INPUT"));

    QString expression = parameters.value("EXPRESSION").toString();
    args << "--calc=" + expression;

    args << "--outfile=" + parameters.value("OUTPUT").toString();

    return args;
}
