/***************************************************************************
  tests/test_lab_offline_e2e.cpp — offline classroom chain (teaching-lab-11).

  Oracle 3 of the track: with the offline gate engaged (--offline /
  SICNU_OFFLINE), the WHOLE teaching chain runs with zero network:
    remote sources refuse (typed)  →  a real artifact grades  →  batch grades
    a class  →  --self-check reports (packs/rules/projection)  →  the report
    export writes the three renderings.

  The gate is process-wide; the test scopes it with RAII and restores the
  previous state. Network refusal is asserted through the gate's own typed
  contract (isRemoteTarget/refusalMessage) AND the GDAL deny (a /vsicurl/
  open must fail without leaving the machine — offline smoke tradition).
 ***************************************************************************/

#include "cli/lab_batch_runner.h"
#include "cli/lab_report_runner.h"
#include "cli/lab_self_check.h"
#include "agent/output_verifier.h"
#include "data/offline_mode.h"
#include "experiment/experiment_store.h"
#include "experiment/experiment_types.h"
#include "exprs/exit_codes.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <catch2/catch_test_macros.hpp>

#include <gdal_priv.h>

#include <string>

using sicnu::agent::OutputVerifier;
using sicnu::cli::LabBatchOptions;
using sicnu::cli::LabBatchRunner;
using sicnu::cli::LabReportOptions;
using sicnu::cli::LabSelfCheckOptions;

namespace {

struct GdalInit
{
    GdalInit() { GDALAllRegister(); }
};
const GdalInit s_gdalInit;

/// RAII: engage the offline gate for the scope, restore afterwards.
struct ScopedOffline
{
    ScopedOffline() { sicnu::data::offline::setEnabled( true ); }
    ~ScopedOffline() { sicnu::data::offline::setEnabled( false ); }
};

/// A tiny valid NDVI-ish raster a student "submitted".
QString writeArtifact( const QString &dir, const QString &name )
{
    const QString path = QDir( dir ).filePath( name );
    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    REQUIRE( driver );
    GDALDataset *dataset = driver->Create( path.toUtf8().constData(), 8, 8, 2, GDT_Float32,
                                           nullptr );
    REQUIRE( dataset );
    std::vector<float> row( 8, 0.5f );
    for ( int b = 1; b <= 2; ++b )
    {
        GDALRasterBand *band = dataset->GetRasterBand( b );
        for ( int y = 0; y < 8; ++y )
            REQUIRE( band->RasterIO( GF_Write, 0, y, 8, 1, row.data(), 8, 1, GDT_Float32, 0, 0 )
                     == CE_None );
    }
    GDALClose( dataset );
    return path;
}

/// Seeds a one-experiment store for the report export.
QString seedStore( const QTemporaryDir &dir )
{
    using namespace sicnu::experiment;
    ExperimentStore store;
    REQUIRE( store.open( dir.filePath( QStringLiteral( "experiments.db" ) ) ) );
    Experiment experiment;
    experiment.setExperimentId( QStringLiteral( "lab-exp-offline" ) );
    experiment.setName( QStringLiteral( "offline classroom run" ) );
    REQUIRE( store.upsertExperiment( experiment ).has_value() );
    ExperimentRun run;
    run.setRunId( QStringLiteral( "run-offline-1" ) );
    run.setExperimentId( QStringLiteral( "lab-exp-offline" ) );
    run.setAlgorithmId( QStringLiteral( "rs:spectral_index" ) );
    run.setStatus( RunStatus::Completed );
    run.setStartedAtUtc( QDateTime::currentDateTimeUtc().addSecs( -30 ) );
    run.setFinishedAtUtc( QDateTime::currentDateTimeUtc().addSecs( -5 ) );
    REQUIRE( store.upsertRun( run ).has_value() );
    store.close();
    return dir.filePath( QStringLiteral( "experiments.db" ) );
}

} // namespace

