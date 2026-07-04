// src/processing/providers/gdal_tools/algorithms/gdal_edit.h
#pragma once

#include "../gdal_tool_wrapper.h"

class GdalEditAlgorithm : public GdalToolWrapper
{
public:
    GdalEditAlgorithm() = default;

    QString name() const override { return "gdal_edit"; }
    QString displayName() const override { return "GDAL Edit (Raster Metadata)"; }
    QString group() const override { return "Raster Information"; }
    QString groupId() const override { return "rasterinformation"; }
    QStringList tags() const override
    {
        return { QObject::tr( "gdal_edit" ), QObject::tr( "metadata" ), QObject::tr( "nodata" ),
                 QObject::tr( "projection" ), QObject::tr( "gdal" ) };
    }
    QString toolName() const override { return "gdal_edit.py"; }

    QgsProcessingAlgorithm *createInstance() const override { return new GdalEditAlgorithm(); }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};