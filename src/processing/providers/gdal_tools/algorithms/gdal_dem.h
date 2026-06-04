// src/processing/providers/gdal_tools/algorithms/gdal_dem.h
#pragma once

#include "../gdal_tool_wrapper.h"

class GdalDemAlgorithm : public GdalToolWrapper
{
public:
    GdalDemAlgorithm() = default;

    QString name() const override { return "gdaldem"; }
    QString displayName() const override { return "GDAL DEM (Terrain Analysis)"; }
    QString group() const override { return "Raster Analysis"; }
    QString groupId() const override { return "rasteranalysis"; }
    QStringList tags() const override { return { QObject::tr( "gdaldem" ), QObject::tr( "terrain" ), QObject::tr( "hillshade" ), QObject::tr( "slope" ), QObject::tr( "aspect" ), QObject::tr( "gdal" ) }; }
    QString toolName() const override { return "gdaldem"; }

    QgsProcessingAlgorithm *createInstance() const override { return new GdalDemAlgorithm(); }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
