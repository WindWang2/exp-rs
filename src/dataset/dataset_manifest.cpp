// dataset_manifest.cpp — canonical serialization + strict validation.
#include "dataset_manifest.h"

#include "dataset_ids.h"

#include <QJsonArray>
#include <QUuid>

#include <cmath>
#include <limits>

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

// --- strict decode helpers (R35-T03) ---------------------------------------
// A PRESENT field of the wrong JSON type is a typed failure. QJsonValue's
// default conversions fold wrong types into ""/0/empty — a string "entries"
// would decode as "no entries" and a negative revision as "revision 0 (not
// pinned)". Absent optionals stay absent (0 remains the legal "not pinned"
// revision).

QString fieldTypeError( const QString &key, const char *wanted )
{
    return QStringLiteral( "manifest field '%1' must be %2" ).arg( key, QString::fromUtf8( wanted ) );
}

bool strictObject( const QJsonObject &parent, const QString &key, QJsonObject *out, QString *error )
{
    const QJsonValue value = parent.value( key );
    if ( value.isUndefined() )
        return true;
    if ( !value.isObject() )
    {
        *error = fieldTypeError( key, "an object" );
        return false;
    }
    *out = value.toObject();
    return true;
}

bool strictArray( const QJsonObject &parent, const QString &key, QJsonArray *out, QString *error )
{
    const QJsonValue value = parent.value( key );
    if ( value.isUndefined() )
        return true;
    if ( !value.isArray() )
    {
        *error = fieldTypeError( key, "an array" );
        return false;
    }
    *out = value.toArray();
    return true;
}

bool strictStringArray( const QJsonObject &parent, const QString &key, QStringList *out, QString *error )
{
    QJsonArray array;
    if ( !strictArray( parent, key, &array, error ) )
        return false;
    for ( const QJsonValueRef &element : array )
    {
        if ( !element.isString() )
        {
            *error = fieldTypeError( key, "an array of strings" );
            return false;
        }
        out->append( element.toString() );
    }
    return true;
}

/// Integral ≥0 revision/version field. Absent → caller default (0 = "not
/// pinned" stays legal); JSON numbers beyond qint64 or fractional/negative
/// values are refused — QJsonValue cannot hold quint64 and a silent wrap or
/// clamp would falsify the pinned revision. 2^63 itself is refused too:
/// double(qint64max) rounds UP to 2^63, and storing it would wrap negative
/// on the qint64 encode side, breaking round-trips.
bool strictNonNegativeInteger( const QJsonObject &parent, const QString &key, quint64 *out, QString *error )
{
    const QJsonValue value = parent.value( key );
    if ( value.isUndefined() )
        return true;
    if ( !value.isDouble() )
    {
        *error = fieldTypeError( key, "an integer" );
        return false;
    }
    const double raw = value.toDouble();
    // 9223372036854775808.0 == 2^63 exactly (the double rounding of qint64 max).
    if ( std::floor( raw ) != raw || raw < 0.0 || raw >= 9223372036854775808.0 )
    {
        *error = QStringLiteral( "manifest field '%1' must be an integer >= 0" ).arg( key );
        return false;
    }
    *out = static_cast<quint64>( raw );
    return true;
}

/// Finite numeric field (resolution / extent coordinates). Absent → caller
/// default; NaN/Inf coordinates would poison every extent comparison.
bool strictFiniteDouble( const QJsonObject &parent, const QString &key, double *out, QString *error )
{
    const QJsonValue value = parent.value( key );
    if ( value.isUndefined() )
        return true;
    if ( !value.isDouble() || !std::isfinite( value.toDouble() ) )
    {
        *error = QStringLiteral( "manifest field '%1' must be a finite number" ).arg( key );
        return false;
    }
    *out = value.toDouble();
    return true;
}

/// The one stable diagnostic for every wrong-type field above.
sicnu::data::Result<DatasetManifest> manifestTypeFailure( const QString &message )
{
    return sicnu::data::Result<DatasetManifest>::failure( Diagnostic{
        QStringLiteral( "dataset.manifest_invalid" ), message, DiagnosticSeverity::Error } );
}

