// benchmark_definition.cpp
#include "benchmark_definition.h"

#include "../data/execution_fingerprint.h"

#include <QCryptographicHash>
#include <QJsonArray>

namespace sicnu::experiment
{

QJsonObject MetricResult::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "name" ), name );
    json.insert( QStringLiteral( "definition_version" ), definitionVersion );
    json.insert( QStringLiteral( "value" ), value );
    if ( !scope.isEmpty() )
        json.insert( QStringLiteral( "scope" ), scope );
    if ( !classCode.isEmpty() )
        json.insert( QStringLiteral( "class" ), classCode );
    if ( support >= 0 )
        json.insert( QStringLiteral( "support" ), support );
    if ( !warnings.isEmpty() )
        json.insert( QStringLiteral( "warnings" ), QJsonArray::fromStringList( warnings ) );
    return json;
}

MetricResult MetricResult::fromJson( const QJsonObject &json )
{
    MetricResult metric;
    metric.name = json.value( QStringLiteral( "name" ) ).toString();
    metric.definitionVersion = json.value( QStringLiteral( "definition_version" ) ).toString();
    metric.value = json.value( QStringLiteral( "value" ) ).toDouble();
    metric.scope = json.value( QStringLiteral( "scope" ) ).toString();
    metric.classCode = json.value( QStringLiteral( "class" ) ).toString();
    metric.support = json.contains( QStringLiteral( "support" ) )
                         ? json.value( QStringLiteral( "support" ) ).toInteger( -1 )
                         : -1;
    const QJsonArray warnings = json.value( QStringLiteral( "warnings" ) ).toArray();
    for ( const QJsonValue &warning : warnings )
        metric.warnings.append( warning.toString() );
    return metric;
}

sicnu::data::Result<void> BenchmarkDefinition::validate() const
{
    using Result = sicnu::data::Result<void>;
    if ( m_benchmarkId.isEmpty() )
    {
        return Result::failure( Diagnostic{
            QStringLiteral( "experiment.benchmark_invalid" ),
            QStringLiteral( "benchmark_id is required" ),
            DiagnosticSeverity::Error,
        } );
    }
    if ( m_benchmarkVersion == 0 )
    {
        return Result::failure( Diagnostic{
            QStringLiteral( "experiment.benchmark_invalid" ),
            QStringLiteral( "benchmark_version must be >= 1" ),
            DiagnosticSeverity::Error,
        } );
    }
    if ( m_datasetVersionId.isEmpty() || m_splitManifestId.isEmpty() )
    {
        return Result::failure( Diagnostic{
            QStringLiteral( "experiment.benchmark_invalid" ),
            QStringLiteral( "dataset_version_id and split_manifest_id are required" ),
            DiagnosticSeverity::Error,
        } );
    }
    if ( m_metricNames.isEmpty() )
    {
        return Result::failure( Diagnostic{
            QStringLiteral( "experiment.benchmark_invalid" ),
            QStringLiteral( "at least one metric name is required" ),
            DiagnosticSeverity::Error,
        } );
    }
    if ( m_determinism != sicnu::dataset::DeterminismGrade::Strict &&
         m_determinismNote.isEmpty() )
    {
        return Result::failure( Diagnostic{
            QStringLiteral( "experiment.benchmark_invalid" ),
            QStringLiteral( "determinism_note required when determinism != strict" ),
            DiagnosticSeverity::Error,
        } );
    }

    // Keep protocol pins aligned with definition pins.
    if ( !m_protocol.datasetVersionId().isEmpty() &&
         m_protocol.datasetVersionId() != m_datasetVersionId )
    {
        return Result::failure( Diagnostic{
            QStringLiteral( "experiment.benchmark_protocol_mismatch" ),
            QStringLiteral( "protocol.dataset_version_id disagrees with definition pin" ),
            DiagnosticSeverity::Error,
        } );
    }
    if ( !m_protocol.splitManifestId().isEmpty() &&
         m_protocol.splitManifestId() != m_splitManifestId )
    {
        return Result::failure( Diagnostic{
            QStringLiteral( "experiment.benchmark_protocol_mismatch" ),
            QStringLiteral( "protocol.split_manifest_id disagrees with definition pin" ),
            DiagnosticSeverity::Error,
        } );
    }

    // Protocol pins may be filled by the caller or left empty when the
    // definition pins are authoritative; only validate a fully-pinned protocol.
    if ( !m_protocol.datasetVersionId().isEmpty() && !m_protocol.splitManifestId().isEmpty() )
    {
        const auto protocolOk = m_protocol.validate();
        if ( !protocolOk )
            return Result::failure( protocolOk.diagnostics() );
    }
    return Result::success();
}

