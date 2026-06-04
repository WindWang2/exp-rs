// src/processing/providers/gdal_tools/algorithms/gdal_contour.h
#pragma once

#include "../gdal_tool_wrapper.h"

class GdalContourAlgorithm : public GdalToolWrapper
{
public:
    GdalContourAlgorithm() = default;

    QString name() const override { return "gdal_contour"; }
    QString displayName() const override { return "GDAL Contour (Raster to Contours)"; }
    QString group() const override { return "Raster Conversion"; }
    QString groupId() const override { return "rasterconversion"; }
    QStringList tags() const override { return { QObject::tr( "gdal_contour" ), QObject::tr( "contour" ), QObject::tr( "raster to vector" ), QObject::tr( "gdal" ) }; }
    QString toolName() const override { return "gdal_contour"; }

    QgsProcessingAlgorithm *createInstance() const override { return new GdalContourAlgorithm(); }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