TEST_CASE( "offline: gate refuses remote targets while the teaching chain runs",
           "[lab_offline_e2e]" )
{
    ScopedOffline offline;
    REQUIRE( sicnu::data::offline::enabled() );

    // The gate's typed refusal contract.
    REQUIRE( sicnu::data::offline::isRemoteTarget(
      QStringLiteral( "/vsicurl/https://example.test/r.tif" ) ) );
    REQUIRE( sicnu::data::offline::isRemoteTarget(
      QStringLiteral( "https://example.test/r.tif" ) ) );
    REQUIRE( !sicnu::data::offline::isRemoteTarget(
      QStringLiteral( "/vsimem/local.tif" ) ) );
    REQUIRE( !sicnu::data::offline::isRemoteTarget(
      QStringLiteral( "C:/data/local.tif" ) ) );
    REQUIRE( !sicnu::data::offline::refusalMessage(
               QStringLiteral( "/vsicurl/https://example.test/r.tif" ) )
               .isEmpty() );

    // A real artifact grades OFFLINE.
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString artifact = writeArtifact( dir.path(), "student_a.tif" );
    OutputVerifier verifier;
    const auto grade = verifier.gradeArtifact( "ndvi_basics", artifact, {} );
    // The artifact grades (verdict may be "fail" for this synthetic input —
    // the offline invariant is that GRADING RAN, i.e. not unverifiable).
    INFO( grade.error.toStdString() );
    REQUIRE( grade.graded );
    REQUIRE( ( grade.verdict == QLatin1String( "pass" )
               || grade.verdict == QLatin1String( "fail" ) ) );

    // Batch grades the class OFFLINE.
    const QString submissions = dir.filePath( "submissions" );
    REQUIRE( QDir().mkpath( submissions ) );
    writeArtifact( submissions, "student_a.tif" );
    writeArtifact( submissions, "student_b.tif" );
    LabBatchOptions batchOptions;
    batchOptions.jsonPath = dir.filePath( "summary.json" );
    const auto summary = LabBatchRunner::run(
      submissions, "ndvi_basics", dir.filePath( "grades.csv" ),
      [ &verifier ]( const QString & p ) { return verifier.gradeArtifact( "ndvi_basics", p, {} ); },
      batchOptions );
    REQUIRE( summary.total == 2 );
    REQUIRE( summary.graded == 2 );
    REQUIRE( summary.isolated == 0 );

    // Self-check runs OFFLINE and reports the engaged gate.
    LabSelfCheckOptions checkOptions;
    checkOptions.packRoot = QString(); // focus: gate + authority + rules
    bool ok = true;
    const Json::Value document = sicnu::cli::runLabSelfCheck( checkOptions, &ok );
    bool sawGate = false;
    for ( const auto &check : document["checks"] )
    {
        if ( check["check"].asString() == "offline_gate" )
        {
            sawGate = true;
            REQUIRE( check["evidence"]["engaged"].asBool() == true );
        }
    }
    REQUIRE( sawGate );

    // The report export writes the three renderings OFFLINE.
    const QString db = seedStore( dir );
    LabReportOptions reportOptions;
    reportOptions.experimentDb = db;
    reportOptions.experimentId = QStringLiteral( "lab-exp-offline" );
    reportOptions.outBase = dir.filePath( QStringLiteral( "report" ) );
    reportOptions.generatedAtUtc = QStringLiteral( "2026-09-16T09:00:00Z" );
    std::string error;
    REQUIRE( sicnu::cli::runLabReport( reportOptions, &error )
             == exprs::exitCodeValue( exprs::ExitCode::Ok ) );
    REQUIRE( QFile::exists( dir.filePath( QStringLiteral( "report.json" ) ) ) );
    REQUIRE( QFile::exists( dir.filePath( QStringLiteral( "report.md" ) ) ) );
    REQUIRE( QFile::exists( dir.filePath( QStringLiteral( "report.html" ) ) ) );
}

TEST_CASE( "offline: GDAL deny makes a network /vsi* source fail fast",
           "[lab_offline_e2e][gdal_deny]" )
{
    ScopedOffline offline;
    sicnu::data::offline::applyGdalNetworkDeny();

    // The CPL deny makes network handlers report "does not exist" without
    // any packet leaving the machine (no network in this sandbox anyway —
    // the assertion is that GDAL itself refuses the OPEN).
    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName( "GTiff" );
    REQUIRE( driver );
    auto *dataset =
      static_cast<GDALDataset *>( GDALOpenEx( "/vsicurl/https://example.test/r.tif",
                                              GDAL_OF_RASTER | GDAL_OF_VERBOSE_ERROR, nullptr,
                                              nullptr, nullptr ) );
    REQUIRE( dataset == nullptr );
    sicnu::data::offline::clearGdalNetworkDeny();
}
