// test_mlops9_cli_record.cpp — Scientific MLOps 9.0: CLI pipeline
// auto-recording E2E (goal M3, the 8.0 documented follow-up).
//
// Drives the REAL sicnu_geo_rs_cli binary with --experiment-record and
// verifies, by reading the experiment store back, that:
//   - a successful pipeline lands as exactly ONE truthful Completed run with
//     the requested identity pins and per-step workflow evidence;
//   - a failing pipeline lands as a truthful Failed run carrying the error
//     (never a success);
//   - nothing is fabricated for executions nobody recorded (resume of an
//     unknown run id leaves the store empty).
// POSIX-only spawn (popen), mirroring test_cli_commands_json.
#include <catch2/catch_test_macros.hpp>

#ifndef SICNU_TEST_CLI
#error "SICNU_TEST_CLI must point at sicnu_geo_rs_cli"
#endif

#include "dataset/dataset_store.h"
#include "dataset/dataset_version.h"
#include "experiment/experiment_store.h"
#include "experiment/experiment_types.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <gdal_priv.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace sicnu::dataset;
using namespace sicnu::experiment;

namespace
{

struct CliRun
{
    int exitCode = -1;
    std::string output;
};

CliRun runCli( const std::string &args, const QString &homeDir )
{
    const std::string command = "env HOME='" + homeDir.toStdString() + "' " +
                                std::string( SICNU_TEST_CLI ) + " " + args + " 2>&1";
    FILE *pipe = ::popen( command.c_str(), "r" );
    REQUIRE( pipe != nullptr );
    char buffer[4096];
    size_t read = 0;
    CliRun result;
    while ( ( read = fread( buffer, 1, sizeof( buffer ), pipe ) ) > 0 )
        result.output.append( buffer, read );
    const int status = ::pclose( pipe );
    result.exitCode = WIFEXITED( status ) ? WEXITSTATUS( status ) : -1;
    return result;
}

void writeTwoBandRaster( const QString &path )
{
    GDALAllRegister();
    GDALDriver *drv = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    REQUIRE( drv != nullptr );
    constexpr int W = 64;
    constexpr int H = 64;
    GDALDataset *ds = drv->Create( path.toUtf8().constData(), W, H, 2, GDT_Float32, nullptr );
    REQUIRE( ds != nullptr );
    std::vector<float> band( static_cast<size_t>( W ) * H );
    for ( int b = 1; b <= 2; ++b )
    {
        for ( int r = 0; r < H; ++r )
            for ( int c = 0; c < W; ++c )
                band[static_cast<size_t>( r ) * W + c] =
                    b == 1 ? 2000.f + 40.f * ( ( r + c ) % 64 )
                           : 1000.f + 30.f * ( ( r * 2 + c ) % 64 );
        GDALRasterBand *rb = ds->GetRasterBand( b );
        rb->RasterIO( GF_Write, 0, 0, W, H, band.data(), W, H, GDT_Float32, 0, 0 );
    }
    double gt[6] = { 0, 1, 0, static_cast<double>( H ), 0, -1 };
    ds->SetGeoTransform( gt );
    GDALClose( ds );
}

QString writePipeline( const QString &path, const QString &inputPath,
                       const QString &outputPath )
{
    QJsonObject step;
    step.insert( QStringLiteral( "id" ), QStringLiteral( "a" ) );
    step.insert( QStringLiteral( "operator" ), QStringLiteral( "rs:spectral_index" ) );
    QJsonObject params;
    params.insert( QStringLiteral( "input" ), inputPath );
    params.insert( QStringLiteral( "output" ), outputPath );
    params.insert( QStringLiteral( "index" ), QStringLiteral( "NDVI" ) );
    // Two-band synthetic raster: NIR=band 1, RED=band 2 (explicit — the
    // operator's defaults assume 4-band imagery).
    params.insert( QStringLiteral( "nir" ), 1 );
    params.insert( QStringLiteral( "red" ), 2 );
    step.insert( QStringLiteral( "params" ), params );
    QJsonArray steps;
    steps.append( step );
    QJsonObject pipeline;
    pipeline.insert( QStringLiteral( "name" ), QStringLiteral( "mlops9 recorded ndvi" ) );
    pipeline.insert( QStringLiteral( "steps" ), steps );
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    file.write( QJsonDocument( pipeline ).toJson() );
    file.close();
    return path;
}

QString writeFailingPipeline( const QString &path )
{
    QJsonObject step;
    step.insert( QStringLiteral( "id" ), QStringLiteral( "boom" ) );
    step.insert( QStringLiteral( "operator" ), QStringLiteral( "unknown:operator" ) );
    step.insert( QStringLiteral( "params" ), QJsonObject{} );
    QJsonArray steps;
    steps.append( step );
    QJsonObject pipeline;
    pipeline.insert( QStringLiteral( "name" ), QStringLiteral( "mlops9 recorded failure" ) );
    pipeline.insert( QStringLiteral( "steps" ), steps );
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    file.write( QJsonDocument( pipeline ).toJson() );
    file.close();
    return path;
}

/// Creates a committed dataset version to pin against (pin verification runs
/// through the dataset store when --dataset-db is given).
QString createPinnedVersion( const QString &datasetDbPath )
{
    DatasetStore store;
    REQUIRE( store.open( datasetDbPath ) );
    const QString datasetId = DatasetId::generate().toString();
    REQUIRE( store
                 .createDataset( DatasetId::fromString( datasetId ).value(),
                                 QStringLiteral( "mlops9 pin dataset" ) )
                 .has_value() );
    const QString versionId = DatasetVersionId::generate().toString();
    DatasetManifest manifest;
    manifest.setDatasetId( datasetId );
    manifest.setVersionId( versionId );
    manifest.setName( QStringLiteral( "pin version" ) );
    manifest.setCreatedAtUtc( QDateTime::currentDateTimeUtc() );
    REQUIRE( store.createDraftVersion( manifest ).has_value() );
    REQUIRE( store.stageVersion( DatasetVersionId::fromString( versionId ).value() ).has_value() );
    REQUIRE( store.commitVersion( DatasetVersionId::fromString( versionId ).value() ).has_value() );
    store.close();
    return versionId;
}

} // namespace

