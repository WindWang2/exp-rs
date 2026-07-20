// src/processing/providers/qgis_algorithms/algorithms/vector/vector_field_calculator.h
#pragma once

#include <processing/qgsprocessingalgorithm.h>
#include "algorithm_help_catalog.h"

class VectorFieldCalculatorAlgorithm : public QgsProcessingAlgorithm
{
public:
    static const QString INPUT;
    static const QString FIELD_NAME;
    static const QString FIELD_TYPE;
    static const QString FIELD_LENGTH;
    static const QString FIELD_PRECISION;
    static const QString FORMULA;
    static const QString OUTPUT;

    VectorFieldCalculatorAlgorithm() = default;

    QString name() const override { return QStringLiteral( "vector_field_calculator" ); }
    QString displayName() const override { return QObject::tr( "Field Calculator" ); }
    QString group() const override { return QObject::tr( "Vector Table" ); }
    QString groupId() const override { return QStringLiteral( "vectortable" ); }
    QStringList tags() const override { return { QObject::tr( "field" ), QObject::tr( "calculator" ), QObject::tr( "expression" ), QObject::tr( "attribute" ), QObject::tr( "column" ) }; }
    QString shortDescription() const override
    {
        return SicnuAlgorithmHelp::shortDescription( name(), displayName() );
    }
    QString shortHelpString() const override
    {
        return SicnuAlgorithmHelp::shortHelpString( name(), displayName(), QString(), tags() );
    }

    QgsProcessingAlgorithm *createInstance() const override { return new VectorFieldCalculatorAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap &configuration = QVariantMap() ) override;
    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override;
};
