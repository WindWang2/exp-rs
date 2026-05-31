// src/processing/providers/qgis_algorithms/algorithms/vector/vector_difference.h
#pragma once

#include <processing/qgsprocessingalgorithm.h>

class VectorDifferenceAlgorithm : public QgsProcessingAlgorithm
{
public:
    static const QString INPUT;
    static const QString OVERLAY;
    static const QString OUTPUT;

    VectorDifferenceAlgorithm() = default;

    QString name() const override { return QStringLiteral( "vector_difference" ); }
    QString displayName() const override { return QObject::tr( "Difference" ); }
    QString group() const override { return QObject::tr( "Vector Overlay" ); }
    QString groupId() const override { return QStringLiteral( "vectoroverlay" ); }
    QStringList tags() const override { return { QObject::tr( "difference" ), QObject::tr( "erase" ), QObject::tr( "subtract" ), QObject::tr( "overlay" ) }; }
    QgsProcessingAlgorithm *createInstance() const override { return new VectorDifferenceAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap &configuration = QVariantMap() ) override;
    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override;
};
