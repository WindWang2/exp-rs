// src/processing/providers/gdal_tools/algorithms/gdalmanage.h
#pragma once

#include "../gdal_tool_wrapper.h"

class GdalManageAlgorithm : public GdalToolWrapper
{
public:
    GdalManageAlgorithm() = default;

    QString name() const override { return "gdalmanage"; }
    QString displayName() const override { return "GDAL Manage (Raster Data Management)"; }
    QString group() const override { return "Raster Analysis"; }
    QString groupId() const override { return "rasteranalysis"; }
    QStringList tags() const override { return { QObject::tr( "gdalmanage" ), QObject::tr( "management" ), QObject::tr( "raster" ), QObject::tr( "gdal" ) }; }
    QString toolName() const override { return "gdalmanage"; }

    QgsProcessingAlgorithm *createInstance() const override { return new GdalManageAlgorithm(); }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
