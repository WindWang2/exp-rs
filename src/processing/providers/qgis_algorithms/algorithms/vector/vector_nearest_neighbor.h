// src/processing/providers/qgis_algorithms/algorithms/vector/vector_nearest_neighbor.h
#pragma once

#include <processing/qgsprocessingalgorithm.h>

class VectorNearestNeighborAlgorithm : public QgsProcessingAlgorithm
{
public:
    static const QString INPUT;
    static const QString REFERENCE_LAYER;
    static const QString MAX_DISTANCE;
    static const QString K_NEIGHBORS;
    static const QString OUTPUT;

    VectorNearestNeighborAlgorithm() = default;

    QString name() const override { return QStringLiteral( "vector_nearest_neighbor" ); }
    QString displayName() const override { return QObject::tr( "Nearest Neighbor Analysis" ); }
    QString group() const override { return QObject::tr( "Vector Analysis" ); }
    QString groupId() const override { return QStringLiteral( "vectoranalysis" ); }
    QStringList tags() const override { return { QObject::tr( "nearest" ), QObject::tr( "neighbor" ), QObject::tr( "distance" ), QObject::tr( "proximity" ), QObject::tr( "closest" ) }; }
    QgsProcessingAlgorithm *createInstance() const override { return new VectorNearestNeighborAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap &configuration = QVariantMap() ) override;
    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override;
};
