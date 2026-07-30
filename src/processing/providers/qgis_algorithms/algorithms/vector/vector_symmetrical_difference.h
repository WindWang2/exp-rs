// src/processing/providers/qgis_algorithms/algorithms/vector/vector_symmetrical_difference.h
#pragma once

#include <processing/qgsprocessingalgorithm.h>
#include "processing/algorithm_help_catalog.h"

class VectorSymmetricalDifferenceAlgorithm : public QgsProcessingAlgorithm
{
public:
    static const QString INPUT;
    static const QString OVERLAY;
    static const QString OUTPUT;

    VectorSymmetricalDifferenceAlgorithm() = default;

    QString name() const override { return QStringLiteral( "vector_symmetrical_difference" ); }
    QString displayName() const override { return QObject::tr( "Symmetrical Difference" ); }
    QString group() const override { return QObject::tr( "Vector Overlay" ); }
    QString groupId() const override { return QStringLiteral( "vectoroverlay" ); }
    QStringList tags() const override { return { QObject::tr( "symmetrical" ), QObject::tr( "difference" ), QObject::tr( "xor" ), QObject::tr( "overlay" ) }; }
    QString shortDescription() const override
    {
        return SicnuAlgorithmHelp::shortDescription( name(), displayName() );
    }
    QString shortHelpString() const override
    {
        return SicnuAlgorithmHelp::shortHelpString( name(), displayName(), QString(), tags() );
    }

    QgsProcessingAlgorithm *createInstance() const override { return new VectorSymmetricalDifferenceAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap &configuration = QVariantMap() ) override;
    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override;
};
