// vector_smooth_geometry.h — Smooth Geometry algorithm
#pragma once

#include <processing/qgsprocessingalgorithm.h>
#include "processing/algorithm_help_catalog.h"

class VectorSmoothGeometryAlgorithm : public QgsProcessingAlgorithm
{
public:
    VectorSmoothGeometryAlgorithm() = default;
    QString name() const override { return QStringLiteral("vector_smooth_geometry"); }
    QString displayName() const override { return QObject::tr("Smooth Geometry"); }
    QString group() const override { return QObject::tr("Vector geometry"); }
    QString groupId() const override { return QStringLiteral("vectorgeometry"); }
    QStringList tags() const override { return { QObject::tr("smooth"), QObject::tr("curve"), QObject::tr("round") }; }
    QString shortDescription() const override
    {
        return SicnuAlgorithmHelp::shortDescription( name(), displayName() );
    }
    QString shortHelpString() const override
    {
        return SicnuAlgorithmHelp::shortHelpString( name(), displayName(), QString(), tags() );
    }

    QgsProcessingAlgorithm *createInstance() const override { return new VectorSmoothGeometryAlgorithm(); }

protected:
    void initAlgorithm(const QVariantMap &) override
    {
        addParameter(new QgsProcessingParameterFeatureSource(QStringLiteral("INPUT"), QObject::tr("Input layer"),
            QList<int>() << static_cast<int>(Qgis::ProcessingSourceType::VectorAnyGeometry)));
        addParameter(new QgsProcessingParameterNumber(QStringLiteral("ITERATIONS"), QObject::tr("Iterations"),
            Qgis::ProcessingNumberParameterType::Integer, 1, false, 1, 10));
        addParameter(new QgsProcessingParameterNumber(QStringLiteral("OFFSET"), QObject::tr("Offset"),
            Qgis::ProcessingNumberParameterType::Double, 0.25, false, 0.01, 1.0));
        addParameter(new QgsProcessingParameterFeatureSink(QStringLiteral("OUTPUT"), QObject::tr("Output")));
    }

    QVariantMap processAlgorithm(const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback) override
    {
        std::unique_ptr<QgsProcessingFeatureSource> source(parameterAsSource(parameters, QStringLiteral("INPUT"), context));
        if (!source)
            throw QgsProcessingException(invalidSourceError(parameters, QStringLiteral("INPUT")));

        int iterations = parameterAsInt(parameters, QStringLiteral("ITERATIONS"), context);
        double offset = parameterAsDouble(parameters, QStringLiteral("OFFSET"), context);

        QString dest;
        std::unique_ptr<QgsFeatureSink> sink(parameterAsSink(parameters, QStringLiteral("OUTPUT"), context, dest,
            source->fields(), source->wkbType(), source->sourceCrs()));
        if (!sink)
            throw QgsProcessingException(invalidSinkError(parameters, QStringLiteral("OUTPUT")));

        QgsFeatureIterator it = source->getFeatures();
        QgsFeature feat;
        long long total = source->featureCount();
        long long current = 0;

        while (it.nextFeature(feat)) {
            if (feedback->isCanceled()) break;
            current++;
            if (total > 0) feedback->setProgress(100.0 * current / total);

            if (feat.hasGeometry()) {
                QgsGeometry smoothed = feat.geometry().smooth(iterations, offset);
                feat.setGeometry(smoothed);
            }
            sink->addFeature(feat, QgsFeatureSink::FastInsert);
        }

        QVariantMap results;
        results[QStringLiteral("OUTPUT")] = dest;
        return results;
    }
};