QJsonObject encodeSourceAsset( const SourceAssetRef &ref )
{
    QJsonObject json;
    json.insert( QStringLiteral( "asset_id" ), ref.assetId );
    json.insert( QStringLiteral( "revision" ), qint64( ref.revision ) );
    if ( !ref.role.isEmpty() )
        json.insert( QStringLiteral( "role" ), ref.role );
    if ( !ref.bandReferences.isEmpty() )
        json.insert( QStringLiteral( "bands" ),
                     QJsonArray::fromStringList( ref.bandReferences ) );
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
    if ( !strictNonNegativeInteger( json, QStringLiteral( "revision" ), &ref.revision, error ) )
        return std::nullopt;
    ref.role = json.value( QStringLiteral( "role" ) ).toString();
    if ( !strictStringArray( json, QStringLiteral( "bands" ), &ref.bandReferences, error ) )
        return std::nullopt;
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
    if ( !strictNonNegativeInteger( json, QStringLiteral( "revision" ), &entry.revision, error ) )
        return std::nullopt;
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
    if ( m_role != DatasetRole::Unspecified )
        json.insert( QStringLiteral( "role" ), datasetRoleToString( m_role ) );
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
        schema.insert( QStringLiteral( "band_roles" ),
                       QJsonArray::fromStringList( m_schema.bandRoles ) );
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

    // schema_version must be PRESENT and integral — the folded default (0)
    // used to masquerade a missing/mistyped version as "version 0".
    const QJsonValue schemaVersionValue = json.value( QStringLiteral( "schema_version" ) );
    if ( schemaVersionValue.isUndefined() )
    {
        return Result::failure( Diagnostic{
            QStringLiteral( "dataset.manifest_version" ),
            QStringLiteral( "manifest schema_version is missing" ),
            DiagnosticSeverity::Error,
        } );
    }
    if ( !schemaVersionValue.isDouble() || std::floor( schemaVersionValue.toDouble() ) != schemaVersionValue.toDouble() )
    {
        return Result::failure( Diagnostic{
            QStringLiteral( "dataset.manifest_version" ),
            QStringLiteral( "manifest schema_version must be an integer" ),
            DiagnosticSeverity::Error,
        } );
    }
    const qint64 schemaVersion = static_cast<qint64>( schemaVersionValue.toDouble() );
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
    QString manifestError;
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

    const QString roleText = json.value( QStringLiteral( "role" ) ).toString();
    if ( !roleText.isEmpty() )
    {
        const auto role = datasetRoleFromString( roleText );
        if ( !role )
        {
            return Result::failure( Diagnostic{
                QStringLiteral( "dataset.manifest_invalid" ),
                QStringLiteral( "unknown dataset role: %1" ).arg( roleText ),
                DiagnosticSeverity::Error,
            } );
        }
        manifest.m_role = *role;
    }
    manifest.m_name = json.value( QStringLiteral( "name" ) ).toString();
    manifest.m_description = json.value( QStringLiteral( "description" ) ).toString();
    const QString createdAt = json.value( QStringLiteral( "created_at_utc" ) ).toString();
    if ( !createdAt.isEmpty() )
        manifest.m_createdAtUtc = QDateTime::fromString( createdAt, Qt::ISODateWithMs );

    QJsonArray sourceAssetsJson;
    if ( !strictArray( json, QStringLiteral( "source_assets" ), &sourceAssetsJson, &manifestError ) )
        return manifestTypeFailure( manifestError );
    for ( const QJsonValueRef &value : sourceAssetsJson )
    {
        if ( !value.isObject() )
            return manifestTypeFailure( QStringLiteral( "source_assets entries must be objects" ) );
        QString error;
        const auto ref = decodeSourceAsset( value.toObject(), &error );
        if ( !ref )
        {
            return Result::failure( Diagnostic{
                QStringLiteral( "dataset.manifest_invalid" ), error, DiagnosticSeverity::Error } );
        }
        manifest.m_sourceAssets.append( *ref );
    }

    QJsonArray entriesJson;
    if ( !strictArray( json, QStringLiteral( "entries" ), &entriesJson, &manifestError ) )
        return manifestTypeFailure( manifestError );
    for ( const QJsonValueRef &value : entriesJson )
    {
        if ( !value.isObject() )
            return manifestTypeFailure( QStringLiteral( "entries elements must be objects" ) );
        QString error;
        const auto entry = decodeEntry( value.toObject(), &error );
        if ( !entry )
        {
            return Result::failure( Diagnostic{
                QStringLiteral( "dataset.manifest_invalid" ), error, DiagnosticSeverity::Error } );
        }
        manifest.m_entries.append( *entry );
    }

    QJsonObject schema;
    if ( !strictObject( json, QStringLiteral( "schema" ), &schema, &manifestError ) )
        return manifestTypeFailure( manifestError );
    manifest.m_schema.modality = schema.value( QStringLiteral( "modality" ) ).toString();
    manifest.m_schema.sensor = schema.value( QStringLiteral( "sensor" ) ).toString();
    manifest.m_schema.crs = schema.value( QStringLiteral( "crs" ) ).toString();
    if ( !strictStringArray( schema, QStringLiteral( "band_roles" ), &manifest.m_schema.bandRoles, &manifestError ) )
        return manifestTypeFailure( manifestError );
    QJsonObject resolution;
    if ( !strictObject( schema, QStringLiteral( "resolution" ), &resolution, &manifestError ) )
        return manifestTypeFailure( manifestError );
    if ( !strictFiniteDouble( resolution, QStringLiteral( "x" ), &manifest.m_schema.resolutionX, &manifestError ) ||
         !strictFiniteDouble( resolution, QStringLiteral( "y" ), &manifest.m_schema.resolutionY, &manifestError ) )
        return manifestTypeFailure( manifestError );
    manifest.m_schema.resolutionUnit = resolution.value( QStringLiteral( "unit" ) ).toString();

    QJsonObject labelSchema;
    if ( !strictObject( json, QStringLiteral( "label_schema" ), &labelSchema, &manifestError ) )
        return manifestTypeFailure( manifestError );
    if ( !labelSchema.isEmpty() )
    {
        manifest.m_labelSchema.schemaId = labelSchema.value( QStringLiteral( "schema_id" ) ).toString();
        if ( !strictNonNegativeInteger( labelSchema, QStringLiteral( "version" ), &manifest.m_labelSchema.version,
                                        &manifestError ) )
            return manifestTypeFailure( manifestError );
        if ( manifest.m_labelSchema.isNull() )
        {
            return Result::failure( Diagnostic{
                QStringLiteral( "dataset.manifest_invalid" ),
                QStringLiteral( "label_schema present without a valid schema_id" ),
                DiagnosticSeverity::Error,
            } );
        }
    }

    QJsonObject spatial;
    if ( !strictObject( json, QStringLiteral( "spatial_extent" ), &spatial, &manifestError ) )
        return manifestTypeFailure( manifestError );
    if ( !spatial.isEmpty() )
    {
        if ( !strictFiniteDouble( spatial, QStringLiteral( "min_x" ), &manifest.m_spatialExtent.minimumX, &manifestError ) ||
             !strictFiniteDouble( spatial, QStringLiteral( "min_y" ), &manifest.m_spatialExtent.minimumY, &manifestError ) ||
             !strictFiniteDouble( spatial, QStringLiteral( "max_x" ), &manifest.m_spatialExtent.maximumX, &manifestError ) ||
             !strictFiniteDouble( spatial, QStringLiteral( "max_y" ), &manifest.m_spatialExtent.maximumY, &manifestError ) )
            return manifestTypeFailure( manifestError );
        manifest.m_spatialExtent.valid = true;
    }

    QJsonObject temporal;
    if ( !strictObject( json, QStringLiteral( "temporal_extent" ), &temporal, &manifestError ) )
        return manifestTypeFailure( manifestError );
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

    if ( !strictStringArray( json, QStringLiteral( "split_manifests" ), &manifest.m_splitManifestIds, &manifestError ) ||
         !strictStringArray( json, QStringLiteral( "tags" ), &manifest.m_tags, &manifestError ) )
        return manifestTypeFailure( manifestError );
    manifest.m_license = json.value( QStringLiteral( "license" ) ).toString();
    manifest.m_citation = json.value( QStringLiteral( "citation" ) ).toString();
    if ( !strictObject( json, QStringLiteral( "provenance" ), &manifest.m_provenance, &manifestError ) ||
         !strictObject( json, QStringLiteral( "statistics" ), &manifest.m_statistics, &manifestError ) ||
         !strictObject( json, QStringLiteral( "quality" ), &manifest.m_quality, &manifestError ) )
        return manifestTypeFailure( manifestError );
    manifest.m_fingerprint = json.value( QStringLiteral( "fingerprint" ) ).toString();

    return Result::success( manifest );
}

} // namespace sicnu::dataset