TEST_CASE( "CLI --experiment-record writes a truthful Completed run", "[mlops9][cli][record]" )
{
    QTemporaryDir homeDir;
    QTemporaryDir workDir;
    REQUIRE( homeDir.isValid() );
    REQUIRE( workDir.isValid() );

    const QString inputPath = workDir.filePath( QStringLiteral( "input.tif" ) );
    const QString outputPath = workDir.filePath( QStringLiteral( "ndvi.tif" ) );
    const QString pipelinePath = writePipeline( workDir.filePath( QStringLiteral( "pipeline.json" ) ),
                                                inputPath, outputPath );
    const QString datasetDb = workDir.filePath( QStringLiteral( "dataset.sqlite" ) );
    const QString experimentDb = workDir.filePath( QStringLiteral( "experiment.sqlite" ) );
    const QString versionId = createPinnedVersion( datasetDb );

    writeTwoBandRaster( inputPath );

    std::string args = "--experiment-record '" + experimentDb.toStdString() + "'"
                       " --experiment-id mlops9-e2e"
                       " --experiment-name 'MLOps 9 E2E'"
                       " --experiment-objective 'record CLI runs truthfully'"
                       " --dataset-db '" + datasetDb.toStdString() + "'"
                       " --pin-dataset-version '" + versionId.toStdString() + "'"
                       " --pin-seed 7"
                       " --pipeline '" + pipelinePath.toStdString() + "'";
    const CliRun run = runCli( args, homeDir.path() );
    if ( run.exitCode != 0 )
    {
        INFO( run.output );
        FAIL( "CLI pipeline run failed" );
    }

    // Read the truth back from the store (runs are the authoritative read;
    // the runs listing is how every surface consumes records).
    ExperimentStore store;
    REQUIRE( store.open( experimentDb ) );
    const auto experiment = store.experimentById( QStringLiteral( "mlops9-e2e" ) );
    REQUIRE( experiment.has_value() );
    REQUIRE( experiment->name() == QStringLiteral( "MLOps 9 E2E" ) );

    const auto runs = store.listRuns( QStringLiteral( "mlops9-e2e" ) );
    REQUIRE( runs.has_value() );
    REQUIRE( runs.value().second.size() == 1 );
    const ExperimentRun &recorded = runs.value().second.first();
    CHECK( recorded.status() == RunStatus::Completed );
    CHECK( recorded.seed() == 7 );
    CHECK( recorded.datasetVersionId() == versionId );
    // Verified pin: the dataset fingerprint is stamped from the store.
    CHECK( !recorded.datasetFingerprint().isEmpty() );
    CHECK( recorded.executionRef() == recorded.executionRef() ); // ref present (non-fabricated)
    CHECK( !recorded.executionRef().isEmpty() );
    // Step evidence rides the metrics document under "workflow".
    const QJsonObject workflow =
        recorded.metrics().value( QStringLiteral( "workflow" ) ).toObject();
    CHECK( workflow.value( QStringLiteral( "steps" ) ).toArray().size() == 1 );
    CHECK( workflow.value( QStringLiteral( "steps" ) ).toArray().first().toObject()
               .value( QStringLiteral( "status" ) ).toString() == QStringLiteral( "Completed" ) );
    store.close();
}

