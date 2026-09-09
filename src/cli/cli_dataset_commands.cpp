// cli_dataset_commands.cpp — dataset/experiment/reproduce CLI groups.
#include "cli_dataset_commands.h"

#include "cli_commands.h"

#include "dataset/dataset_manifest.h"
#include "dataset/dataset_store.h"
#include "dataset/sample.h"
#include "dataset/dataset_types.h"
#include "dataset/dataset_version.h"
#include "experiment/experiment_store.h"
#include "experiment/experiment_types.h"
#include "experiment/reproduction_bundle.h"

#include <json/json.h>

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
        QStringLiteral( "--bundle" ),
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
