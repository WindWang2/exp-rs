// src/processing/providers/gdal_tools/algorithms/gdal_rasterize.h
#pragma once

#include "../gdal_tool_wrapper.h"

class GdalRasterizeAlgorithm : public GdalToolWrapper
{
public:
    GdalRasterizeAlgorithm() = default;

    QString name() const override { return "gdal_rasterize"; }
    QString displayName() const override { return "GDAL Rasterize (Vector to Raster)"; }
    QString group() const override { return "Raster Conversion"; }
    QString groupId() const override { return "rasterconversion"; }
    QStringList tags() const override { return { QObject::tr( "gdal_rasterize" ), QObject::tr( "rasterize" ), QObject::tr( "vector to raster" ), QObject::tr( "burn" ), QObject::tr( "gdal" ) }; }
    QString toolName() const override { return "gdal_rasterize"; }
    QString shortHelpString() const override
    {
        return QObject::tr("Burns vector geometries (points, lines, polygons) into a raster. "
                           "The output raster can be initialized from an existing template raster "
                           "or created with specified extent and resolution. An attribute field can "
                           "be used as the burn value, or a fixed value can be specified.");
    }

    QgsProcessingAlgorithm *createInstance() const override { return new GdalRasterizeAlgorithm(); }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
