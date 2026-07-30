// src/processing/providers/qgis_algorithms/algorithms/vector/vector_dissolve.h
#pragma once

#include <processing/qgsprocessingalgorithm.h>
#include "processing/algorithm_help_catalog.h"

class VectorDissolveAlgorithm : public QgsProcessingAlgorithm
{
public:
    static const QString INPUT;
    static const QString FIELD;
    static const QString OUTPUT;

    VectorDissolveAlgorithm() = default;

    QString name() const override { return QStringLiteral( "vector_dissolve" ); }
    QString displayName() const override { return QObject::tr( "Dissolve" ); }
    QString group() const override { return QObject::tr( "Vector Geometry" ); }
    QString groupId() const override { return QStringLiteral( "vectorgeometry" ); }
    QStringList tags() const override { return { QObject::tr( "dissolve" ), QObject::tr( "merge" ), QObject::tr( "combine" ) }; }
    QString shortDescription() const override
    {
        return SicnuAlgorithmHelp::shortDescription( name(), displayName() );
    }
    QString shortHelpString() const override
    {
        return SicnuAlgorithmHelp::shortHelpString( name(), displayName(), QString(), tags() );
    }

    QgsProcessingAlgorithm *createInstance() const override { return new VectorDissolveAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap &configuration = QVariantMap() ) override;
    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override;
};
