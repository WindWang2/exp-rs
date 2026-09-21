// test_study_e2e.cpp — full-stack teaching scenario (RS14-07).
//
// The exemplar story, end to end on the REAL platform:
//   a student's NDVI raster → `rs:threshold_raster` threshold sweep (OAT)
//   → ExecutionPlane → TaskCenter → JobEngine → committed rasters
//   → ExperimentStore truth → spatial difference summaries
//   → `sicnu.studyreport.v1` document (student narrative + agent evidence).
//
// The exemplar spec is loaded from examples/studies/ and validated through
// the production reader — the shipped file cannot silently rot.
#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <atomic>

#include <cpl_conv.h>
#include <gdal.h>
#include <gdal_priv.h>

#include "data/data_manager.h"
#include "experiment/experiment_matrix.h"
#include "experiment/experiment_store.h"
#include "jobs/job_engine.h"
#include "processing/framework/atomic_algorithm_registry.h"
#include "processing/framework/execution_plane.h"
#include "qgsapplication.h"
#include "study/bridge/study_execution_plane.h"
#include "study/study_export.h"
#include "study/study_runner.h"
#include "study/study_sampling.h"
#include "study/study_spatial.h"

using namespace sicnu::study;

namespace
{

// One QGIS application for the whole binary (house pattern: real operators
// touch QgsSettings/QGIS paths, which a bare QCoreApplication does not
// provide — see test_asset_preview_service). Never exec'd; #568 forbids
// per-TEST_CASE app instances.
QCoreApplication *ensureCoreApp()
{
    if ( QCoreApplication::instance() )
        return QCoreApplication::instance();
    static int argc = 1;
    static char arg0[] = "test_study_e2e";
    char *argv[] = { arg0, nullptr };
    auto *app = new QgsApplication( argc, argv, true );
    QgsApplication::initQgis();
    return app;
}

// Bridge the AtomicAlgorithmRegistry into JobEngine exactly like production
// (main.cpp ADR 0062 wiring) so plane submissions actually execute.
void wireRegistryFallback()
{
    sicnu::jobs::JobEngine::instance().setFallbackExecutor(
        []( const sicnu::jobs::JobRequest &req, sicnu::operators::RSOperatorContext &ctx )
            -> Json::Value {
            const auto adapter =
                sicnu::processing::AtomicAlgorithmRegistry::instance().findAdapter(
                    req.algorithmId );
            if ( !adapter )
                throw std::runtime_error( "Unknown algorithm: " + req.algorithmId );
            sicnu::processing::ProgressCallback progressBridge =
                [&ctx]( int percent, const std::string &message ) {
                    ctx.reportProgress( percent / 100.0, message );
                };
            return adapter->execute( req.params, progressBridge,
                                     [&ctx]() { return ctx.isCancelled(); } );
        } );
}

/// Synthetic "NDVI" raster: a deterministic [0,1] gradient over a 16x16 grid.
void writeSyntheticNdvi( const QString &path )
{
    GDALAllRegister();
    GDALDriverH driver = GDALGetDriverByName( "GTiff" );
    REQUIRE( driver != nullptr );
    GDALDatasetH ds = GDALCreate( driver, path.toUtf8().constData(), 16, 16, 1, GDT_Float32,
                                  nullptr );
    REQUIRE( ds != nullptr );
    float pixels[256];
    for ( int y = 0; y < 16; ++y )
        for ( int x = 0; x < 16; ++x )
            pixels[y * 16 + x] = static_cast<float>( x + y ) / 30.0f; // 0 .. 1
    REQUIRE( GDALRasterIO( GDALGetRasterBand( ds, 1 ), GF_Write, 0, 0, 16, 16, pixels, 16,
                           16, GDT_Float32, 0, 0 ) == CE_None );
    GDALClose( ds );
}

} // namespace

