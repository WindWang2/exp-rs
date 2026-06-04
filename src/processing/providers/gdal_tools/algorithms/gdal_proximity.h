// src/processing/providers/gdal_tools/algorithms/gdal_proximity.h
#pragma once

#include "../gdal_tool_wrapper.h"

class GdalProximityAlgorithm : public GdalToolWrapper
{
public:
    GdalProximityAlgorithm() = default;

    QString name() const override { return "gdal_proximity"; }
    QString displayName() const override { return "GDAL Proximity (Raster Distance)"; }
    QString group() const override { return "Raster Analysis"; }
    QString groupId() const override { return "rasteranalysis"; }
    QStringList tags() const override { return { QObject::tr( "gdal_proximity" ), QObject::tr( "proximity" ), QObject::tr( "distance" ), QObject::tr( "raster" ), QObject::tr( "gdal" ) }; }
    QString toolName() const override { return "gdal_proximity"; }
    QString shortHelpString() const override
    {
        return QObject::tr("Generates a raster proximity map indicating the distance from the center "
                           "of each pixel to the center of the nearest target pixel. Target pixels are "
                           "those with a value in the source raster matching the specified target value.");
    }

    QgsProcessingAlgorithm *createInstance() const override { return new GdalProximityAlgorithm(); }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
