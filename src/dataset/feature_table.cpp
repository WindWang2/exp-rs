// feature_table.cpp — FeatureSet schema + identity join.
#include "feature_table.h"

#include "../data/execution_fingerprint.h"

#include <QCryptographicHash>
#include <QHash>
#include <QJsonArray>
#include <QSet>

namespace sicnu::dataset
{

QString featureJoinStatusToString( FeatureJoinStatus status )
{
    switch ( status )
    {
        case FeatureJoinStatus::Ok:
            return QStringLiteral( "ok" );
        case FeatureJoinStatus::MissingKey:
            return QStringLiteral( "missing_key" );
        case FeatureJoinStatus::AmbiguousKey:
            return QStringLiteral( "ambiguous_key" );
        case FeatureJoinStatus::ExtraColumns:
            return QStringLiteral( "extra_columns" );
        case FeatureJoinStatus::MissingRequiredColumn:
            return QStringLiteral( "missing_required_column" );
        case FeatureJoinStatus::StaleInputVersion:
            return QStringLiteral( "stale_input_version" );
    }
    return QStringLiteral( "unknown" );
}

QJsonObject FeatureColumn::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "name" ), name );
    if ( !dtype.isEmpty() )
        json.insert( QStringLiteral( "dtype" ), dtype );
    if ( !unit.isEmpty() )
        json.insert( QStringLiteral( "unit" ), unit );
    if ( !domain.isEmpty() )
        json.insert( QStringLiteral( "domain" ), domain );
    json.insert( QStringLiteral( "required" ), required );
    return json;
}

sicnu::data::Result<FeatureColumn> FeatureColumn::fromJson( const QJsonObject &json )
{
    using Result = sicnu::data::Result<FeatureColumn>;
    FeatureColumn column;
    column.name = json.value( QStringLiteral( "name" ) ).toString();
    if ( column.name.isEmpty() )
    {
        return Result::failure( Diagnostic{
            QStringLiteral( "dataset.feature_column_invalid" ),
            QStringLiteral( "feature column requires a non-empty name" ),
            DiagnosticSeverity::Error,
        } );
    }
    column.dtype = json.value( QStringLiteral( "dtype" ) ).toString();
    column.unit = json.value( QStringLiteral( "unit" ) ).toString();
    column.domain = json.value( QStringLiteral( "domain" ) ).toString();
    column.required = json.value( QStringLiteral( "required" ) ).toBool( true );
    return Result::success( column );
}

sicnu::data::Result<void> FeatureSet::validate() const
{
    using Result = sicnu::data::Result<void>;
    if ( m_featureSetId.isEmpty() )
    {
        return Result::failure( Diagnostic{
            QStringLiteral( "dataset.feature_set_invalid" ),
            QStringLiteral( "feature_set_id is required" ),
            DiagnosticSeverity::Error,
        } );
    }
    if ( m_schemaVersion == 0 )
    {
        return Result::failure( Diagnostic{
            QStringLiteral( "dataset.feature_set_invalid" ),
            QStringLiteral( "schema_version must be >= 1" ),
            DiagnosticSeverity::Error,
        } );
    }
    if ( m_sampleKey.isEmpty() )
    {
        return Result::failure( Diagnostic{
            QStringLiteral( "dataset.feature_set_invalid" ),
            QStringLiteral( "sample_key is required" ),
            DiagnosticSeverity::Error,
        } );
    }
    QSet<QString> names;
    for ( const FeatureColumn &column : m_columns )
    {
        if ( column.name.isEmpty() )
        {
            return Result::failure( Diagnostic{
                QStringLiteral( "dataset.feature_set_invalid" ),
                QStringLiteral( "feature columns must have names" ),
                DiagnosticSeverity::Error,
            } );
        }
        if ( names.contains( column.name ) )
        {
            return Result::failure( Diagnostic{
                QStringLiteral( "dataset.feature_set_invalid" ),
                QStringLiteral( "duplicate feature column: %1" ).arg( column.name ),
                DiagnosticSeverity::Error,
            } );
        }
        names.insert( column.name );
    }
    return Result::success();
}

QJsonObject FeatureSet::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "schema_version" ), kFeatureSetSerializationVersion );
    json.insert( QStringLiteral( "feature_set_id" ), m_featureSetId );
    json.insert( QStringLiteral( "feature_schema_version" ), qint64( m_schemaVersion ) );
    json.insert( QStringLiteral( "sample_key" ), m_sampleKey );
    QJsonArray columns;
    for ( const FeatureColumn &column : m_columns )
        columns.append( column.toJson() );
    json.insert( QStringLiteral( "columns" ), columns );
    if ( !m_producer.isEmpty() )
        json.insert( QStringLiteral( "producer" ), m_producer );
    if ( !m_inputDatasetVersionId.isEmpty() )
        json.insert( QStringLiteral( "input_dataset_version_id" ), m_inputDatasetVersionId );
    if ( !m_digest.isEmpty() )
        json.insert( QStringLiteral( "digest" ), m_digest );
    if ( !m_metadata.isEmpty() )
        json.insert( QStringLiteral( "metadata" ), m_metadata );
    return json;
}