TEST_CASE( "NDVI threshold exemplar study, end to end on the real spine",
           "[study][e2e][teaching]" )
{
    ensureCoreApp();
    wireRegistryFallback();

    QTemporaryDir workDir;
    REQUIRE( workDir.isValid() );

    // 1. The shipped exemplar spec loads through the production reader.
    QFile exemplarFile( QStringLiteral( CMAKE_SOURCE_DIR
                                         "/examples/studies/ndvi-threshold.sicnu-study.json" ) );
    REQUIRE( exemplarFile.open( QIODevice::ReadOnly ) );
    QJsonObject exemplarJson =
        QJsonDocument::fromJson( exemplarFile.readAll() ).object();
    REQUIRE( !exemplarJson.isEmpty() );

    // 2. The student's raster replaces the placeholder input.
    const QString rasterPath = workDir.filePath( QStringLiteral( "ndvi.tif" ) );
    writeSyntheticNdvi( rasterPath );
    const QString outputDir = workDir.filePath( QStringLiteral( "study-outputs" ) );
    const QString experimentDb = workDir.filePath( QStringLiteral( "experiments.db" ) );

    QJsonObject baseParameters = exemplarJson.value( QStringLiteral( "base_parameters" ) ).toObject();
    baseParameters.insert( QStringLiteral( "input" ), rasterPath );
    exemplarJson.insert( QStringLiteral( "base_parameters" ), baseParameters );
    exemplarJson.insert( QStringLiteral( "experiment_id" ),
                         QStringLiteral( "study-e2e-ndvi" ) );

    const auto spec = ParameterStudySpec::fromJson( exemplarJson );
    REQUIRE( spec.has_value() );
    REQUIRE( spec.value().strategy == SamplingStrategy::OneAtATime );
    const QVector<StudyPoint> points = sampleStudyPoints( spec.value() ).value();
    REQUIRE( points.size() == 9 ); // baseline + 8 single-dimension variations

    // 3. Run the study through the REAL execution spine.
    sicnu::experiment::ExperimentStore store;
    REQUIRE( store.open( experimentDb ) );
    sicnu::experiment::MatrixLedger ledger( store );
    ExecutionPlaneStudyBackend backend;
    StudyRunner runner( store, ledger, backend );
    std::atomic<bool> cancel{ false };

    const auto result = runner.run( spec.value(), cancel, outputDir );
    REQUIRE( result.has_value() );
    const auto &summary = result.value();
    REQUIRE( summary.totalPoints == 9 );
    if ( summary.recordedCount != 9 )
    {
        for ( const QString &runId : summary.runIds )
        {
            const auto run = store.runById( runId );
            if ( run && run.value().status() != sicnu::experiment::RunStatus::Completed )
                WARN( QStringLiteral( "run %1 status %2 error: %3" )
                          .arg( runId )
                          .arg( sicnu::experiment::runStatusToString( run.value().status() ),
                                run.value()
                                    .metrics()
                                    .value( QStringLiteral( "error" ) )
                                    .toObject()
                                    .value( QStringLiteral( "message" ) )
                                    .toString() ) );
        }
    }
    REQUIRE( summary.recordedCount == 9 );
    REQUIRE( summary.failedCount == 0 );
    REQUIRE( summary.cancelledCount == 0 );

    // 4. Every point has a committed raster and a recorded metric.
    for ( const StudyPoint &point : points )
    {
        const QString output =
            outputDir + QStringLiteral( "/" ) + point.pointId
            + QStringLiteral( "/output.tif" );
        REQUIRE( QFile::exists( output ) );
        const auto runs = ledger.runsForCell( point.pointId );
        REQUIRE( runs.size() == 1 );
        const auto run = store.runById( runs.first() );
        REQUIRE( run.has_value() );
        REQUIRE( run.value().status() == sicnu::experiment::RunStatus::Completed );
        REQUIRE( run.value()
                     .metrics()
                     .value( QStringLiteral( "maskedPercent" ) )
                     .isDouble() );
    }

    // Spatial differences vs the baseline run (threshold = 0.5 reference) —
    // composed through the production helper the spec field declares.
    GdalRasterDifferenceSummarizer summarizer( spec.value().spatialEpsilon );
    const auto spatial = summarizeStudyOutputs( store, ledger, spec.value(), points,
                                                outputDir, summarizer );
    REQUIRE( spatial.has_value() );
    REQUIRE( spatial.value().size() == 8 );
    for ( const SpatialDifferenceSummary &diff : spatial.value() )
    {
        REQUIRE( diff.validPixels == 256 );
        REQUIRE( diff.changedPixels > 0 ); // different thresholds ⇒ different masks
    }

    // 5. The report serves both audiences.
    const StudyReport report =
        buildStudyReport( store, ledger, spec.value(), points, spatial.value(), &summary );
    const QString reportPath = workDir.filePath( QStringLiteral( "study.sicnu-studyreport.json" ) );
    REQUIRE( writeStudyReport( report, reportPath ).has_value() );

    // Teaching: the curves exist and the mechanical trend is factual —
    // higher threshold ⇒ fewer masked pixels on the gradient raster. The
    // exemplar aggregates TWO metrics, so there is one curve per metric.
    REQUIRE( report.curves.size() == 2 );
    const SensitivityCurve *maskedCurve = nullptr;
    for ( const SensitivityCurve &curve : report.curves )
        if ( curve.metricName == QStringLiteral( "maskedPercent" ) )
            maskedCurve = &curve;
    REQUIRE( maskedCurve != nullptr );
    REQUIRE( maskedCurve->points.size() == 9 );
    REQUIRE( maskedCurve->trend == QStringLiteral( "decreasing" ) );
    REQUIRE( report.narrative.first().observation.contains( QStringLiteral( "fall" ) ) );
    // No objective metric in the exemplar → no "best" verdict anywhere.
    REQUIRE( report.declaredBest.isEmpty() );
    REQUIRE( report.paretoPointIds.isEmpty() );

    // Agent: the standalone document carries versioned, explicit evidence.
    QFile reportFile( reportPath );
    REQUIRE( reportFile.open( QIODevice::ReadOnly ) );
    const QJsonObject document = QJsonDocument::fromJson( reportFile.readAll() ).object();
    REQUIRE( document.value( QStringLiteral( "document_type" ) ).toString()
             == QStringLiteral( "sicnu.studyreport.v1" ) );
    REQUIRE( document.value( QStringLiteral( "status_accounting" ) )
                 .toObject()
                 .value( QStringLiteral( "recorded" ) )
                 .toInt() == 9 );
    REQUIRE( document.value( QStringLiteral( "spatial_summaries" ) ).toArray().size() == 8 );
    REQUIRE( document.contains( QStringLiteral( "usage_notes" ) ) );
}
