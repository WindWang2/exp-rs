// src/processing/providers/gdal_tools/algorithms/gdal_fillnodata.h
#pragma once

#include "../gdal_tool_wrapper.h"

class GdalFillNodataAlgorithm : public GdalToolWrapper
{
public:
    GdalFillNodataAlgorithm() = default;

    QString name() const override { return "gdal_fillnodata"; }
    QString displayName() const override { return "GDAL Fill Nodata (Interpolate)"; }
    QString group() const override { return "Raster Analysis"; }
    QString groupId() const override { return "rasteranalysis"; }
    QStringList tags() const override { return { QObject::tr( "gdal_fillnodata" ), QObject::tr( "nodata" ), QObject::tr( "interpolation" ), QObject::tr( "fill" ), QObject::tr( "gdal" ) }; }
    QString toolName() const override { return "gdal_fillnodata"; }
    QString shortHelpString() const override
    {
        return QObject::tr("Fills selected raster regions (nodata regions) by interpolation from "
                           "the edges of the regions. The algorithm uses an inverse distance "
                           "weighting approach with smoothing iterations. Useful for filling gaps "
                           "in DEMs or other continuous rasters.");
    }

    QgsProcessingAlgorithm *createInstance() const override { return new GdalFillNodataAlgorithm(); }

    void initAlgorithm(const QVariantMap &configuration = QVariantMap()) override;

protected:
    QStringList buildArgs(const QVariantMap &parameters,
                          QgsProcessingContext &context,
                          QgsProcessingFeedback *feedback) override;
};
