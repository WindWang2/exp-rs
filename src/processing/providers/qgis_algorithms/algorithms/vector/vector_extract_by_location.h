// src/processing/providers/qgis_algorithms/algorithms/vector/vector_extract_by_location.h
#pragma once

#include <processing/qgsprocessingalgorithm.h>

class VectorExtractByLocationAlgorithm : public QgsProcessingAlgorithm
{
public:
    static const QString INPUT;
    static const QString INTERSECT;
    static const QString PREDICATE;
    static const QString OUTPUT;

    VectorExtractByLocationAlgorithm() = default;

    QString name() const override { return QStringLiteral( "vector_extract_by_location" ); }
    QString displayName() const override { return QObject::tr( "Extract by Location" ); }
    QString group() const override { return QObject::tr( "Vector Selection" ); }
    QString groupId() const override { return QStringLiteral( "vectorselection" ); }
    QStringList tags() const override { return { QObject::tr( "extract" ), QObject::tr( "location" ), QObject::tr( "spatial" ), QObject::tr( "filter" ) }; }
    QgsProcessingAlgorithm *createInstance() const override { return new VectorExtractByLocationAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap &configuration = QVariantMap() ) override;
    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override;
};