QString BenchmarkDefinition::contentDigest() const
{
    QJsonObject json = toJson();
    json.remove( QStringLiteral( "metadata" ) );
    const QByteArray canonical = sicnu::data::canonicalizeJsonRfc8785( json );
    return QString::fromUtf8(
        QCryptographicHash::hash( canonical, QCryptographicHash::Sha256 ).toHex() );
}

QJsonObject BenchmarkDefinition::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "schema_version" ), kBenchmarkDefinitionSerializationVersion );
    json.insert( QStringLiteral( "benchmark_id" ), m_benchmarkId );
    json.insert( QStringLiteral( "benchmark_version" ), qint64( m_benchmarkVersion ) );
    if ( !m_name.isEmpty() )
        json.insert( QStringLiteral( "name" ), m_name );
    if ( !m_description.isEmpty() )
        json.insert( QStringLiteral( "description" ), m_description );
    json.insert( QStringLiteral( "task_family" ),
                 sicnu::dataset::benchmarkTaskFamilyToString( m_taskFamily ) );
    json.insert( QStringLiteral( "dataset_version_id" ), m_datasetVersionId );
    json.insert( QStringLiteral( "split_manifest_id" ), m_splitManifestId );
    if ( !m_labelSchemaId.isEmpty() )
    {
        json.insert( QStringLiteral( "label_schema_id" ), m_labelSchemaId );
        json.insert( QStringLiteral( "label_schema_version" ), qint64( m_labelSchemaVersion ) );
    }
    json.insert( QStringLiteral( "metric_names" ), QJsonArray::fromStringList( m_metricNames ) );
    json.insert( QStringLiteral( "protocol" ), m_protocol.toJson() );
    json.insert( QStringLiteral( "refuse_pseudo_labels_in_test" ), m_refusePseudoLabelsInTest );
    if ( !m_allowedPreprocessing.isEmpty() )
        json.insert( QStringLiteral( "allowed_preprocessing" ),
                     QJsonArray::fromStringList( m_allowedPreprocessing ) );
    if ( !m_forbiddenLeakage.isEmpty() )
        json.insert( QStringLiteral( "forbidden_leakage" ),
                     QJsonArray::fromStringList( m_forbiddenLeakage ) );
    json.insert( QStringLiteral( "determinism" ),
                 sicnu::dataset::determinismGradeToString( m_determinism ) );
    if ( !m_determinismNote.isEmpty() )
        json.insert( QStringLiteral( "determinism_note" ), m_determinismNote );
    json.insert( QStringLiteral( "seed_policy" ), qint64( m_seedPolicy ) );
    if ( !m_requiredEnvironmentPins.isEmpty() )
        json.insert( QStringLiteral( "required_environment_pins" ),
                     QJsonArray::fromStringList( m_requiredEnvironmentPins ) );
    if ( !m_metadata.isEmpty() )
        json.insert( QStringLiteral( "metadata" ), m_metadata );
    return json;
}

