// vector_multipart_to_singlepart.h — Multipart to Singlepart algorithm
#pragma once

#include <processing/qgsprocessingalgorithm.h>
#include "processing/algorithm_help_catalog.h"
#include "../../algorithm_write_guards.h"

class VectorMultipartToSinglepartAlgorithm : public QgsProcessingAlgorithm
{
public:
    VectorMultipartToSinglepartAlgorithm() = default;
    QString name() const override { return QStringLiteral("vector_multipart_to_singlepart"); }
    QString displayName() const override { return QObject::tr("Multipart to Singlepart"); }
    QString group() const override { return QObject::tr("Vector geometry"); }
    QString groupId() const override { return QStringLiteral("vectorgeometry"); }
    QStringList tags() const override { return { QObject::tr("multipart"), QObject::tr("singlepart"), QObject::tr("explode") }; }
    QString shortDescription() const override
    {
        return SicnuAlgorithmHelp::shortDescription( name(), displayName() );
    }
    QString shortHelpString() const override
    {
        return SicnuAlgorithmHelp::shortHelpString( name(), displayName(), QString(), tags() );
    }

    QgsProcessingAlgorithm *createInstance() const override { return new VectorMultipartToSinglepartAlgorithm(); }

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

        sicnu::qgis_algorithms::PartialOutputGuard destGuard;
        QString dest;
        std::unique_ptr<QgsFeatureSink> sink(parameterAsSink(parameters, QStringLiteral("OUTPUT"), context, dest,
            source->fields(), source->wkbType(), source->sourceCrs()));
        if (!sink)
            throw QgsProcessingException(invalidSinkError(parameters, QStringLiteral("OUTPUT")));
        destGuard.arm( dest );

        QgsFeatureIterator it = source->getFeatures();
        QgsFeature feat;
        long long total = source->featureCount();
        long long current = 0;
        long long outputCount = 0;

        while (it.nextFeature(feat)) {
            sicnu::qgis_algorithms::checkCanceled( feedback );
            current++;
            if (total > 0) feedback->setProgress(100.0 * current / total);

            if (!feat.hasGeometry()) {
                sicnu::qgis_algorithms::addFeatureChecked( sink.get(), feat, feedback );
                outputCount++;
                continue;
            }

            QgsGeometry geom = feat.geometry();
            if (geom.isMultipart()) {
                // Explode multipart to singlepart using asGeometryCollection
                QVector<QgsGeometry> parts = geom.asGeometryCollection();
                for (const QgsGeometry &part : parts) {
                    QgsFeature outputFeat = feat;
                    outputFeat.setGeometry(part);
                    sicnu::qgis_algorithms::addFeatureChecked( sink.get(), outputFeat, feedback );
                    outputCount++;
                }
            } else {
                sicnu::qgis_algorithms::addFeatureChecked( sink.get(), feat, feedback );
                outputCount++;
            }
        }

        sicnu::qgis_algorithms::flushSinkChecked( sink.get() );
        destGuard.disarm();

        QVariantMap results;
        results[QStringLiteral("OUTPUT")] = dest;
        return results;
    }
};
