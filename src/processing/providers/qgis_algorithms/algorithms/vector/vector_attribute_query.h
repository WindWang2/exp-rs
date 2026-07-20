// src/processing/providers/qgis_algorithms/algorithms/vector/vector_attribute_query.h
#pragma once

#include <processing/qgsprocessingalgorithm.h>
#include "algorithm_help_catalog.h"

class VectorAttributeQueryAlgorithm : public QgsProcessingAlgorithm
{
public:
    static const QString INPUT;
    static const QString EXPRESSION;
    static const QString OUTPUT;

    VectorAttributeQueryAlgorithm() = default;

    QString name() const override { return QStringLiteral( "vector_attribute_query" ); }
    QString displayName() const override { return QObject::tr( "Attribute Query (Select by Expression)" ); }
    QString group() const override { return QObject::tr( "Vector Selection" ); }
    QString groupId() const override { return QStringLiteral( "vectorselection" ); }
    QStringList tags() const override { return { QObject::tr( "query" ), QObject::tr( "expression" ), QObject::tr( "filter" ), QObject::tr( "select" ) }; }
    QString shortDescription() const override
    {
        return SicnuAlgorithmHelp::shortDescription( name(), displayName() );
    }
    QString shortHelpString() const override
    {
        return SicnuAlgorithmHelp::shortHelpString( name(), displayName(), QString(), tags() );
    }

    QgsProcessingAlgorithm *createInstance() const override { return new VectorAttributeQueryAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap &configuration = QVariantMap() ) override;
    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override;
};
