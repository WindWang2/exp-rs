// src/processing/providers/gdal_tools/algorithms/gdal_info.h
#pragma once

#include "../gdal_tool_wrapper.h"

class GdalInfoAlgorithm : public GdalToolWrapper
{
public:
    GdalInfoAlgorithm() = default;

    QString name() const override { return "gdalinfo"; }
    QString displayName() const override { return "GDAL Info (Raster Information)"; }
    QString group() const override { return "Raster Information"; }
    QString groupId() const override { return "rasterinformation"; }
    QStringList tags() const override { return { QObject::tr( "gdalinfo" ), QObject::tr( "metadata" ), QObject::tr( "raster" ), QObject::tr( "information" ), QObject::tr( "gdal" ) }; }
    QString toolName() const override { return "gdalinfo"; }

    QgsProcessingAlgorithm *createInstance() const override { return new GdalInfoAlgorithm(); }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
