// src/processing/providers/qgis_algorithms/algorithms/raster/raster_resample.h
#pragma once

#include <processing/qgsprocessingalgorithm.h>
#include "processing/algorithm_help_catalog.h"

class RasterResampleAlgorithm : public QgsProcessingAlgorithm
{
public:
    RasterResampleAlgorithm() = default;

    QString name() const override { return QStringLiteral( "raster_resample" ); }
    QString displayName() const override { return QObject::tr( "Resample Raster" ); }
    QString group() const override { return QObject::tr( "Raster" ); }
    QString groupId() const override { return QStringLiteral( "raster" ); }
    QStringList tags() const override { return { QObject::tr( "raster" ), QObject::tr( "resample" ), QObject::tr( "resolution" ) }; }
    QString shortDescription() const override
    {
        return SicnuAlgorithmHelp::shortDescription( name(), displayName() );
    }
    QString shortHelpString() const override
    {
        return SicnuAlgorithmHelp::shortHelpString( name(), displayName(), QString(), tags() );
    }


    QgsProcessingAlgorithm *createInstance() const override { return new RasterResampleAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap &configuration = QVariantMap() ) override;
    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override;
};
