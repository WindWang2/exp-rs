// vector_fix_geometries.h — Fix Geometries algorithm
#pragma once

#include <processing/qgsprocessingalgorithm.h>
#include "algorithm_help_catalog.h"

class VectorFixGeometriesAlgorithm : public QgsProcessingAlgorithm
{
public:
    VectorFixGeometriesAlgorithm() = default;
    QString name() const override { return QStringLiteral("vector_fix_geometries"); }
    QString displayName() const override { return QObject::tr("Fix Geometries"); }
    QString group() const override { return QObject::tr("Vector geometry"); }
    QString groupId() const override { return QStringLiteral("vectorgeometry"); }
    QStringList tags() const override { return { QObject::tr("fix"), QObject::tr("repair"), QObject::tr("valid") }; }
    QString shortDescription() const override
    {
        return SicnuAlgorithmHelp::shortDescription( name(), displayName() );
    }
    QString shortHelpString() const override
    {
        return SicnuAlgorithmHelp::shortHelpString( name(), displayName(), QString(), tags() );
    }

    QgsProcessingAlgorithm *createInstance() const override { return new VectorFixGeometriesAlgorithm(); }

protected:
    void initAlgorithm(const QVariantMap &) override
    {
        addParameter(new QgsProcessingParameterFeatureSource(QStringLiteral("INPUT"), QObject::tr("Input layer"),
            QList<int>() << static_cast<int>(Qgis::ProcessingSourceType::VectorAnyGeometry)));
        addParameter(new QgsProcessingParameterFeatureSink(QStringLiteral("OUTPUT"), QObject::tr("Output")));
    }

    QVariantMap processAlgorithm(const QVariantMap &parameters, QgsProcessingContext &context, QgsProcessingFeedback *feedback) override
    {
        std::unique_ptr<QgsProcessingFeatureSource> source(parameterAsSource(parameters, QStringLiteral("INPUT"), context));
        if (!source)
            throw QgsProcessingException(invalidSourceError(parameters, QStringLiteral("INPUT")));

        QString dest;
        std::unique_ptr<QgsFeatureSink> sink(parameterAsSink(parameters, QStringLiteral("OUTPUT"), context, dest,
            source->fields(), source->wkbType(), source->sourceCrs()));
        if (!sink)
            throw QgsProcessingException(invalidSinkError(parameters, QStringLiteral("OUTPUT")));

        QgsFeatureIterator it = source->getFeatures();
        QgsFeature feat;
        long long total = source->featureCount();
        long long current = 0;
        long long fixedCount = 0;

        while (it.nextFeature(feat)) {
            if (feedback->isCanceled()) break;
            current++;
            if (total > 0) feedback->setProgress(100.0 * current / total);

            if (feat.hasGeometry()) {
                QgsGeometry geom = feat.geometry();
                if (!geom.isGeosValid()) {
                    QgsGeometry fixed = geom.makeValid();
                    if (!fixed.isNull()) {
                        feat.setGeometry(fixed);
                        fixedCount++;
                    }
                }
            }
            sink->addFeature(feat, QgsFeatureSink::FastInsert);
        }

        feedback->pushInfo(QObject::tr("Fixed %1 geometries").arg(fixedCount));

        QVariantMap results;
        results[QStringLiteral("OUTPUT")] = dest;
        return results;
    }
};
