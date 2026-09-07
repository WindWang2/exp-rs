// dataset_manifest.cpp — canonical serialization + strict validation.
#include "dataset_manifest.h"

#include "dataset_ids.h"

#include <QJsonArray>
#include <QUuid>

namespace sicnu::dataset
{

using sicnu::data::Diagnostic;
using sicnu::data::DiagnosticSeverity;

namespace
{

bool isValidIdText( const QString &text )
{
    return !text.isEmpty() && !QUuid::fromString( text ).isNull();
}

QJsonObject encodeSourceAsset( const SourceAssetRef &ref )
{
    QJsonObject json;
    json.insert( QStringLiteral( "asset_id" ), ref.assetId );
    json.insert( QStringLiteral( "revision" ), qint64( ref.revision ) );
    if ( !ref.role.isEmpty() )
        json.insert( QStringLiteral( "role" ), ref.role );
    if ( !ref.bandReferences.isEmpty() )
        json.insert( QStringLiteral( "bands" ), ref.bandReferences.join( QLatin1Char( ',' ) ) );
    return json;
}

std::optional<SourceAssetRef> decodeSourceAsset( const QJsonObject &json, QString *error )
{
    SourceAssetRef ref;
    ref.assetId = json.value( QStringLiteral( "asset_id" ) ).toString();
    if ( !isValidIdText( ref.assetId ) )
    {
        *error = QStringLiteral( "source asset entry without a valid asset_id" );
        return std::nullopt;
    }
    ref.revision = quint64( qMax<qint64>( 0, json.value( QStringLiteral( "revision" ) ).toInteger() ) );
    ref.role = json.value( QStringLiteral( "role" ) ).toString();
    const QString bands = json.value( QStringLiteral( "bands" ) ).toString();
    if ( !bands.isEmpty() )
        ref.bandReferences = bands.split( QLatin1Char( ',' ), Qt::SkipEmptyParts );
    return ref;
}

QJsonObject encodeEntry( const DatasetEntry &entry )
{
    QJsonObject json;
    json.insert( QStringLiteral( "kind" ), entry.kind );
    json.insert( QStringLiteral( "ref_id" ), entry.refId );
    json.insert( QStringLiteral( "revision" ), qint64( entry.revision ) );
    if ( !entry.role.isEmpty() )
        json.insert( QStringLiteral( "role" ), entry.role );
    return json;
}

std::optional<DatasetEntry> decodeEntry( const QJsonObject &json, QString *error )
{
    DatasetEntry entry;
    entry.kind = json.value( QStringLiteral( "kind" ) ).toString();
    entry.refId = json.value( QStringLiteral( "ref_id" ) ).toString();
    if ( entry.kind != QStringLiteral( "asset" ) && entry.kind != QStringLiteral( "sample" ) )
    {
        *error = QStringLiteral( "entry kind must be 'asset' or 'sample', got '%1'" ).arg( entry.kind );
        return std::nullopt;
    }
    if ( entry.refId.isEmpty() )
    {
        *error = QStringLiteral( "entry without ref_id" );
        return std::nullopt;
    }
    entry.revision = quint64( qMax<qint64>( 0, json.value( QStringLiteral( "revision" ) ).toInteger() ) );
    entry.role = json.value( QStringLiteral( "role" ) ).toString();
    return entry;
}

} // namespace

QJsonObject DatasetManifest::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "schema_version" ), kDatasetManifestSerializationVersion );
    json.insert( QStringLiteral( "dataset_id" ), m_datasetId );
    json.insert( QStringLiteral( "version_id" ), m_versionId );
    if ( !m_parentVersionId.isEmpty() )
        json.insert( QStringLiteral( "parent_version_id" ), m_parentVersionId );
    if ( !m_name.isEmpty() )
        json.insert( QStringLiteral( "name" ), m_name );
    if ( !m_description.isEmpty() )
        json.insert( QStringLiteral( "description" ), m_description );
    if ( m_createdAtUtc.isValid() )
        json.insert( QStringLiteral( "created_at_utc" ), m_createdAtUtc.toString( Qt::ISODateWithMs ) );

    QJsonArray sourceAssets;
    for ( const SourceAssetRef &ref : m_sourceAssets )
        sourceAssets.append( encodeSourceAsset( ref ) );
    json.insert( QStringLiteral( "source_assets" ), sourceAssets );

    QJsonArray entries;
    for ( const DatasetEntry &entry : m_entries )
        entries.append( encodeEntry( entry ) );
    json.insert( QStringLiteral( "entries" ), entries );

    QJsonObject schema;
    if ( !m_schema.modality.isEmpty() )
        schema.insert( QStringLiteral( "modality" ), m_schema.modality );
    if ( !m_schema.sensor.isEmpty() )
        schema.insert( QStringLiteral( "sensor" ), m_schema.sensor );
    if ( !m_schema.crs.isEmpty() )
        schema.insert( QStringLiteral( "crs" ), m_schema.crs );
    if ( !m_schema.bandRoles.isEmpty() )
        schema.insert( QStringLiteral( "band_roles" ), m_schema.bandRoles.join( QLatin1Char( ',' ) ) );
    if ( m_schema.resolutionX != 0.0 || m_schema.resolutionY != 0.0 )
    {
        QJsonObject resolution;
        resolution.insert( QStringLiteral( "x" ), m_schema.resolutionX );
        resolution.insert( QStringLiteral( "y" ), m_schema.resolutionY );
        if ( !m_schema.resolutionUnit.isEmpty() )
            resolution.insert( QStringLiteral( "unit" ), m_schema.resolutionUnit );
        schema.insert( QStringLiteral( "resolution" ), resolution );
    }
    json.insert( QStringLiteral( "schema" ), schema );

    if ( !m_labelSchema.isNull() )
    {
        QJsonObject labelSchema;
        labelSchema.insert( QStringLiteral( "schema_id" ), m_labelSchema.schemaId );
        labelSchema.insert( QStringLiteral( "version" ), qint64( m_labelSchema.version ) );
        json.insert( QStringLiteral( "label_schema" ), labelSchema );
    }

    if ( m_spatialExtent.valid )
    {
        QJsonObject extent;
        extent.insert( QStringLiteral( "min_x" ), m_spatialExtent.minimumX );
        extent.insert( QStringLiteral( "min_y" ), m_spatialExtent.minimumY );
        extent.insert( QStringLiteral( "max_x" ), m_spatialExtent.maximumX );
        extent.insert( QStringLiteral( "max_y" ), m_spatialExtent.maximumY );
        json.insert( QStringLiteral( "spatial_extent" ), extent );
    }

    if ( m_temporalExtent.valid )
    {
        QJsonObject extent;
        extent.insert( QStringLiteral( "start_utc" ), m_temporalExtent.startUtc.toString( Qt::ISODateWithMs ) );
        extent.insert( QStringLiteral( "end_utc" ), m_temporalExtent.endUtc.toString( Qt::ISODateWithMs ) );
        json.insert( QStringLiteral( "temporal_extent" ), extent );
    }

    if ( !m_splitManifestIds.isEmpty() )
        json.insert( QStringLiteral( "split_manifests" ),
                     QJsonArray::fromStringList( m_splitManifestIds ) );
    if ( !m_tags.isEmpty() )
        json.insert( QStringLiteral( "tags" ), QJsonArray::fromStringList( m_tags ) );
    if ( !m_license.isEmpty() )
        json.insert( QStringLiteral( "license" ), m_license );
    if ( !m_citation.isEmpty() )
        json.insert( QStringLiteral( "citation" ), m_citation );
    if ( !m_provenance.isEmpty() )
        json.insert( QStringLiteral( "provenance" ), m_provenance );
    if ( !m_statistics.isEmpty() )
        json.insert( QStringLiteral( "statistics" ), m_statistics );
    if ( !m_quality.isEmpty() )
        json.insert( QStringLiteral( "quality" ), m_quality );
    if ( !m_fingerprint.isEmpty() )
        json.insert( QStringLiteral( "fingerprint" ), m_fingerprint );
    return json;
}

