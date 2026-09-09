// cli_dataset_commands.cpp — dataset/experiment/reproduce CLI groups.
#include "cli_dataset_commands.h"

#include "cli_commands.h"

#include "dataset/dataset_manifest.h"
#include "dataset/dataset_store.h"
#include "dataset/label_schema.h"
#include "dataset/leakage_audit.h"
#include "dataset/sample.h"
#include "dataset/dataset_types.h"
#include "dataset/dataset_version.h"
#include "dataset/split.h"
#include "experiment/experiment_store.h"
#include "experiment/experiment_types.h"
#include "experiment/replay_readiness.h"
#include "experiment/reproduction_bundle.h"

#include <json/json.h>

#include <QFileInfo>
#include <QJsonDocument>

namespace sicnu::cli
{

namespace
{

struct CommonOptions
{
    QString datasetDb;
    QString experimentDb;
    QString name;
    QString description;
    QString objective;
    QString datasetId;
    QString versionFrom;
    QString versionTo;
    QString versionId;
    QString experimentId;
    QString runA;
    QString runB;
    QString runId;
    QString outputDir;
    QString bundleDir;
    QString schemaId;
    QString schemaVersion;
    QString splitManifestId;
    QString mode;
    qint64 limit = 0;
    qint64 cursor = 0;
};

CommonOptions parseOptions( QStringList args )
{
    CommonOptions options;
    static const QStringList kValueFlags = {
        QStringLiteral( "--dataset-db" ), QStringLiteral( "--experiment-db" ),
        QStringLiteral( "--name" ),       QStringLiteral( "--description" ),
        QStringLiteral( "--objective" ),  QStringLiteral( "--dataset" ),
        QStringLiteral( "--version" ),    QStringLiteral( "--from" ),
        QStringLiteral( "--to" ),         QStringLiteral( "--experiment" ),
        QStringLiteral( "--a" ),          QStringLiteral( "--b" ),
        QStringLiteral( "--run" ),        QStringLiteral( "--out" ),
        QStringLiteral( "--bundle" ),     QStringLiteral( "--schema" ),
        QStringLiteral( "--schema-version" ), QStringLiteral( "--split" ),
        QStringLiteral( "--mode" ),       QStringLiteral( "--limit" ),
        QStringLiteral( "--cursor" ),
    };
    while ( !args.isEmpty() )
    {
        const QString flag = args.takeFirst();
        // Only known flags consume a value; global flags (--json, --quiet…)
        // and unknown tokens are skipped so flag order never matters.
        if ( !kValueFlags.contains( flag ) )
            continue;
        if ( args.isEmpty() )
            break;
        const QString value = args.takeFirst();
        if ( flag == QLatin1String( "--dataset-db" ) )
            options.datasetDb = value;
        else if ( flag == QLatin1String( "--experiment-db" ) )
            options.experimentDb = value;
        else if ( flag == QLatin1String( "--name" ) )
            options.name = value;
        else if ( flag == QLatin1String( "--description" ) )
            options.description = value;
        else if ( flag == QLatin1String( "--objective" ) )
            options.objective = value;
        else if ( flag == QLatin1String( "--dataset" ) )
            options.datasetId = value;
        else if ( flag == QLatin1String( "--version" ) )
            options.versionId = value;
        else if ( flag == QLatin1String( "--from" ) )
            options.versionFrom = value;
        else if ( flag == QLatin1String( "--to" ) )
            options.versionTo = value;
        else if ( flag == QLatin1String( "--experiment" ) )
            options.experimentId = value;
        else if ( flag == QLatin1String( "--a" ) )
            options.runA = value;
        else if ( flag == QLatin1String( "--b" ) )
            options.runB = value;
        else if ( flag == QLatin1String( "--run" ) )
            options.runId = value;
        else if ( flag == QLatin1String( "--out" ) )
            options.outputDir = value;
        else if ( flag == QLatin1String( "--bundle" ) )
            options.bundleDir = value;
        else if ( flag == QLatin1String( "--schema" ) )
            options.schemaId = value;
        else if ( flag == QLatin1String( "--schema-version" ) )
            options.schemaVersion = value;
        else if ( flag == QLatin1String( "--split" ) )
            options.splitManifestId = value;
        else if ( flag == QLatin1String( "--mode" ) )
            options.mode = value;
        else if ( flag == QLatin1String( "--limit" ) )
            options.limit = value.toLongLong();
        else if ( flag == QLatin1String( "--cursor" ) )
            options.cursor = value.toLongLong();
    }
    return options;
}

Json::Value toJsonValue( const QJsonObject &json )
{
    // Bridge QJson → jsoncpp by re-parsing the serialized text: both sides
    // are plain JSON documents, and the CLI envelope is jsoncpp-native.
    Json::Value parsed;
    Json::Reader reader;
    reader.parse( QString::fromUtf8( QJsonDocument( json ).toJson( QJsonDocument::Compact ) )
                      .toStdString(),
                  parsed );
    return parsed;
}

Json::Value diagnosticsToJson( const QVector<sicnu::data::Diagnostic> &diagnostics )
{
    Json::Value array( Json::arrayValue );
    for ( const auto &diagnostic : diagnostics )
    {
        Json::Value item( Json::objectValue );
        item["code"] = diagnostic.code.toStdString();
        item["message"] = diagnostic.message.toStdString();
        item["severity"] = diagnostic.severity == sicnu::data::DiagnosticSeverity::Error
                               ? "error"
                               : ( diagnostic.severity == sicnu::data::DiagnosticSeverity::Warning
                                       ? "warning"
                                       : "info" );
        array.append( item );
    }
    return array;
}

int fail( const CliIO &io, const std::string &command, const std::string &message,
          const QVector<sicnu::data::Diagnostic> &diagnostics = {} )
{
    return io.finish( false, command, {}, 1, diagnosticsToJson( diagnostics ), message );
}

std::string versionIdStdString( const QString &text )
{
    return text.toStdString();
}

int datasetSubcommand( const QString &sub, QStringList args, const CliIO &io )
{
    const CommonOptions options = parseOptions( args );
    if ( options.datasetDb.isEmpty() )
        return fail( io, "dataset", "--dataset-db <path> is required" );

    sicnu::dataset::DatasetStore store;
    QString error;
    if ( !store.open( options.datasetDb, &error ) )
        return fail( io, "dataset", error.toStdString() );

    if ( sub == QLatin1String( "create" ) )
    {
        if ( options.name.isEmpty() )
            return fail( io, "dataset", "--name is required" );
        const auto created = store.createDataset( sicnu::dataset::DatasetId::generate(),
                                                  options.name, options.description );
        if ( !created )
            return fail( io, "dataset", "create failed", created.diagnostics() );
        Json::Value data( Json::objectValue );
        data["dataset_id"] = created.value().toStdString();
        return io.finish( true, "dataset", data, 0 );
    }

    if ( sub == QLatin1String( "inspect" ) )
    {
        Json::Value data( Json::objectValue );
        if ( !options.versionId.isEmpty() )
        {
            const auto version = store.versionById(
                sicnu::dataset::DatasetVersionId::fromString( options.versionId )
                    .value_or( sicnu::dataset::DatasetVersionId{} ) );
            if ( !version )
                return fail( io, "dataset", "version not found" );
            data["version"] = toJsonValue(
                QJsonDocument::fromJson( version->manifestJson().toUtf8() ).object() );
            data["status"] =
                sicnu::dataset::datasetVersionStatusToString( version->status() ).toStdString();
            data["fingerprint"] = version->fingerprint().toStdString();
            data["sample_count"] = static_cast<Json::Int64>( store.sampleCount(
                sicnu::dataset::DatasetVersionId::fromString( options.versionId )
                    .value_or( sicnu::dataset::DatasetVersionId{} ) ) );
        }
        else if ( !options.datasetId.isEmpty() )
        {
            const auto record = store.datasetById(
                sicnu::dataset::DatasetId::fromString( options.datasetId )
                    .value_or( sicnu::dataset::DatasetId{} ) );
            if ( !record )
                return fail( io, "dataset", "dataset not found" );
            data["dataset"] = toJsonValue( QJsonObject::fromVariantMap( record.value() ) );
            Json::Value versions( Json::arrayValue );
            for ( const auto &entry : store.versionsOfDataset(
                      sicnu::dataset::DatasetId::fromString( options.datasetId )
                          .value_or( sicnu::dataset::DatasetId{} ) ) )
            {
                Json::Value item( Json::objectValue );
                item["version_id"] = entry.versionId().toStdString();
                item["status"] =
                    sicnu::dataset::datasetVersionStatusToString( entry.status() ).toStdString();
                item["fingerprint"] = entry.fingerprint().toStdString();
                versions.append( item );
            }
            data["versions"] = versions;
        }
        else
        {
            const auto page = store.listDatasets( 0, sicnu::dataset::DatasetStore::kMaxPageSize );
            if ( !page )
                return fail( io, "dataset", "listing failed" );
            data["total"] = static_cast<Json::Int64>( page.value().first );
            Json::Value rows( Json::arrayValue );
            for ( const QVariantMap &row : page.value().second )
            {
                Json::Value item( Json::objectValue );
                item["id"] = row.value( QStringLiteral( "id" ) ).toString().toStdString();
                item["name"] = row.value( QStringLiteral( "name" ) ).toString().toStdString();
                rows.append( item );
            }
            data["datasets"] = rows;
        }
        return io.finish( true, "dataset", data, 0 );
    }

    if ( sub == QLatin1String( "validate" ) )
    {
        if ( options.versionId.isEmpty() )
            return fail( io, "dataset", "--version is required" );
        const auto staged = store.stageVersion(
            sicnu::dataset::DatasetVersionId::fromString( options.versionId )
                .value_or( sicnu::dataset::DatasetVersionId{} ) );
        if ( !staged )
            return fail( io, "dataset", "validation failed", staged.diagnostics() );
        Json::Value data( Json::objectValue );
        data["version_id"] = versionIdStdString( options.versionId );
        data["staged"] = true;
        return io.finish( true, "dataset", data, 0 );
    }

    if ( sub == QLatin1String( "diff" ) )
    {
        if ( options.versionFrom.isEmpty() || options.versionTo.isEmpty() )
            return fail( io, "dataset", "--from and --to are required" );
        auto load = [&]( const QString &id ) -> std::optional<sicnu::dataset::DatasetManifest> {
            const auto version = store.versionById(
                sicnu::dataset::DatasetVersionId::fromString( id )
                    .value_or( sicnu::dataset::DatasetVersionId{} ) );
            if ( !version )
                return std::nullopt;
            const auto parsed = sicnu::dataset::DatasetManifest::fromJson(
                QJsonDocument::fromJson( version->manifestJson().toUtf8() ).object() );
            return parsed.has_value() ? std::optional<sicnu::dataset::DatasetManifest>( parsed.value() )
                                      : std::nullopt;
        };
        const auto from = load( options.versionFrom );
        const auto to = load( options.versionTo );
        if ( !from || !to )
            return fail( io, "dataset", "version(s) not found" );
        const auto diff = sicnu::dataset::diffManifests( from.value(), to.value() );
        if ( !diff )
            return fail( io, "dataset", "diff failed", diff.diagnostics() );
        Json::Value data( Json::objectValue );
        data["diff"] = toJsonValue( diff.value().toJson() );
        return io.finish( true, "dataset", data, 0 );
    }

    if ( sub == QLatin1String( "stats" ) )
    {
        if ( options.versionId.isEmpty() )
            return fail( io, "dataset", "--version is required" );
        const auto versionId = sicnu::dataset::DatasetVersionId::fromString( options.versionId )
                                   .value_or( sicnu::dataset::DatasetVersionId{} );
        qint64 offset = 0;
        qint64 count = 0;
        Json::Value byGroup( Json::objectValue );
        while ( true )
        {
            const auto page = store.samplesPage( versionId, offset, 500 );
            if ( !page )
                return fail( io, "dataset", "sample page failed" );
            if ( page.value().second.isEmpty() )
                break;
            for ( const auto &sample : page.value().second )
            {
                ++count;
                const std::string group = sample.groupId().isEmpty()
                                              ? "(ungrouped)"
                                              : sample.groupId().toStdString();
                byGroup[group] = byGroup[group].asInt64() + 1;
            }
            offset += page.value().second.size();
        }
        Json::Value data( Json::objectValue );
        data["sample_count"] = static_cast<Json::Int64>( count );
        data["by_group"] = byGroup;
        return io.finish( true, "dataset", data, 0 );
    }

    if ( sub == QLatin1String( "list" ) )
    {
        const qint64 limit = options.limit > 0 ? qMin( options.limit, qint64( 500 ) ) : 50;
        const qint64 cursor = options.cursor > 0 ? options.cursor : 0;
        const auto page = store.listDatasets( cursor, limit );
        if ( !page )
            return fail( io, "dataset", "listing failed" );
        Json::Value data( Json::objectValue );
        data["total"] = static_cast<Json::Int64>( page.value().first );
        Json::Value rows( Json::arrayValue );
        for ( const QVariantMap &row : page.value().second )
        {
            Json::Value item( Json::objectValue );
            item["id"] = row.value( QStringLiteral( "id" ) ).toString().toStdString();
            item["name"] = row.value( QStringLiteral( "name" ) ).toString().toStdString();
            item["description"] =
                row.value( QStringLiteral( "description" ) ).toString().toStdString();
            rows.append( item );
        }
        data["datasets"] = rows;
        data["next_cursor"] = static_cast<Json::Int64>(
            cursor + limit < page.value().first ? cursor + limit : -1 );
        return io.finish( true, "dataset", data, 0 );
    }

    if ( sub == QLatin1String( "version" ) )
    {
        if ( options.datasetId.isEmpty() )
            return fail( io, "dataset", "--dataset is required" );
        const auto datasetId = sicnu::dataset::DatasetId::fromString( options.datasetId );
        if ( !datasetId )
            return fail( io, "dataset", "invalid dataset id: " + options.datasetId.toStdString() );
        Json::Value data( Json::objectValue );
        data["dataset_id"] = options.datasetId.toStdString();
        Json::Value rows( Json::arrayValue );
        for ( const auto &entry : store.versionsOfDataset( datasetId.value() ) )
        {
            Json::Value item( Json::objectValue );
            item["version_id"] = entry.versionId().toStdString();
            item["parent_version_id"] = entry.parentVersionId().toStdString();
            item["status"] =
                sicnu::dataset::datasetVersionStatusToString( entry.status() ).toStdString();
            item["quality_level"] =
                sicnu::dataset::datasetQualityLevelToString( entry.qualityLevel() ).toStdString();
            item["fingerprint"] = entry.fingerprint().toStdString();
            item["note"] = entry.note().toStdString();
            rows.append( item );
        }
        data["versions"] = rows;
        return io.finish( true, "dataset", data, 0 );
    }

    if ( sub == QLatin1String( "label-schema" ) )
    {
        if ( options.schemaId.isEmpty() )
            return fail( io, "dataset", "--schema is required" );
        Json::Value data( Json::objectValue );
        if ( !options.schemaVersion.isEmpty() )
        {
            bool versionOk = false;
            const quint64 version = options.schemaVersion.toULongLong( &versionOk );
            if ( !versionOk )
                return fail( io, "dataset", "invalid --schema-version" );
            const auto schema = store.labelSchema( options.schemaId, version );
            if ( !schema )
                return fail( io, "dataset", "label schema not found" );
            data["schema"] = toJsonValue( schema->toJson() );
        }
        else
        {
            Json::Value rows( Json::arrayValue );
            for ( const auto &entry : store.labelSchemaVersions( options.schemaId ) )
            {
                Json::Value item( Json::objectValue );
                item["version"] = static_cast<Json::UInt64>( entry.first );
                item["fingerprint"] = entry.second.toStdString();
                rows.append( item );
            }
            data["schema_id"] = options.schemaId.toStdString();
            data["versions"] = rows;
        }
        return io.finish( true, "dataset", data, 0 );
    }

    if ( sub == QLatin1String( "split" ) )
    {
        if ( options.splitManifestId.isEmpty() )
        {
            if ( options.versionId.isEmpty() )
                return fail( io, "dataset", "--split or --version is required" );
            const auto versionId =
                sicnu::dataset::DatasetVersionId::fromString( options.versionId );
            if ( !versionId )
                return fail( io, "dataset", "invalid version id" );
            Json::Value data( Json::objectValue );
            Json::Value rows( Json::arrayValue );
            for ( const auto &manifest : store.splitManifestsForVersion( versionId.value() ) )
            {
                Json::Value item( Json::objectValue );
                item["manifest_id"] = manifest.manifestId().toStdString();
                item["method"] =
                    sicnu::dataset::splitMethodToString( manifest.config().method ).toStdString();
                item["fingerprint"] = manifest.fingerprint().toStdString();
                item["assignments"] = static_cast<Json::Int64>( manifest.assignments().size() );
                rows.append( item );
            }
            data["manifests"] = rows;
            return io.finish( true, "dataset", data, 0 );
        }
        const auto manifest = store.splitManifestById( options.splitManifestId );
        if ( !manifest )
            return fail( io, "dataset", "split manifest not found" );
        Json::Value data = toJsonValue( manifest->toJson() );
        // A 100k-assignment manifest is summarized by default; the assignment
        // page (bounded by --limit) is opt-in.
        if ( options.limit <= 0 )
            data.removeMember( "assignments" );
        else
        {
            const auto &assignments = manifest->assignments();
            const qint64 bound = qMin( options.limit, qint64( 500 ) );
            Json::Value rows( Json::arrayValue );
            const qint64 end = qMin( qint64( assignments.size() ), bound );
            for ( qint64 i = 0; i < end; ++i )
            {
                Json::Value item( Json::objectValue );
                item["sample_id"] = assignments[int( i )].sampleId.toStdString();
                item["role"] =
                    sicnu::dataset::splitRoleToString( assignments[int( i )].role ).toStdString();
                item["fold"] = assignments[int( i )].fold;
                rows.append( item );
            }
            data["assignments"] = rows;
            data["assignment_total"] = static_cast<Json::Int64>( assignments.size() );
        }
        Json::Value roleCounts( Json::objectValue );
        Json::Value foldCounts( Json::objectValue );
        for ( const auto &assignment : manifest->assignments() )
        {
            const std::string role =
                sicnu::dataset::splitRoleToString( assignment.role ).toStdString();
            roleCounts[role] = roleCounts[role].asInt64() + 1;
            if ( assignment.fold >= 0 )
            {
                const std::string foldKey = std::to_string( assignment.fold );
                foldCounts[foldKey] = foldCounts[foldKey].asInt64() + 1;
            }
        }
        data["role_counts"] = roleCounts;
        if ( !foldCounts.empty() )
            data["fold_counts"] = foldCounts;
        return io.finish( true, "dataset", data, 0 );
    }

    if ( sub == QLatin1String( "leakage" ) )
    {
        if ( options.splitManifestId.isEmpty() )
            return fail( io, "dataset", "--split is required" );
        const auto report = store.latestLeakageReport( options.splitManifestId );
        if ( !report )
            return fail( io, "dataset",
                         "no stored leakage report for split " +
                             options.splitManifestId.toStdString() );
        Json::Value data = toJsonValue( report->toJson() );
        data["clean"] = report->isClean();
        return io.finish( true, "dataset", data, 0 );
    }

    return fail( io, "dataset", "unknown dataset subcommand: " + sub.toStdString() );
}

int experimentSubcommand( const QString &sub, QStringList args, const CliIO &io )
{
    const CommonOptions options = parseOptions( args );
    if ( options.experimentDb.isEmpty() )
        return fail( io, "experiment", "--experiment-db <path> is required" );

    sicnu::experiment::ExperimentStore store;
    QString error;
    if ( !store.open( options.experimentDb, &error ) )
        return fail( io, "experiment", error.toStdString() );

    if ( sub == QLatin1String( "create" ) )
    {
        if ( options.name.isEmpty() )
            return fail( io, "experiment", "--name is required" );
        sicnu::experiment::Experiment experiment;
        experiment.setExperimentId( sicnu::experiment::ExperimentId::generate().toString() );
        experiment.setName( options.name );
        experiment.setObjective( options.objective );
        experiment.setCreatedAtUtc( QDateTime::currentDateTimeUtc() );
        const auto created = store.upsertExperiment( experiment );
        if ( !created )
            return fail( io, "experiment", "create failed", created.diagnostics() );
        Json::Value data( Json::objectValue );
        data["experiment_id"] = experiment.experimentId().toStdString();
        return io.finish( true, "experiment", data, 0 );
    }

    if ( sub == QLatin1String( "inspect" ) )
    {
        if ( options.experimentId.isEmpty() )
            return fail( io, "experiment", "--experiment is required" );
        const auto experiment = store.experimentById( options.experimentId );
        if ( !experiment )
            return fail( io, "experiment", "experiment not found" );
        Json::Value data = toJsonValue( experiment->toJson() );
        Json::Value runs( Json::arrayValue );
        const auto runPage = store.listRuns( options.experimentId );
        if ( runPage )
        {
            for ( const auto &run : runPage.value().second )
            {
                Json::Value item( Json::objectValue );
                item["run_id"] = run.runId().toStdString();
                item["status"] =
                    sicnu::dataset::runStatusToString( run.status() ).toStdString();
                item["dataset_version_id"] = run.datasetVersionId().toStdString();
                runs.append( item );
            }
        }
        data["runs"] = runs;
        return io.finish( true, "experiment", data, 0 );
    }

    if ( sub == QLatin1String( "compare" ) )
    {
        if ( options.runA.isEmpty() || options.runB.isEmpty() )
            return fail( io, "experiment", "--a and --b are required" );
        const auto a = store.runById( options.runA );
        const auto b = store.runById( options.runB );
        if ( !a || !b )
            return fail( io, "experiment", "run(s) not found" );
        const auto comparison = sicnu::experiment::RunComparison::compare( a.value(), b.value() );
        Json::Value data = toJsonValue( comparison.toJson() );
        data["metric_diff"] = toJsonValue( comparison.metricDiff( a.value(), b.value() ) );
        return io.finish( true, "experiment", data, 0 );
    }

    if ( sub == QLatin1String( "list" ) )
    {
        const qint64 limit = options.limit > 0 ? qMin( options.limit, qint64( 500 ) ) : 50;
        const qint64 cursor = options.cursor > 0 ? options.cursor : 0;
        const auto page = store.listExperiments( cursor, limit );
        if ( !page )
            return fail( io, "experiment", "listing failed" );
        Json::Value data( Json::objectValue );
        data["total"] = static_cast<Json::Int64>( page.value().first );
        Json::Value rows( Json::arrayValue );
        for ( const auto &experiment : page.value().second )
        {
            Json::Value item( Json::objectValue );
            item["experiment_id"] = experiment.experimentId().toStdString();
            item["name"] = experiment.name().toStdString();
            item["run_count"] = static_cast<Json::Int64>( experiment.runIds().size() );
            rows.append( item );
        }
        data["experiments"] = rows;
        data["next_cursor"] = static_cast<Json::Int64>(
            cursor + limit < page.value().first ? cursor + limit : -1 );
        return io.finish( true, "experiment", data, 0 );
    }

    if ( sub == QLatin1String( "run" ) )
    {
        if ( options.runId.isEmpty() )
            return fail( io, "experiment", "--run is required" );
        const auto run = store.runById( options.runId );
        if ( !run )
            return fail( io, "experiment", "run not found" );
        Json::Value data = toJsonValue( sicnu::experiment::RunEnvironment::redactSecretKeys(
            run->toJson() ) );
        data["config_hash"] = run->configHash().toStdString();
        const auto metricRecord = store.metricRecordForRun( options.runId );
        if ( metricRecord )
        {
            data["protocol"] = toJsonValue( metricRecord->protocol.toJson() );
            data["metrics_record"] = toJsonValue(
                sicnu::experiment::RunEnvironment::redactSecretKeys( metricRecord->metrics ) );
        }
        return io.finish( true, "experiment", data, 0 );
    }

    return fail( io, "experiment", "unknown experiment subcommand: " + sub.toStdString() );
}

int reproduceSubcommand( const QString &sub, QStringList args, const CliIO &io )
{
    const CommonOptions options = parseOptions( args );
    if ( options.experimentDb.isEmpty() || options.datasetDb.isEmpty() )
        return fail( io, "reproduce", "--experiment-db and --dataset-db are required" );

    sicnu::dataset::DatasetStore datasetStore;
    sicnu::experiment::ExperimentStore experimentStore;
    QString error;
    if ( !datasetStore.open( options.datasetDb, &error ) ||
         !experimentStore.open( options.experimentDb, &error ) )
        return fail( io, "reproduce", error.toStdString() );

    sicnu::experiment::ReproductionBundleExporter exporter( experimentStore, datasetStore );

    if ( sub == QLatin1String( "export" ) )
    {
        if ( options.runId.isEmpty() || options.outputDir.isEmpty() )
            return fail( io, "reproduce", "--run and --out are required" );
        sicnu::experiment::ReproductionBundleOptions bundleOptions;
        bundleOptions.outputDir = options.outputDir;
        const auto report = exporter.exportRun( options.runId, bundleOptions );
        if ( !report.ok && report.fileCount == 0 )
            return fail( io, "reproduce", "export failed" );
        Json::Value data = toJsonValue( report.toJson() );
        return io.finish( report.ok, "reproduce", data, report.ok ? 0 : 1 );
    }

    if ( sub == QLatin1String( "validate" ) )
    {
        if ( options.bundleDir.isEmpty() )
            return fail( io, "reproduce", "--bundle is required" );
        sicnu::experiment::ReproductionHooks hooks;
        const auto validation = exporter.validateBundle( options.bundleDir, hooks );
        Json::Value data = toJsonValue( validation.toJson() );
        const bool replayable =
            validation.level != sicnu::dataset::ReproductionLevel::Impossible;
        return io.finish( replayable, "reproduce", data, replayable ? 0 : 1 );
    }

    if ( sub == QLatin1String( "inspect" ) )
    {
        if ( options.runId.isEmpty() )
            return fail( io, "reproduce", "--run is required" );
        const auto run = experimentStore.runById( options.runId );
        if ( !run )
            return fail( io, "reproduce", "run not found" );
        // Same library assessment as the MCP surface: dataset/split pins
        // against the store, artifact existence probe, model/algorithm via
        // hooks (unwired here -> unknown, never a fabricated ok).
        sicnu::experiment::ReproductionHooks hooks;
        hooks.artifactAvailable = []( const QString &path, qint64 sizeBytes ) {
            const QFileInfo info( path );
            return info.exists() && ( sizeBytes <= 0 || info.size() == sizeBytes );
        };
        const auto report =
            sicnu::experiment::ReplayReadiness::assess( run.value(), &datasetStore, hooks );
        const auto fingerprint =
            sicnu::experiment::runExecutionFingerprint( run->executionIdentity() );
        const auto equivalent = sicnu::experiment::ReplayReadiness::equivalentRuns(
            experimentStore, fingerprint, options.runId );
        Json::Value data = toJsonValue( report.toJson() );
        data["run_id"] = options.runId.toStdString();
        data["execution_fingerprint"] = fingerprint.toStdString();
        Json::Value equivalentJson( Json::arrayValue );
        for ( const auto &id : equivalent )
            equivalentJson.append( id.toStdString() );
        data["equivalent_runs"] = equivalentJson;
        const bool impossible =
            report.level == sicnu::dataset::ReproductionLevel::Impossible;
        return io.finish( !impossible, "reproduce", data, impossible ? 1 : 0 );
    }

    return fail( io, "reproduce", "unknown reproduce subcommand: " + sub.toStdString() );
}

} // namespace

int commandDataset( QStringList args, const CliIO &io )
{
    const QString sub = args.isEmpty() ? QString() : args.takeFirst();
    return datasetSubcommand( sub, std::move( args ), io );
}

int commandExperiment( QStringList args, const CliIO &io )
{
    const QString sub = args.isEmpty() ? QString() : args.takeFirst();
    return experimentSubcommand( sub, std::move( args ), io );
}

int commandReproduce( QStringList args, const CliIO &io )
{
    const QString sub = args.isEmpty() ? QString() : args.takeFirst();
    return reproduceSubcommand( sub, std::move( args ), io );
}

} // namespace sicnu::cli
