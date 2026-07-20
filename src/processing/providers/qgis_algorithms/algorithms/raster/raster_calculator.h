// src/processing/providers/qgis_algorithms/algorithms/raster/raster_calculator.h
#pragma once

#include <processing/qgsprocessingalgorithm.h>
#include "algorithm_help_catalog.h"

class RasterCalculatorAlgorithm : public QgsProcessingAlgorithm
{
public:
    RasterCalculatorAlgorithm() = default;

    QString name() const override { return QStringLiteral( "raster_calculator" ); }
    QString displayName() const override { return QObject::tr( "Raster Calculator" ); }
    QString group() const override { return QObject::tr( "Raster" ); }
    QString groupId() const override { return QStringLiteral( "raster" ); }
    QStringList tags() const override { return { QObject::tr( "raster" ), QObject::tr( "calculator" ), QObject::tr( "expression" ) }; }
    QString shortDescription() const override
    {
        return SicnuAlgorithmHelp::shortDescription( name(), displayName() );
    }
    QString shortHelpString() const override
    {
        return SicnuAlgorithmHelp::shortHelpString( name(), displayName(), QString(), tags() );
    }


    QgsProcessingAlgorithm *createInstance() const override { return new RasterCalculatorAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap &configuration = QVariantMap() ) override;
    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override;
};
