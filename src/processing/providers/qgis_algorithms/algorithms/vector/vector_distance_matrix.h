// src/processing/providers/qgis_algorithms/algorithms/vector/vector_distance_matrix.h
#pragma once

#include <processing/qgsprocessingalgorithm.h>
#include "processing/algorithm_help_catalog.h"

class VectorDistanceMatrixAlgorithm : public QgsProcessingAlgorithm
{
public:
    static const QString INPUT;
    static const QString TARGET_LAYER;
    static const QString INPUT_FIELD;
    static const QString TARGET_FIELD;
    static const QString OUTPUT_TYPE;
    static const QString NEAREST_ONLY;
    static const QString OUTPUT;

    VectorDistanceMatrixAlgorithm() = default;

    QString name() const override { return QStringLiteral( "vector_distance_matrix" ); }
    QString displayName() const override { return QObject::tr( "Distance Matrix" ); }
    QString group() const override { return QObject::tr( "Vector Analysis" ); }
    QString groupId() const override { return QStringLiteral( "vectoranalysis" ); }
    QStringList tags() const override { return { QObject::tr( "distance" ), QObject::tr( "matrix" ), QObject::tr( "proximity" ), QObject::tr( "pairwise" ) }; }
    QString shortDescription() const override
    {
        return SicnuAlgorithmHelp::shortDescription( name(), displayName() );
    }
    QString shortHelpString() const override
    {
        return SicnuAlgorithmHelp::shortHelpString( name(), displayName(), QString(), tags() );
    }

    QgsProcessingAlgorithm *createInstance() const override { return new VectorDistanceMatrixAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap &configuration = QVariantMap() ) override;
    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override;
};