Result<BenchmarkDefinition> BenchmarkDefinition::fromJson( const QJsonObject &json )
{
    const qint64 schemaVersion = json.value( QStringLiteral( "schema_version" ) ).toInteger();
    if ( schemaVersion != kBenchmarkDefinitionSerializationVersion )
    {
        return Result<BenchmarkDefinition>::failure( Diagnostic{
            QStringLiteral( "experiment.benchmark_version" ),
            QStringLiteral( "benchmark schema version %1 not supported" ).arg( schemaVersion ),
            DiagnosticSeverity::Error,
        } );
    }

    BenchmarkDefinition def;
    def.m_benchmarkId = json.value( QStringLiteral( "benchmark_id" ) ).toString();
    def.m_benchmarkVersion =
        quint64( qMax<qint64>( 0, json.value( QStringLiteral( "benchmark_version" ) ).toInteger( 1 ) ) );
    def.m_name = json.value( QStringLiteral( "name" ) ).toString();
    def.m_description = json.value( QStringLiteral( "description" ) ).toString();

    const QString taskText = json.value( QStringLiteral( "task_family" ) ).toString();
    const auto task = sicnu::dataset::benchmarkTaskFamilyFromString( taskText );
    if ( !task )
    {
        return Result<BenchmarkDefinition>::failure( Diagnostic{
            QStringLiteral( "experiment.benchmark_invalid" ),
            QStringLiteral( "unknown task_family: %1" ).arg( taskText ),
            DiagnosticSeverity::Error,
        } );
    }
    def.m_taskFamily = *task;

    def.m_datasetVersionId = json.value( QStringLiteral( "dataset_version_id" ) ).toString();
    def.m_splitManifestId = json.value( QStringLiteral( "split_manifest_id" ) ).toString();
    def.m_labelSchemaId = json.value( QStringLiteral( "label_schema_id" ) ).toString();
    def.m_labelSchemaVersion =
        quint64( qMax<qint64>( 0, json.value( QStringLiteral( "label_schema_version" ) ).toInteger() ) );
    def.m_metricNames = json.value( QStringLiteral( "metric_names" ) ).toVariant().toStringList();

    const auto protocol = EvaluationProtocol::fromJson( json.value( QStringLiteral( "protocol" ) ).toObject() );
    if ( !protocol )
        return Result<BenchmarkDefinition>::failure( protocol.diagnostics() );
    def.m_protocol = *protocol;
    if ( def.m_protocol.datasetVersionId().isEmpty() )
        def.m_protocol.setDatasetVersionId( def.m_datasetVersionId );
    if ( def.m_protocol.splitManifestId().isEmpty() )
        def.m_protocol.setSplitManifestId( def.m_splitManifestId );

    def.m_refusePseudoLabelsInTest =
        json.value( QStringLiteral( "refuse_pseudo_labels_in_test" ) ).toBool( true );
    def.m_allowedPreprocessing =
        json.value( QStringLiteral( "allowed_preprocessing" ) ).toVariant().toStringList();
    def.m_forbiddenLeakage =
        json.value( QStringLiteral( "forbidden_leakage" ) ).toVariant().toStringList();

    const QString detText = json.value( QStringLiteral( "determinism" ) )
                                .toString( QStringLiteral( "strict" ) );
    const auto det = sicnu::dataset::determinismGradeFromString( detText );
    if ( !det )
    {
        return Result<BenchmarkDefinition>::failure( Diagnostic{
            QStringLiteral( "experiment.benchmark_invalid" ),
            QStringLiteral( "unknown determinism: %1" ).arg( detText ),
            DiagnosticSeverity::Error,
        } );
    }
    def.m_determinism = *det;
    def.m_determinismNote = json.value( QStringLiteral( "determinism_note" ) ).toString();
    def.m_seedPolicy =
        quint64( qMax<qint64>( 0, json.value( QStringLiteral( "seed_policy" ) ).toInteger() ) );
    def.m_requiredEnvironmentPins =
        json.value( QStringLiteral( "required_environment_pins" ) ).toVariant().toStringList();
    def.m_metadata = json.value( QStringLiteral( "metadata" ) ).toObject();

    const auto validated = def.validate();
    if ( !validated )
        return Result<BenchmarkDefinition>::failure( validated.diagnostics() );
    return Result<BenchmarkDefinition>::success( def );
}

} // namespace sicnu::experiment
