// src/processing/providers/gdal_tools/algorithms/ogrinfo.cpp
#include "ogrinfo.h"

#include <processing/qgsprocessingparameters.h>
#include <qgsvectorlayer.h>

void OgrInfoAlgorithm::initAlgorithm(const QVariantMap &configuration)
{
    Q_UNUSED(configuration);

    addInputVectorLayerParameter("INPUT", "Input vector layer");

    addParameter(new QgsProcessingParameterBoolean(
        "SUMMARY_ONLY", "Summary only (no feature details)", false));

    addParameter(new QgsProcessingParameterBoolean(
        "ALL_LAYERS", "Report on all layers", true));

    addParameter(new QgsProcessingParameterString(
        "LAYER_NAME", "Layer name to report on (empty for all)", QVariant(), false, true));

    addParameter(new QgsProcessingParameterString(
        "WHERE", "Attribute filter (SQL WHERE clause)", QVariant(), false, true));

    addParameter(new QgsProcessingParameterString(
        "SQL", "SQL query to execute", QVariant(), false, true));

    addParameter(new QgsProcessingParameterString(
        "EXTRA", "Additional ogrinfo arguments", QVariant(), false, true));

    addParameter(new QgsProcessingParameterFileDestination(
        "OUTPUT", "Info output (text file)", "Text files (*.txt)"));
}

QStringList OgrInfoAlgorithm::buildArgs(const QVariantMap &parameters,
                                         QgsProcessingContext &context,
                                         QgsProcessingFeedback *feedback)
{
    Q_UNUSED(context);
    Q_UNUSED(feedback);

    QStringList args;

    // Summary only mode
    if (parameters.value("SUMMARY_ONLY", false).toBool()) {
        args << "-so";
    }

    // All layers flag
    if (parameters.value("ALL_LAYERS", true).toBool()) {
        args << "-al";
    }

    // Attribute filter
    if (parameters.contains("WHERE") && !parameters.value("WHERE").toString().isEmpty()) {
        args << "-where" << parameters.value("WHERE").toString();
    }

    // SQL query
    if (parameters.contains("SQL") && !parameters.value("SQL").toString().isEmpty()) {
        args << "-sql" << parameters.value("SQL").toString();
    }

    // Extra arguments
    if (parameters.contains("EXTRA") && !parameters.value("EXTRA").toString().isEmpty()) {
        args << QProcess::splitCommand(parameters.value("EXTRA").toString());
    }

    // Input file
    args << vectorLayerSource(parameters.value("INPUT"));

    // Layer name (optional)
    if (parameters.contains("LAYER_NAME") && !parameters.value("LAYER_NAME").toString().isEmpty()) {
        args << parameters.value("LAYER_NAME").toString();
    }

    return args;
}
