// src/processing/providers/qgis_algorithms/algorithms/vector/vector_buffer.h
#pragma once

#include <processing/qgsprocessingalgorithm.h>
#include "algorithm_help_catalog.h"

class VectorBufferAlgorithm : public QgsProcessingAlgorithm
{
public:
    static const QString INPUT;
    static const QString DISTANCE;
    static const QString CAP_STYLE;
    static const QString SEGMENTS;
    static const QString OUTPUT;

    VectorBufferAlgorithm() = default;

    QString name() const override { return QStringLiteral( "vector_buffer" ); }
    QString displayName() const override { return QObject::tr( "Buffer" ); }
    QString group() const override { return QObject::tr( "Vector Geometry" ); }
    QString groupId() const override { return QStringLiteral( "vectorgeometry" ); }
    QStringList tags() const override { return { QObject::tr( "buffer" ), QObject::tr( "distance" ), QObject::tr( "polygon" ) }; }
    QString shortDescription() const override
    {
        return SicnuAlgorithmHelp::shortDescription( name(), displayName() );
    }
    QString shortHelpString() const override
    {
        return SicnuAlgorithmHelp::shortHelpString( name(), displayName(), QString(), tags() );
    }

    QgsProcessingAlgorithm *createInstance() const override { return new VectorBufferAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap &configuration = QVariantMap() ) override;
    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override;
};
