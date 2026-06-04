// src/processing/providers/gdal_tools/algorithms/gdal_polygonize.h
#pragma once

#include "../gdal_tool_wrapper.h"

class GdalPolygonizeAlgorithm : public GdalToolWrapper
{
public:
    GdalPolygonizeAlgorithm() = default;

    QString name() const override { return "gdal_polygonize"; }
    QString displayName() const override { return "GDAL Polygonize (Raster to Polygon)"; }
    QString group() const override { return "Raster Conversion"; }
    QString groupId() const override { return "rasterconversion"; }
    QStringList tags() const override { return { QObject::tr( "gdal_polygonize" ), QObject::tr( "polygonize" ), QObject::tr( "raster to polygon" ), QObject::tr( "gdal" ) }; }
    QString toolName() const override { return "gdal_polygonize"; }

    QgsProcessingAlgorithm *createInstance() const override { return new GdalPolygonizeAlgorithm(); }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