TEST_CASE( "CLI --experiment-record records failures truthfully", "[mlops9][cli][record]" )
{
    QTemporaryDir homeDir;
    QTemporaryDir workDir;
    REQUIRE( homeDir.isValid() );
    REQUIRE( workDir.isValid() );

    const QString pipelinePath = writeFailingPipeline( workDir.filePath( QStringLiteral( "bad.json" ) ) );
    const QString experimentDb = workDir.filePath( QStringLiteral( "experiment.sqlite" ) );

    std::string args = "--experiment-record '" + experimentDb.toStdString() + "'"
                       " --experiment-id mlops9-fail"
                       " --pipeline '" + pipelinePath.toStdString() + "'";
    const CliRun run = runCli( args, homeDir.path() );
    INFO( run.output );
    REQUIRE( run.exitCode != 0 ); // the pipeline must fail

    ExperimentStore store;
    REQUIRE( store.open( experimentDb ) );
    const auto runs = store.listRuns( QStringLiteral( "mlops9-fail" ) );
    REQUIRE( runs.has_value() );
    REQUIRE( runs.value().second.size() == 1 );
    const ExperimentRun &recorded = runs.value().second.first();
    CHECK( recorded.status() == RunStatus::Failed );
    // The failed run explains itself.
    CHECK( recorded.metrics().value( QStringLiteral( "error" ) ).toObject().size() > 0 );
    store.close();
}

TEST_CASE( "recording fabricates nothing for unrecorded executions", "[mlops9][cli][record]" )
{
    QTemporaryDir homeDir;
    QTemporaryDir workDir;
    REQUIRE( homeDir.isValid() );
    REQUIRE( workDir.isValid() );

    const QString experimentDb = workDir.filePath( QStringLiteral( "experiment.sqlite" ) );
    // Resuming an execution nobody recorded must not create a record.
    std::string args = "--experiment-record '" + experimentDb.toStdString() + "'"
                       " --experiment-id mlops9-ghost"
                       " --resume does-not-exist";
    const CliRun run = runCli( args, homeDir.path() );
    INFO( run.output );
    REQUIRE( run.exitCode != 0 );

    if ( QFile::exists( experimentDb ) )
    {
        ExperimentStore store;
        REQUIRE( store.open( experimentDb ) );
        const auto runs = store.listRuns( QStringLiteral( "mlops9-ghost" ) );
        REQUIRE( runs.has_value() );
        CHECK( runs.value().second.isEmpty() );
        store.close();
    }
}
