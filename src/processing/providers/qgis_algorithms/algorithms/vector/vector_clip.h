// src/processing/providers/qgis_algorithms/algorithms/vector/vector_clip.h
#pragma once

#include <processing/qgsprocessingalgorithm.h>

class VectorClipAlgorithm : public QgsProcessingAlgorithm
{
public:
    static const QString INPUT;
    static const QString OVERLAY;
    static const QString OUTPUT;

    VectorClipAlgorithm() = default;

    QString name() const override { return QStringLiteral( "vector_clip" ); }
    QString displayName() const override { return QObject::tr( "Clip Vector" ); }
    QString group() const override { return QObject::tr( "Vector Overlay" ); }
    QString groupId() const override { return QStringLiteral( "vectoroverlay" ); }
    QStringList tags() const override { return { QObject::tr( "clip" ), QObject::tr( "cut" ), QObject::tr( "trim" ) }; }
    QString provider() const override { return QStringLiteral( "qgis_algorithms" ); }

    QgsProcessingAlgorithm *createInstance() const override { return new VectorClipAlgorithm(); }

protected:
    void initAlgorithm( const QVariantMap &configuration = QVariantMap() ) override;
    QVariantMap processAlgorithm( const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback ) override;
};
