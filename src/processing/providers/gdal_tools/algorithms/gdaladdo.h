// src/processing/providers/gdal_tools/algorithms/gdaladdo.h
#pragma once

#include "../gdal_tool_wrapper.h"

class GdalAddoAlgorithm : public GdalToolWrapper
{
public:
    GdalAddoAlgorithm() = default;

    QString name() const override { return "gdaladdo"; }
    QString displayName() const override { return "GDAL Add Overviews"; }
    QString group() const override { return "Raster Conversion"; }
    QString groupId() const override { return "rasterconversion"; }
    QStringList tags() const override { return { QObject::tr( "gdaladdo" ), QObject::tr( "overviews" ), QObject::tr( "pyramid" ), QObject::tr( "gdal" ) }; }
    QString toolName() const override { return "gdaladdo"; }

    QgsProcessingAlgorithm *createInstance() const override { return new GdalAddoAlgorithm(); }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};