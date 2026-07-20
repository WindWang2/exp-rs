// src/processing/providers/qgis_algorithms/algorithms/raster/raster_ndvi.h
#pragma once

#include <processing/qgsprocessingalgorithm.h>
#include "algorithm_help_catalog.h"

class RasterNdviAlgorithm : public QgsProcessingAlgorithm
{
public:
    RasterNdviAlgorithm() = default;

    QString name() const override { return QStringLiteral( "raster_ndvi" ); }
    QString displayName() const override { return QObject::tr( "Calculate NDVI" ); }
    QString group() const override { return QObject::tr( "Raster" ); }
    QString groupId() const override { return QStringLiteral( "raster" ); }
    QStringList tags() const override { return { QObject::tr( "raster" ), QObject::tr( "ndvi" ), QObject::tr( "vegetation" ) }; }
    QString shortDescription() const override
    {
        return SicnuAlgorithmHelp::shortDescription( name(), displayName() );
    }
    QString shortHelpString() const override
    {
        return SicnuAlgorithmHelp::shortHelpString( name(), displayName(), QString(), tags() );
    }


    QgsProcessingAlgorithm *createInstance() const override { return new RasterNdviAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap &configuration = QVariantMap() ) override;
    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override;
};
