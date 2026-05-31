// src/processing/providers/gdal_tools/algorithms/gdal_sieve.h
#pragma once

#include "../gdal_tool_wrapper.h"

class GdalSieveAlgorithm : public GdalToolWrapper
{
public:
    GdalSieveAlgorithm() = default;

    QString name() const override { return "gdal_sieve"; }
    QString displayName() const override { return "GDAL Sieve (Remove Small Polygons)"; }
    QString group() const override { return "Raster Analysis"; }
    QString groupId() const override { return "rasteranalysis"; }
    QString toolName() const override { return "gdal_sieve"; }
    QString shortHelpString() const override
    {
        return QObject::tr("Removes small raster polygons (regions of connected pixels sharing "
                           "the same value) from a classified raster. Polygons smaller than the "
                           "specified threshold size (in pixels) are merged into the largest "
                           "neighbouring polygon.");
    }

    QgsProcessingAlgorithm *createInstance() const override { return new GdalSieveAlgorithm(); }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
