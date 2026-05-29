// src/processing/providers/qgis_algorithms/algorithms/vector/vector_dissolve.h
#pragma once

#include <processing/qgsprocessingalgorithm.h>

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
    QString provider() const override { return QStringLiteral( "qgis_algorithms" ); }

    QgsProcessingAlgorithm *createInstance() const override { return new VectorDissolveAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap &configuration = QVariantMap() ) override;
    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override;
};
