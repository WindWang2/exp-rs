// src/processing/providers/qgis_algorithms/algorithms/vector/vector_spatial_query.h
#pragma once

#include <processing/qgsprocessingalgorithm.h>
#include "processing/algorithm_help_catalog.h"

class VectorSpatialQueryAlgorithm : public QgsProcessingAlgorithm
{
public:
    static const QString INPUT;
    static const QString INTERSECT;
    static const QString PREDICATE;
    static const QString OUTPUT;

    VectorSpatialQueryAlgorithm() = default;

    QString name() const override { return QStringLiteral( "vector_spatial_query" ); }
    QString displayName() const override { return QObject::tr( "Spatial Query" ); }
    QString group() const override { return QObject::tr( "Vector Selection" ); }
    QString groupId() const override { return QStringLiteral( "vectorselection" ); }
    QStringList tags() const override { return { QObject::tr( "spatial" ), QObject::tr( "query" ), QObject::tr( "select" ), QObject::tr( "filter" ) }; }
    QString shortDescription() const override
    {
        return SicnuAlgorithmHelp::shortDescription( name(), displayName() );
    }
    QString shortHelpString() const override
    {
        return SicnuAlgorithmHelp::shortHelpString( name(), displayName(), QString(), tags() );
    }

    QgsProcessingAlgorithm *createInstance() const override { return new VectorSpatialQueryAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap &configuration = QVariantMap() ) override;
    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override;
};