sicnu::data::Result<DatasetManifest> DatasetManifest::fromJson( const QJsonObject &json )
{
    using Result = sicnu::data::Result<DatasetManifest>;

    const qint64 schemaVersion = json.value( QStringLiteral( "schema_version" ) ).toInteger();
    if ( schemaVersion != kDatasetManifestSerializationVersion )
    {
        return Result::failure( Diagnostic{
            QStringLiteral( "dataset.manifest_version" ),
            QStringLiteral( "manifest schema version %1 not supported (expected %2)" )
                .arg( schemaVersion )
                .arg( kDatasetManifestSerializationVersion ),
            DiagnosticSeverity::Error,
        } );
    }

    DatasetManifest manifest;
    manifest.m_datasetId = json.value( QStringLiteral( "dataset_id" ) ).toString();
    manifest.m_versionId = json.value( QStringLiteral( "version_id" ) ).toString();
    if ( !isValidIdText( manifest.m_datasetId ) || !isValidIdText( manifest.m_versionId ) )
    {
        return Result::failure( Diagnostic{
            QStringLiteral( "dataset.manifest_invalid" ),
            QStringLiteral( "manifest requires valid dataset_id and version_id" ),
            DiagnosticSeverity::Error,
        } );
    }
    // Canonicalize ids so two spellings of one uuid cannot diverge.
    manifest.m_datasetId = QUuid::fromString( manifest.m_datasetId ).toString( QUuid::WithoutBraces );
    manifest.m_versionId = QUuid::fromString( manifest.m_versionId ).toString( QUuid::WithoutBraces );

    manifest.m_parentVersionId = json.value( QStringLiteral( "parent_version_id" ) ).toString();
    if ( !manifest.m_parentVersionId.isEmpty() )
    {
        if ( !isValidIdText( manifest.m_parentVersionId ) )
        {
            return Result::failure( Diagnostic{
                QStringLiteral( "dataset.manifest_invalid" ),
                QStringLiteral( "parent_version_id present but not a valid id" ),
                DiagnosticSeverity::Error,
            } );
        }
        manifest.m_parentVersionId =
            QUuid::fromString( manifest.m_parentVersionId ).toString( QUuid::WithoutBraces );
    }

    manifest.m_name = json.value( QStringLiteral( "name" ) ).toString();
    manifest.m_description = json.value( QStringLiteral( "description" ) ).toString();
    const QString createdAt = json.value( QStringLiteral( "created_at_utc" ) ).toString();
    if ( !createdAt.isEmpty() )
        manifest.m_createdAtUtc = QDateTime::fromString( createdAt, Qt::ISODateWithMs );

    for ( const QJsonValue &value : json.value( QStringLiteral( "source_assets" ) ).toArray() )
    {
        QString error;
        const auto ref = decodeSourceAsset( value.toObject(), &error );
        if ( !ref )
        {
            return Result::failure( Diagnostic{
                QStringLiteral( "dataset.manifest_invalid" ), error, DiagnosticSeverity::Error } );
        }
        manifest.m_sourceAssets.append( *ref );
    }

    for ( const QJsonValue &value : json.value( QStringLiteral( "entries" ) ).toArray() )
    {
        QString error;
        const auto entry = decodeEntry( value.toObject(), &error );
        if ( !entry )
        {
            return Result::failure( Diagnostic{
                QStringLiteral( "dataset.manifest_invalid" ), error, DiagnosticSeverity::Error } );
        }
        manifest.m_entries.append( *entry );
    }

    const QJsonObject schema = json.value( QStringLiteral( "schema" ) ).toObject();
    manifest.m_schema.modality = schema.value( QStringLiteral( "modality" ) ).toString();
    manifest.m_schema.sensor = schema.value( QStringLiteral( "sensor" ) ).toString();
    manifest.m_schema.crs = schema.value( QStringLiteral( "crs" ) ).toString();
    const QString bandRoles = schema.value( QStringLiteral( "band_roles" ) ).toString();
    if ( !bandRoles.isEmpty() )
        manifest.m_schema.bandRoles = bandRoles.split( QLatin1Char( ',' ), Qt::SkipEmptyParts );
    const QJsonObject resolution = schema.value( QStringLiteral( "resolution" ) ).toObject();
    manifest.m_schema.resolutionX = resolution.value( QStringLiteral( "x" ) ).toDouble();
    manifest.m_schema.resolutionY = resolution.value( QStringLiteral( "y" ) ).toDouble();
    manifest.m_schema.resolutionUnit = resolution.value( QStringLiteral( "unit" ) ).toString();

    const QJsonObject labelSchema = json.value( QStringLiteral( "label_schema" ) ).toObject();
    if ( !labelSchema.isEmpty() )
    {
        manifest.m_labelSchema.schemaId = labelSchema.value( QStringLiteral( "schema_id" ) ).toString();
        manifest.m_labelSchema.version =
            quint64( qMax<qint64>( 0, labelSchema.value( QStringLiteral( "version" ) ).toInteger() ) );
        if ( manifest.m_labelSchema.isNull() )
        {
            return Result::failure( Diagnostic{
                QStringLiteral( "dataset.manifest_invalid" ),
                QStringLiteral( "label_schema present without a valid schema_id" ),
                DiagnosticSeverity::Error,
            } );
        }
    }

    const QJsonObject spatial = json.value( QStringLiteral( "spatial_extent" ) ).toObject();
    if ( !spatial.isEmpty() )
    {
        manifest.m_spatialExtent.minimumX = spatial.value( QStringLiteral( "min_x" ) ).toDouble();
        manifest.m_spatialExtent.minimumY = spatial.value( QStringLiteral( "min_y" ) ).toDouble();
        manifest.m_spatialExtent.maximumX = spatial.value( QStringLiteral( "max_x" ) ).toDouble();
        manifest.m_spatialExtent.maximumY = spatial.value( QStringLiteral( "max_y" ) ).toDouble();
        manifest.m_spatialExtent.valid = true;
    }

    const QJsonObject temporal = json.value( QStringLiteral( "temporal_extent" ) ).toObject();
    if ( !temporal.isEmpty() )
    {
        manifest.m_temporalExtent.startUtc =
            QDateTime::fromString( temporal.value( QStringLiteral( "start_utc" ) ).toString(), Qt::ISODateWithMs );
        manifest.m_temporalExtent.endUtc =
            QDateTime::fromString( temporal.value( QStringLiteral( "end_utc" ) ).toString(), Qt::ISODateWithMs );
        manifest.m_temporalExtent.valid =
            manifest.m_temporalExtent.startUtc.isValid() && manifest.m_temporalExtent.endUtc.isValid();
        if ( !manifest.m_temporalExtent.valid )
        {
            return Result::failure( Diagnostic{
                QStringLiteral( "dataset.manifest_invalid" ),
                QStringLiteral( "temporal_extent present but not parseable" ),
                DiagnosticSeverity::Error,
            } );
        }
    }

    manifest.m_splitManifestIds =
        json.value( QStringLiteral( "split_manifests" ) ).toVariant().toStringList();
    manifest.m_tags = json.value( QStringLiteral( "tags" ) ).toVariant().toStringList();
    manifest.m_license = json.value( QStringLiteral( "license" ) ).toString();
    manifest.m_citation = json.value( QStringLiteral( "citation" ) ).toString();
    manifest.m_provenance = json.value( QStringLiteral( "provenance" ) ).toObject();
    manifest.m_statistics = json.value( QStringLiteral( "statistics" ) ).toObject();
    manifest.m_quality = json.value( QStringLiteral( "quality" ) ).toObject();
    manifest.m_fingerprint = json.value( QStringLiteral( "fingerprint" ) ).toString();

    return Result::success( manifest );
}

} // namespace sicnu::dataset