sicnu::data::Result<FeatureSet> FeatureSet::fromJson( const QJsonObject &json )
{
    using Result = sicnu::data::Result<FeatureSet>;
    const qint64 schemaVersion = json.value( QStringLiteral( "schema_version" ) ).toInteger();
    if ( schemaVersion != kFeatureSetSerializationVersion )
    {
        return Result::failure( Diagnostic{
            QStringLiteral( "dataset.feature_set_version" ),
            QStringLiteral( "feature set schema version %1 not supported" ).arg( schemaVersion ),
            DiagnosticSeverity::Error,
        } );
    }
    FeatureSet set;
    set.m_featureSetId = json.value( QStringLiteral( "feature_set_id" ) ).toString();
    set.m_schemaVersion =
        quint64( qMax<qint64>( 0, json.value( QStringLiteral( "feature_schema_version" ) ).toInteger( 1 ) ) );
    set.m_sampleKey = json.value( QStringLiteral( "sample_key" ) ).toString( QStringLiteral( "sample_id" ) );
    set.m_producer = json.value( QStringLiteral( "producer" ) ).toString();
    set.m_inputDatasetVersionId = json.value( QStringLiteral( "input_dataset_version_id" ) ).toString();
    set.m_digest = json.value( QStringLiteral( "digest" ) ).toString();
    set.m_metadata = json.value( QStringLiteral( "metadata" ) ).toObject();
    for ( const QJsonValue &value : json.value( QStringLiteral( "columns" ) ).toArray() )
    {
        const auto column = FeatureColumn::fromJson( value.toObject() );
        if ( !column )
            return Result::failure( column.diagnostics() );
        set.m_columns.append( *column );
    }
    const auto validated = set.validate();
    if ( !validated )
        return Result::failure( validated.diagnostics() );
    return Result::success( set );
}

QString FeatureSet::schemaDigest( bool excludeMetadata ) const
{
    QJsonObject json = toJson();
    if ( excludeMetadata )
        json.remove( QStringLiteral( "metadata" ) );
    json.remove( QStringLiteral( "digest" ) );
    const QByteArray canonical = sicnu::data::canonicalizeJsonRfc8785( json );
    return QString::fromUtf8(
        QCryptographicHash::hash( canonical, QCryptographicHash::Sha256 ).toHex() );
}

FeatureJoinResult joinFeaturesBySampleId( const FeatureSet &featureSet,
                                          const QVector<FeatureRow> &rows,
                                          const QStringList &expectedSampleIds,
                                          const QString &expectedInputVersionId,
                                          qint64 maxFindings )
{
    FeatureJoinResult result;
    const auto schemaOk = featureSet.validate();
    if ( !schemaOk )
    {
        result.verdict = AuditVerdict::Fail;
        FeatureJoinFinding finding;
        finding.status = FeatureJoinStatus::MissingRequiredColumn;
        finding.detail = schemaOk.diagnostics().isEmpty()
                             ? QStringLiteral( "feature set invalid" )
                             : schemaOk.diagnostics().first().message;
        result.findings.append( finding );
        return result;
    }

    if ( !expectedInputVersionId.isEmpty() &&
         featureSet.inputDatasetVersionId() != expectedInputVersionId )
    {
        result.verdict = AuditVerdict::Fail;
        FeatureJoinFinding finding;
        finding.status = FeatureJoinStatus::StaleInputVersion;
        finding.detail = QStringLiteral(
            "feature set input_dataset_version_id %1 != expected %2" )
                             .arg( featureSet.inputDatasetVersionId(), expectedInputVersionId );
        finding.evidence.insert( QStringLiteral( "feature_set_id" ), featureSet.featureSetId() );
        result.findings.append( finding );
        return result;
    }

    QHash<QString, QVector<int>> index;
    index.reserve( rows.size() );
    for ( int i = 0; i < rows.size(); ++i )
        index[rows.at( i ).sampleId].append( i );

    QSet<QString> required;
    for ( const FeatureColumn &column : featureSet.columns() )
    {
        if ( column.required )
            required.insert( column.name );
    }

    auto pushFinding = [&]( FeatureJoinFinding finding ) {
        if ( result.findings.size() < maxFindings )
            result.findings.append( std::move( finding ) );
    };

    for ( const QString &sampleId : expectedSampleIds )
    {
        const auto it = index.constFind( sampleId );
        if ( it == index.constEnd() || it->isEmpty() )
        {
            ++result.missing;
            FeatureJoinFinding finding;
            finding.status = FeatureJoinStatus::MissingKey;
            finding.sampleId = sampleId;
            finding.detail = QStringLiteral( "no feature row for sample" );
            pushFinding( std::move( finding ) );
            continue;
        }
        if ( it->size() > 1 )
        {
            ++result.ambiguous;
            FeatureJoinFinding finding;
            finding.status = FeatureJoinStatus::AmbiguousKey;
            finding.sampleId = sampleId;
            finding.detail = QStringLiteral( "duplicate feature rows for sample (%1)" )
                                 .arg( it->size() );
            finding.evidence.insert( QStringLiteral( "row_count" ), it->size() );
            pushFinding( std::move( finding ) );
            continue;
        }

        const FeatureRow &row = rows.at( it->first() );
        bool missingRequired = false;
        for ( const QString &name : required )
        {
            if ( !row.values.contains( name ) )
            {
                missingRequired = true;
                FeatureJoinFinding finding;
                finding.status = FeatureJoinStatus::MissingRequiredColumn;
                finding.sampleId = sampleId;
                finding.detail = QStringLiteral( "missing required column: %1" ).arg( name );
                pushFinding( std::move( finding ) );
                break;
            }
        }
        if ( missingRequired )
            continue;

        result.joined.append( row );
        ++result.matched;
    }

    if ( result.ambiguous > 0 )
        result.verdict = AuditVerdict::Fail;
    else if ( result.missing > 0 )
        result.verdict = AuditVerdict::Warn;
    else if ( result.matched == expectedSampleIds.size() )
        result.verdict = AuditVerdict::Pass;
    else
        result.verdict = AuditVerdict::Unknown;

    return result;
}

} // namespace sicnu::dataset
