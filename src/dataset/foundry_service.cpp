// foundry_service.cpp — Dataset Foundry façade.
#include "foundry_service.h"

#include "dataset_manifest.h"
#include "split.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace sicnu::dataset
{

DatasetFoundryService::DatasetFoundryService( DatasetStore *store )
  : m_store( store )
{
}

sicnu::data::Result<QPair<qint64, QVector<QVariantMap>>> DatasetFoundryService::listDatasets(
    qint64 offset, qint64 limit ) const
{
    using Result = sicnu::data::Result<QPair<qint64, QVector<QVariantMap>>>;
    if ( !m_store || !m_store->isOpen() )
    {
        return Result::failure( Diagnostic{
            QStringLiteral( "dataset.store_closed" ),
            QStringLiteral( "dataset store is not open" ),
            DiagnosticSeverity::Error,
        } );
    }
    return m_store->listDatasets( offset, limit );
}

sicnu::data::Result<QJsonObject> DatasetFoundryService::inspectVersion(
    const DatasetVersionId &versionId ) const
{
    using Result = sicnu::data::Result<QJsonObject>;
    if ( !m_store || !m_store->isOpen() )
    {
        return Result::failure( Diagnostic{
            QStringLiteral( "dataset.store_closed" ),
            QStringLiteral( "dataset store is not open" ),
            DiagnosticSeverity::Error,
        } );
    }
    const auto record = m_store->versionById( versionId );
    if ( !record )
    {
        return Result::failure( Diagnostic{
            QStringLiteral( "dataset.version_not_found" ),
            QStringLiteral( "version not found: %1" ).arg( versionId.toString() ),
            DiagnosticSeverity::Error,
        } );
    }

    QJsonObject json;
    json.insert( QStringLiteral( "version_id" ), record->versionId() );
    json.insert( QStringLiteral( "dataset_id" ), record->datasetId() );
    json.insert( QStringLiteral( "parent_version_id" ), record->parentVersionId() );
    json.insert( QStringLiteral( "status" ),
                 datasetVersionStatusToString( record->status() ) );
    json.insert( QStringLiteral( "quality_level" ),
                 datasetQualityLevelToString( record->qualityLevel() ) );
    json.insert( QStringLiteral( "fingerprint" ), record->fingerprint() );
    json.insert( QStringLiteral( "note" ), record->note() );
    json.insert( QStringLiteral( "sample_count" ), m_store->sampleCount( versionId ) );

    const auto parsed = DatasetManifest::fromJson(
        QJsonDocument::fromJson( record->manifestJson().toUtf8() ).object() );
    if ( parsed )
    {
        json.insert( QStringLiteral( "role" ), datasetRoleToString( parsed->role() ) );
        json.insert( QStringLiteral( "name" ), parsed->name() );
        json.insert( QStringLiteral( "modality" ), parsed->schema().modality );
        json.insert( QStringLiteral( "sensor" ), parsed->schema().sensor );
        if ( !parsed->labelSchema().isNull() )
        {
            QJsonObject label;
            label.insert( QStringLiteral( "schema_id" ), parsed->labelSchema().schemaId );
            label.insert( QStringLiteral( "version" ), qint64( parsed->labelSchema().version ) );
            json.insert( QStringLiteral( "label_schema" ), label );
        }
        json.insert( QStringLiteral( "split_manifests" ),
                     QJsonArray::fromStringList( parsed->splitManifestIds() ) );
    }

    const QVector<SplitManifest> splits = m_store->splitManifestsForVersion( versionId );
    QJsonArray splitIds;
    for ( const SplitManifest &split : splits )
        splitIds.append( split.manifestId() );
    json.insert( QStringLiteral( "stored_split_ids" ), splitIds );

    return Result::success( json );
}

sicnu::data::Result<QVector<DatasetVersionRecord>> DatasetFoundryService::versionLineage(
    const DatasetVersionId &versionId ) const
{
    using Result = sicnu::data::Result<QVector<DatasetVersionRecord>>;
    if ( !m_store || !m_store->isOpen() )
    {
        return Result::failure( Diagnostic{
            QStringLiteral( "dataset.store_closed" ),
            QStringLiteral( "dataset store is not open" ),
            DiagnosticSeverity::Error,
        } );
    }
    return m_store->versionAncestors( versionId );
}

DatasetQaReport DatasetFoundryService::runQa( const DatasetQaInputs &inputs ) const
{
    return buildDatasetQaReport( inputs );
}

SampleCatalogPage DatasetFoundryService::querySamples( const QVector<SampleCatalogRow> &rows,
                                                       const SampleCatalogFilter &filter,
                                                       qint64 offset, qint64 limit ) const
{
    return querySampleCatalog( rows, filter, offset, limit );
}

SampleCatalogSummary DatasetFoundryService::summarizeSamples(
    const QVector<SampleCatalogRow> &rows, const SampleCatalogFilter &filter ) const
{
    return summarizeSampleCatalog( rows, filter );
}

FeatureJoinResult DatasetFoundryService::joinFeatures(
    const FeatureSet &featureSet, const QVector<FeatureRow> &rows,
    const QStringList &expectedSampleIds, const QString &expectedInputVersionId ) const
{
    return joinFeaturesBySampleId( featureSet, rows, expectedSampleIds, expectedInputVersionId );
}

} // namespace sicnu::dataset
