// src/processing/providers/qgis_algorithms/algorithms/vector/vector_merge.h
#pragma once

#include <processing/qgsprocessingalgorithm.h>
#include "processing/algorithm_help_catalog.h"

class VectorMergeAlgorithm : public QgsProcessingAlgorithm
{
public:
    static const QString INPUT_LAYERS;
    static const QString OUTPUT;

    VectorMergeAlgorithm() = default;

    QString name() const override { return QStringLiteral( "vector_merge" ); }
    QString displayName() const override { return QObject::tr( "Merge Vector Layers" ); }
    QString group() const override { return QObject::tr( "Vector General" ); }
    QString groupId() const override { return QStringLiteral( "vectorgeneral" ); }
    QStringList tags() const override { return { QObject::tr( "merge" ), QObject::tr( "combine" ), QObject::tr( "concatenate" ) }; }
    QString shortDescription() const override
    {
        return SicnuAlgorithmHelp::shortDescription( name(), displayName() );
    }
    QString shortHelpString() const override
    {
        return SicnuAlgorithmHelp::shortHelpString( name(), displayName(), QString(), tags() );
    }

    QgsProcessingAlgorithm *createInstance() const override { return new VectorMergeAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap &configuration = QVariantMap() ) override;
    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override;
};
