// src/processing/providers/qgis_algorithms/algorithms/vector/vector_select_by_location.h
#pragma once

#include <processing/qgsprocessingalgorithm.h>
#include "algorithm_help_catalog.h"

class VectorSelectByLocationAlgorithm : public QgsProcessingAlgorithm
{
public:
    static const QString INPUT;
    static const QString INTERSECT;
    static const QString PREDICATE;
    static const QString OUTPUT;

    VectorSelectByLocationAlgorithm() = default;

    QString name() const override { return QStringLiteral( "vector_select_by_location" ); }
    QString displayName() const override { return QObject::tr( "Select by Location" ); }
    QString group() const override { return QObject::tr( "Vector Selection" ); }
    QString groupId() const override { return QStringLiteral( "vectorselection" ); }
    QStringList tags() const override { return { QObject::tr( "select" ), QObject::tr( "location" ), QObject::tr( "spatial" ), QObject::tr( "filter" ) }; }
    QString shortDescription() const override
    {
        return SicnuAlgorithmHelp::shortDescription( name(), displayName() );
    }
    QString shortHelpString() const override
    {
        return SicnuAlgorithmHelp::shortHelpString( name(), displayName(), QString(), tags() );
    }

    QgsProcessingAlgorithm *createInstance() const override { return new VectorSelectByLocationAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap &configuration = QVariantMap() ) override;
    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override;
};
