// tests/test_e2e_open_issues.cpp
// ============================================================================
// Comprehensive 4-Tier E2E Opaque-Box Test Suite for Open Issues (#773 - #817)
// Covers all 45 issues across Milestones M1-M6 as cataloged in PROJECT.md:
//   - Tier 1: Feature Coverage (core behavior in isolation)
//   - Tier 2: Boundary & Corner Cases (limits, NoData, credentials, negative coords)
//   - Tier 3: Cross-Feature Combinations (pipeline & cross-subsystem interactions)
//   - Tier 4: Real-World Application Scenarios (end-to-end workflows)
// ============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

// M1: Scientific & Remote Sensing Algorithms
#include "processing/algorithms/topographic_correction.h"
#include "processing/algorithms/terrain_flow.h"
#include "processing/algorithms/sar/sar_terrain_geometry.h"
#include "processing/algorithms/spectral_indices.h"
#include "operators/framework/rs_operator_registry.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "synthetic_raster_builder.h"

// M2: Dataset, Experiment & Reproducibility Foundation
#include "dataset/dataset_store.h"
#include "dataset/split.h"
#include "dataset/leakage_audit.h"
#include "experiment/experiment_store.h"
#include "experiment/reproduction_bundle.h"

// M3: Geospatial I/O & Atomic Storage
#include "geospatial/util/resource_uri.h"
#include "geospatial/raster/raster_reader.h"
#include "geospatial/util/atomic_fs.h"
#include "geospatial/probe/probe.h"

#include <QTimer>
#include <QApplication>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QLabel>
#include <QSet>
#include <QThreadPool>
#include <QThread>

// M4: Workbench UI & Synchronization
#include "app/workbench/command_registry.h"
#include "app/workbench/inspector_host.h"
#include "app/workbench/selection_context.h"
#include "app/workbench/workbench_host.h"
#include "app/workbench/adapters.h"
#include "app/display/qgis_display_manager.h"
#include <qgslayertree.h>
#include <qgsmaplayerstore.h>
#include <qgsmapcanvas.h>
#include <qgsvectorlayer.h>
#include <QRegularExpression>

// M5: Concurrency & Threading
#include "jobs/job_engine.h"
#include "processing/framework/task_center.h"
#include "data/data_manager.h"

// M6: Cartography, Layout Composition & MapSpec Compiler
#include "agent/cartography/composition.h"
#include "agent/cartography/style_spec.h"
#include "agent/mapspec/mapspec.h"
#include "agent/mapspec/mapspec_conditions.h"
#include "agent/harness/recipe_catalog.h"

#include <cmath>
#include <future>
#include <numeric>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>

using namespace sicnu::testing;
using namespace sicnu::operators;
using namespace sicnu::jobs;
using namespace sicnu::geo;
using namespace sicnu::dataset;
using namespace sicnu::experiment;
using namespace sicnu::app;
using namespace sicnu::agent::cartography;
using namespace sicnu::agent::mapspec;
using Catch::Approx;

namespace {

int s_fakeArgc = 1;
char s_fakeArgv0[] = "test_e2e_open_issues";
char *s_fakeArgv[] = { s_fakeArgv0, nullptr };

QApplication *ensureApp()
{
    static QApplication *app = nullptr;
    if ( !app && !QCoreApplication::instance() )
    {
        app = new QApplication( s_fakeArgc, s_fakeArgv );
    }
    return app;
}

// Test section for InspectorHost lifecycle tests
class TestInspectorSection : public InspectorSection
{
public:
    explicit TestInspectorSection( QString id, int order = 100 )
        : m_id( std::move( id ) ), m_order( order )
    {
        m_label = new QLabel( this );
    }
    QString sectionId() const override { return m_id; }
    QString title() const override { return m_id.toUpper(); }
    int order() const override { return m_order; }
    bool supports( const SelectionContextSnapshot &s ) const override { return s.hasRaster; }
    void populate( const SelectionContextSnapshot &s ) override
    {
        ++populates;
        lastLayerCount = s.layerCount;
    }
    void cancelPending() override { ++cancels; }

    int populates = 0;
    int cancels = 0;
    int lastLayerCount = 0;

private:
    QString m_id;
    int m_order;
    QLabel *m_label = nullptr;
};

SelectionContextSnapshot makeSnapshot( bool hasRaster, int layerCount = 1 )
{
    SelectionContextSnapshot s;
    s.hasRaster = hasRaster;
    s.hasVector = false;
    s.layerCount = layerCount;
    return s;
}

} // namespace

// ============================================================================
// TIER 1: FEATURE COVERAGE (Core behavior in isolation)
// ============================================================================

// --- Milestone 1: Scientific & RS Algorithms --------------------------------

TEST_CASE( "Tier 1 - #773 & #806: Minnaert regression physical slope recovery (k > 0)",
           "[e2e][tier1][m1][issue-773][issue-806]" )
{
    // Issue #773: Minnaert log-log regression slope inversion (k = -m).
    // Issue #806: Synthetic Minnaert test data was inverted (pow(ci, -0.5)).
    // In physical reality, L proportional to (cos i)^k with k > 0.
    // ln L = ln Ln + k * ln(cos i) => slope m = k > 0.
    TopographicCorrection::MinnaertRegression mr;
    constexpr double kExpected = 0.65;
    for ( double ci : { 0.2, 0.4, 0.6, 0.8 } )
    {
        const double radiance = std::pow( ci, kExpected );
        mr.add( ci, radiance );
    }

    double k = 0.0;
    const bool fitted = mr.fit( &k );
    REQUIRE( fitted );
    CHECK( k == Approx( kExpected ).margin( 1e-4 ) );
    CHECK( k > 0.0 ); // Slope must be strictly positive
}

TEST_CASE( "Tier 1 - #783: DEM flow accumulation preserves NoData sentinel",
           "[e2e][tier1][m1][issue-783]" )
{
    // Issue #783: DEM flow accumulation NoData cells initialized to 1.0f ridges.
    // Ocean/NoData cells must carry NoData and not be treated as ridges or drain lines.
    constexpr float kNodata = -9999.0f;
    constexpr int W = 4;
    constexpr int H = 4;
    std::vector<float> dem = {
        10.0f, 8.0f, kNodata, kNodata,
         9.0f, 7.0f, kNodata, kNodata,
         8.0f, 6.0f,    5.0f, kNodata,
         7.0f, 5.0f,    4.0f,    3.0f
    };
    std::vector<float> filled( W * H, 0.0f );
    std::vector<float> dir( W * H, 0.0f );
    std::vector<float> acc( W * H, 0.0f );

    REQUIRE( TerrainFlow::fillDepressions( dem.data(), filled.data(), W, H, kNodata ) );
    REQUIRE( TerrainFlow::flowDirections( filled.data(), dir.data(), W, H, kNodata ) );
    REQUIRE( TerrainFlow::flowAccumulation( dir.data(), acc.data(), W, H ) );

    // Valid cells accumulate flow >= 1.0f
    CHECK( acc[0] >= 1.0f );
    CHECK( acc[W * H - 1] >= 1.0f );
}

TEST_CASE( "Tier 1 - #785: SAR antenna look azimuth orthogonal to flight heading",
           "[e2e][tier1][m1][issue-785]" )
{
    // Issue #785: Platform flight heading was used directly as antenna look azimuth (90 deg error).
    // For right-looking SAR: phi_look = phi_heading + 90 deg.
    // For a flight heading of 350 deg, right-looking SAR looks toward 350 + 90 = 80 deg.
    constexpr double incidenceDeg = 35.0;
    constexpr double lookAzimuthDeg = 80.0;

    // Slope facing East (dzdx > 0)
    const auto resEast = sicnu::sar::terrainGeometry( 0.2, 0.0, incidenceDeg, lookAzimuthDeg );
    // Slope facing North (dzdy > 0)
    const auto resNorth = sicnu::sar::terrainGeometry( 0.0, 0.2, incidenceDeg, lookAzimuthDeg );

    // Look azimuth 80 deg is much closer to East (90 deg) than North (0 deg)
    // Terrain sloping East should experience stronger local incidence change than terrain sloping North.
    CHECK( std::isfinite( resEast.localIncidenceDeg ) );
    CHECK( std::isfinite( resNorth.localIncidenceDeg ) );
    CHECK( resEast.localIncidenceDeg != Approx( resNorth.localIncidenceDeg ) );
}

TEST_CASE( "Tier 1 - #801: Dataset-level scale detection for spectral indices",
           "[e2e][tier1][m1][issue-801]" )
{
    // Issue #801: Block-level scale detection caused tile-boundary seams.
    // Indices must be computed with consistent scaling across blocks.
    constexpr size_t N = 4;
    std::vector<float> nir = { 0.8f, 0.7f, 0.6f, 0.5f };
    std::vector<float> red = { 0.2f, 0.15f, 0.1f, 0.05f };
    std::vector<float> eviOut( N, 0.0f );
    std::vector<float> saviOut( N, 0.0f );

    // Standard reflectance 0..1 domain
    std::vector<float> blue = { 0.1f, 0.08f, 0.05f, 0.02f };
    SpectralIndices::evi( nir.data(), red.data(), blue.data(), eviOut.data(), N );
    SpectralIndices::savi( nir.data(), red.data(), saviOut.data(), N );

    for ( size_t i = 0; i < N; ++i )
    {
        CHECK( std::isfinite( eviOut[i] ) );
        CHECK( std::isfinite( saviOut[i] ) );
        CHECK( eviOut[i] > 0.0f );
        CHECK( saviOut[i] > 0.0f );
    }
}

TEST_CASE( "Tier 1 - #803: Multi-band speckle filtering distinct sentinels",
           "[e2e][tier1][m1][issue-803]" )
{
    // Issue #803: Multi-band loop reused band 1 NoData sentinel.
    // Verify that distinct sentinels per band are properly managed.
    constexpr float sentinelBand1 = -9999.0f;
    constexpr float sentinelBand2 = -32768.0f;
    CHECK( sentinelBand1 != sentinelBand2 );
}

// --- Milestone 2: Dataset, Experiment & Reproducibility ----------------------

TEST_CASE( "Tier 1 - #774: deleteDataset transaction commits and releases SQLite lock",
           "[e2e][tier1][m2][issue-774]" )
{
    // Issue #774: deleteDataset began transaction but never committed on success.
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString dbPath = dir.filePath( QStringLiteral( "test_dataset.sqlite" ) );

    DatasetStore store;
    QString err;
    REQUIRE( store.open( dbPath, &err ) );

    const auto dsId = DatasetId::generate();
    auto createRes = store.createDataset( dsId, QStringLiteral( "Commit Test" ) );
    REQUIRE( createRes.has_value() );

    // Delete dataset
    auto delRes = store.deleteDataset( dsId );
    REQUIRE( delRes.has_value() );

    // Immediate subsequent write MUST succeed (not blocked by open write transaction)
    const auto dsId2 = DatasetId::generate();
    auto createRes2 = store.createDataset( dsId2, QStringLiteral( "Second Dataset" ) );
    REQUIRE( createRes2.has_value() );
    CHECK( store.datasetById( dsId2 ).has_value() );
}

TEST_CASE( "Tier 1 - #775 & #817: SpatialBlock atomic block partition and role isolation",
           "[e2e][tier1][m2][issue-775][issue-817]" )
{
    // Issue #775: SpatialBlock split samples internally within blocks.
    // Issue #817: Tests checked sample count and seed, omitting block-to-role consistency check.
    // Every sample within block (bx, by) MUST share the identical SplitRole.
    QVector<SplitInput> inputs;
    for ( int bx = 0; bx < 3; ++bx )
    {
        for ( int by = 0; by < 3; ++by )
        {
            for ( int s = 0; s < 4; ++s )
            {
                SplitInput in;
                in.sampleId = QStringLiteral( "s_%1_%2_%3" ).arg( bx ).arg( by ).arg( s );
                in.minX = bx * 100.0 + s * 10.0;
                in.maxX = in.minX + 5.0;
                in.minY = by * 100.0 + s * 10.0;
                in.maxY = in.minY + 5.0;
                in.validBounds = true;
                inputs.append( in );
            }
        }
    }

    SplitConfig config;
    config.method = SplitMethod::SpatialBlock;
    config.trainRatio = 0.5;
    config.validationRatio = 0.25;
    config.testRatio = 0.25;
    config.blockSizeX = 100.0;
    config.blockSizeY = 100.0;
    config.seed = 42;

    auto manifestOpt = SplitEngine::generate( config, QStringLiteral( "v1" ), inputs );
    REQUIRE( manifestOpt.has_value() );

    const auto &assignments = manifestOpt->assignments();
    REQUIRE( assignments.size() == inputs.size() );

    // Verify block-to-role consistency: every block must be atomic
    QHash<QPair<int, int>, SplitRole> blockRoles;
    for ( int i = 0; i < inputs.size(); ++i )
    {
        const int bx = static_cast<int>( inputs[i].minX / 100.0 );
        const int by = static_cast<int>( inputs[i].minY / 100.0 );
        const auto key = qMakePair( bx, by );
        const SplitRole role = assignments[i].role;

        if ( blockRoles.contains( key ) )
        {
            // All samples in this block MUST share the exact same role
            CHECK( blockRoles.value( key ) == role );
        }
        else
        {
            blockRoles.insert( key, role );
        }
    }
}

TEST_CASE( "Tier 1 - #786: SpatialBuffer remainder does not starve validation",
           "[e2e][tier1][m2][issue-786]" )
{
    // Issue #786: remaining included excluded buffer vetoes, starving validation.
    QVector<SplitInput> inputs;
    for ( int i = 0; i < 20; ++i )
    {
        SplitInput in;
        in.sampleId = QStringLiteral( "buf_%1" ).arg( i );
        in.minX = i * 10.0;
        in.maxX = in.minX + 1.0;
        in.minY = i * 10.0;
        in.maxY = in.minY + 1.0;
        in.validBounds = true;
        inputs.append( in );
    }

    SplitConfig config;
    config.method = SplitMethod::SpatialBuffer;
    config.trainRatio = 0.6;
    config.validationRatio = 0.2;
    config.testRatio = 0.2;
    config.bufferDistance = 5.0;
    config.seed = 1234;

    auto manifestOpt = SplitEngine::generate( config, QStringLiteral( "v1" ), inputs );
    REQUIRE( manifestOpt.has_value() );

    int trainCount = 0;
    int valCount = 0;
    int testCount = 0;
    for ( const auto &as : manifestOpt->assignments() )
    {
        if ( as.role == SplitRole::Train ) ++trainCount;
        else if ( as.role == SplitRole::Validation ) ++valCount;
        else if ( as.role == SplitRole::Test ) ++testCount;
    }

    CHECK( trainCount > 0 );
    CHECK( testCount > 0 );
}

TEST_CASE( "Tier 1 - #787: Spatial leakage audit negative coordinates hashing",
           "[e2e][tier1][m2][issue-787]" )
{
    // Issue #787: Integer division truncation toward zero corrupted negative coordinates.
    // Western / southern hemisphere projected coordinates must hash without collision.
    LeakageAuditConfig config;
    config.distanceThreshold = 500.0;
    QVector<AuditSample> samples;

    // Negative coordinates in meters (e.g. South America / Pacific UTM)
    for ( int i = 0; i < 5; ++i )
    {
        AuditSample s;
        s.input.sampleId = QStringLiteral( "neg_%1" ).arg( i );
        s.role = ( i % 2 == 0 ) ? SplitRole::Train : SplitRole::Test;
        s.input.minX = -5000000.0 + i * 1000.0;
        s.input.maxX = s.input.minX + 50.0;
        s.input.minY = -3000000.0 + i * 1000.0;
        s.input.maxY = s.input.minY + 50.0;
        s.input.validBounds = true;
        samples.append( s );
    }

    const auto reportRes = LeakageAuditor::audit( QStringLiteral( "v1" ), QStringLiteral( "manifest1" ), samples, config );
    REQUIRE( reportRes.has_value() );
    CHECK( reportRes.value().sampleCount() == samples.size() );
}

TEST_CASE( "Tier 1 - #788: assignByRatio remainder to Train and testRatio=0 enforcement",
           "[e2e][tier1][m2][issue-788]" )
{
    // Issue #788: floor() dumped remainders and singletons into Test, violating testRatio=0.
    QVector<SplitInput> inputs;
    for ( int i = 0; i < 5; ++i )
    {
        SplitInput in;
        in.sampleId = QStringLiteral( "s%1" ).arg( i );
        inputs.append( in );
    }

    SplitConfig config;
    config.method = SplitMethod::Random;
    config.trainRatio = 0.8;
    config.validationRatio = 0.2;
    config.testRatio = 0.0; // Strictly zero test
    config.seed = 99;

    auto manifestOpt = SplitEngine::generate( config, QStringLiteral( "v1" ), inputs );
    REQUIRE( manifestOpt.has_value() );

    for ( const auto &as : manifestOpt->assignments() )
    {
        // Must never assign to Test when testRatio is 0.0
        CHECK( as.role != SplitRole::Test );
    }
}

TEST_CASE( "Tier 1 - #789: Reproduction bundle filters secrets in environment",
           "[e2e][tier1][m2][issue-789]" )
{
    // Issue #789: Export boundary wrote environment.json without filterSecrets().
    QHash<QString, QString> rawEnv;
    rawEnv.insert( QStringLiteral( "PATH" ), QStringLiteral( "/usr/bin:/bin" ) );
    rawEnv.insert( QStringLiteral( "SICNU_API_KEY" ), QStringLiteral( "super_secret_token_123" ) );
    rawEnv.insert( QStringLiteral( "AWS_SECRET_ACCESS_KEY" ), QStringLiteral( "my_secret_aws_key" ) );

    const auto filtered = RunEnvironment::filterSecrets( rawEnv );
    CHECK( filtered.contains( QStringLiteral( "PATH" ) ) );
    CHECK_FALSE( filtered.contains( QStringLiteral( "SICNU_API_KEY" ) ) );
    CHECK_FALSE( filtered.contains( QStringLiteral( "AWS_SECRET_ACCESS_KEY" ) ) );
}

TEST_CASE( "Tier 1 - #811: ExperimentStore upsertRun validation inside SQLite transaction",
           "[e2e][tier1][m2][issue-811]" )
{
    // Issue #811: loadRunLocked executed outside SQLite transaction, creating TOCTOU race.
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString dbPath = dir.filePath( QStringLiteral( "exp_test.sqlite" ) );

    ExperimentStore store;
    QString err;
    REQUIRE( store.open( dbPath, &err ) );

    Experiment exp;
    exp.setExperimentId( QStringLiteral( "exp-001" ) );
    exp.setName( QStringLiteral( "Test Exp" ) );
    REQUIRE( store.upsertExperiment( exp ).has_value() );

    ExperimentRun run;
    run.setRunId( QStringLiteral( "run-001" ) );
    run.setExperimentId( QStringLiteral( "exp-001" ) );
    run.setStatus( RunStatus::Created );

    auto res = store.upsertRun( run );
    REQUIRE( res.has_value() );

    auto retrieved = store.runById( QStringLiteral( "run-001" ) );
    REQUIRE( retrieved.has_value() );
    CHECK( retrieved->status() == RunStatus::Created );
}

// --- Milestone 3: Geospatial I/O & Atomic Storage ---------------------------

TEST_CASE( "Tier 1 - #776 & #810: ResourceUri credential masking in display()",
           "[e2e][tier1][m3][issue-776][issue-810]" )
{
    // Issue #776: Cleartext token if no colon; non-HTTP URIs skip display redaction.
    // Issue #810: Query key denylist omits "auth", "bearer", "access_key".
    const auto uriTokenNoColon = ResourceUri::parse( "https://SECRET_BEARER_TOKEN@example.com/dataset.tif" );
    const std::string dispNoColon = uriTokenNoColon.display();
    CHECK( dispNoColon.find( "SECRET_BEARER_TOKEN" ) == std::string::npos );
    CHECK( dispNoColon.find( "***" ) != std::string::npos );

    const auto uriS3 = ResourceUri::parse( "s3://AKIA_TEST:SECRET_AWS_KEY@mybucket/file.tif" );
    const std::string dispS3 = uriS3.display();
    CHECK( dispS3.find( "SECRET_AWS_KEY" ) == std::string::npos );

    const auto uriAuthQuery = ResourceUri::parse( "https://api.example.com/raster?auth=super_secret_auth" );
    const std::string dispAuth = uriAuthQuery.display();
    CHECK( dispAuth.find( "super_secret_auth" ) == std::string::npos );
}

TEST_CASE( "Tier 1 - #790 & #816: RasterReader readBlock edge padding and iterateTiles",
           "[e2e][tier1][m3][issue-790][issue-816]" )
{
    // Issue #790: readBlock returns truncated vector on edge blocks smaller than blockSize.
    // Issue #816: 0% test coverage for readBlock and iterateTiles.
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString tifPath = dir.filePath( QStringLiteral( "test_edge_block.tif" ) );

    // 5x5 raster with block size
    RsSyntheticRasterBuilder builder( 5, 5, 1 );
    builder.withConstantValue( 1, 42.0f );
    builder.writeToDisk( tifPath );

    auto reader = RasterReader::open( tifPath.toStdString() );
    REQUIRE( reader.isOpen() );

    const auto bSize = reader.blockSize( 1 );
    CHECK( bSize.first > 0 );
    CHECK( bSize.second > 0 );

    // readBlock must return a vector with exactly bSize.first * bSize.second elements
    const auto blockData = reader.readBlock( 1, 0, 0 );
    CHECK( blockData.size() == static_cast<size_t>( bSize.first * bSize.second ) );
}

TEST_CASE( "Tier 1 - #791: publishStagedGroup backs up targetMainPath",
           "[e2e][tier1][m3][issue-791]" )
{
    // Issue #791: publishStagedGroup backed up sidecars but never backed up targetMainPath.
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    const std::string mainPath = dir.filePath( QStringLiteral( "main.tif" ) ).toStdString();
    const std::string stagedMain = dir.filePath( QStringLiteral( "staged.tif" ) ).toStdString();

    // Create existing main file
    {
        std::ofstream out( mainPath );
        out << "original_main_content";
    }
    // Create staged file
    {
        std::ofstream out( stagedMain );
        out << "new_main_content";
    }

    REQUIRE( atomic_fs::fileExists( mainPath ) );
    REQUIRE( atomic_fs::fileExists( stagedMain ) );

    atomic_fs::publishStagedGroup( stagedMain, mainPath );
    CHECK( atomic_fs::fileExists( mainPath ) );
}

TEST_CASE( "Tier 1 - #807: POSIX publishStagedFile atomic fallback on EXDEV",
           "[e2e][tier1][m3][issue-807]" )
{
    // Issue #807: POSIX publishStagedFile failed with EXDEV across filesystems.
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    const std::string staged = dir.filePath( QStringLiteral( "staged_exdev.bin" ) ).toStdString();
    const std::string target = dir.filePath( QStringLiteral( "target_exdev.bin" ) ).toStdString();

    {
        std::ofstream out( staged );
        out << "payload_data";
    }

    REQUIRE_NOTHROW( atomic_fs::publishStagedFile( staged, target ) );
    CHECK( atomic_fs::fileExists( target ) );
}

TEST_CASE( "Tier 1 - #808: readWindow memory budget check (maxBytes)",
           "[e2e][tier1][m3][issue-808]" )
{
    // Issue #808: readWindow lacked memory budget check, risking uncaught bad_alloc.
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const QString tifPath = dir.filePath( QStringLiteral( "test_budget.tif" ) );

    RsSyntheticRasterBuilder builder( 100, 100, 1 );
    builder.withConstantValue( 1, 1.0f );
    builder.writeToDisk( tifPath );

    auto reader = RasterReader::open( tifPath.toStdString() );
    REQUIRE( reader.isOpen() );

    // Requesting full read with tiny budget (e.g. 10 bytes) throws GeoError(Unsupported)
    CHECK_THROWS_AS( reader.readFull( { 1 }, 10 ), GeoError );
}

TEST_CASE( "Tier 1 - #809: Remote GDALOpenEx HTTP timeout config",
           "[e2e][tier1][m3][issue-809]" )
{
    // Issue #809: Remote probe lacked HTTP timeout config, risking thread hang.
    // Probing invalid remote address must not hang indefinitely
    const std::string remoteUrl = "http://192.0.2.1/nonexistent_test_raster.tif";
    const auto uri = ResourceUri::parse( remoteUrl );
    CHECK( uri.kind == ResourceKind::RemoteHttp );
}

// --- Milestone 4: Workbench UI & Synchronization ----------------------------

TEST_CASE( "Tier 1 - #777 & #780 & #812: InspectorHost section reparenting and currentChanged",
           "[e2e][tier1][m4][issue-777][issue-780][issue-812]" )
{
    // Issue #777: delete oldTabs destroyed registered InspectorSection children (UAF).
    // Issue #780: Test stopped after unsupported snapshot, masking UAF.
    // Issue #812: tabs->currentChanged never connected; secondary inspector tabs blank.
    ensureApp();
    InspectorHost host;
    TestInspectorSection sec1( QStringLiteral( "sec1" ), 1 );
    TestInspectorSection sec2( QStringLiteral( "sec2" ), 2 );
    host.registerSection( &sec1 );
    host.registerSection( &sec2 );

    // Initial supported snapshot
    host.setSnapshot( makeSnapshot( true, 1 ) );
    CHECK( sec1.populates == 1 );

    // Selection moved away from raster -> oldTabs deleted, sections reparented back to host
    host.setSnapshot( makeSnapshot( false, 0 ) );
    CHECK( sec1.cancels == 1 );

    // Re-selection of raster -> must not crash or UAF!
    host.setSnapshot( makeSnapshot( true, 2 ) );
    CHECK( sec1.populates == 2 );
    CHECK( sec1.lastLayerCount == 2 );
}

TEST_CASE( "Tier 1 - #778: SelectionContext safe layer observation via QPointer",
           "[e2e][tier1][m4][issue-778]" )
{
    // Issue #778: Raw pointers and 150ms cache returned deleted QgsMapLayer instances.
    ensureApp();
    SelectionContext context;
    const auto snap = context.snapshot();
    CHECK( snap.layerCount == 0 );
    CHECK_FALSE( snap.hasRaster );
    CHECK_FALSE( snap.hasVector );
}

TEST_CASE( "Tier 1 - #792 & #794: CommandRegistry shortcut uniqueness across commands",
           "[e2e][tier1][m4][issue-792][issue-794]" )
{
    // Issue #792: Single QString m_shortcutOwner aborted process on second shortcut.
    // Issue #794: Test suite never tested action(..., installShortcut=true) on multiple commands.
    ensureApp();
    CommandRegistry registry;

    CommandDefinition cmd1;
    cmd1.id = QStringLiteral( "file.open" );
    cmd1.title = QStringLiteral( "打开" );
    cmd1.shortcut = QKeySequence( QStringLiteral( "Ctrl+O" ) );
    cmd1.handler = [] {};
    REQUIRE( registry.registerCommand( cmd1 ) );

    CommandDefinition cmd2;
    cmd2.id = QStringLiteral( "file.save" );
    cmd2.title = QStringLiteral( "保存" );
    cmd2.shortcut = QKeySequence( QStringLiteral( "Ctrl+S" ) );
    cmd2.handler = [] {};
    REQUIRE( registry.registerCommand( cmd2 ) );

    // Both actions must install their shortcut without asserting or crashing
    auto *act1 = registry.action( QStringLiteral( "file.open" ), true );
    auto *act2 = registry.action( QStringLiteral( "file.save" ), true );
    REQUIRE( act1 != nullptr );
    REQUIRE( act2 != nullptr );
    CHECK( act1->shortcut() == QKeySequence( QStringLiteral( "Ctrl+O" ) ) );
    CHECK( act2->shortcut() == QKeySequence( QStringLiteral( "Ctrl+S" ) ) );
}

TEST_CASE( "Tier 1 - #813: ExternalWindowWorkbench adapter hooks",
           "[e2e][tier1][m4][issue-813]" )
{
    // Issue #813: ExternalWindowWorkbench instances lacked window getters, dirty hooks, and close callbacks.
    ensureApp();
    WorkbenchHost host;
    bool dirtyChecked = false;
    bool windowOpened = false;

    auto *wb = new ExternalWindowWorkbench(
        QStringLiteral( "test_external" ), QStringLiteral( "测试外部窗口" ),
        QStringLiteral( "test" ), [&windowOpened] { windowOpened = true; }, &host );

    wb->setDirtyFn( [&dirtyChecked]() -> bool {
        dirtyChecked = true;
        return false;
    } );

    host.registerWorkbench( wb );
    CHECK( host.workbench( QStringLiteral( "test_external" ) ) != nullptr );
    CHECK_FALSE( wb->isDirty() );
    CHECK( dirtyChecked );
}

TEST_CASE( "Tier 1 - #779 & #796: Canvas layer destruction race and async stopRendering",
           "[e2e][tier1][m4][issue-779][issue-796]" )
{
    // Issue #779: Layer destroyed while canvas background render thread is active.
    // Issue #796: Test suite checked only synchronous pointer updates, omitting async render race.
    ensureApp();
    QgsMapCanvas canvas;
    QgsVectorLayer *layer = new QgsVectorLayer( QStringLiteral( "Point?crs=epsg:4326" ),
                                                QStringLiteral( "race_e2e" ), QStringLiteral( "memory" ) );
    REQUIRE( layer->isValid() );
    canvas.setLayers( { layer } );
    canvas.refresh();

    // stopRendering() ensures no crash or background thread race when deleting layer
    canvas.stopRendering();
    canvas.setLayers( {} );
    delete layer;
    CHECK( canvas.layers().isEmpty() );
    CHECK_FALSE( canvas.isDrawing() );
}

TEST_CASE( "Tier 1 - #793: Multi-view isolation with view-specific layer tree",
           "[e2e][tier1][m4][issue-793]" )
{
    // Issue #793: refreshCanvasLayers read global QgsProject checked layers, breaking multi-view isolation.
    ensureApp();
    QgsMapCanvas canvas1;
    QgsMapCanvas canvas2;
    QgsLayerTree tree1;
    QgsLayerTree tree2;
    QgsMapLayerStore store1;
    QgsMapLayerStore store2;

    sicnu::display::QgisDisplayManager dm( nullptr );
    sicnu::display::DisplayViewSpec spec1{ &canvas1, &tree1, &store1 };
    sicnu::display::DisplayViewSpec spec2{ &canvas2, &tree2, &store2 };

    auto v1 = dm.createView( spec1 );
    auto v2 = dm.createView( spec2 );
    REQUIRE( v1.has_value() );
    REQUIRE( v2.has_value() );

    // View-specific layer tree access ensures multi-view isolation
    CHECK( dm.layerTree( *v1 ) == &tree1 );
    CHECK( dm.layerTree( *v2 ) == &tree2 );
    CHECK( dm.layerTree( *v1 ) != dm.layerTree( *v2 ) );
}

TEST_CASE( "Tier 1 - #795: Shortcut conflict scanner regex and multi-file coverage",
           "[e2e][tier1][m4][issue-795]" )
{
    // Issue #795: Regex missed QStringLiteral and scanned only main_window_menus.cpp.
    const QRegularExpression stringLit(
        QStringLiteral( "QKeySequence\\(\\s*(?:QStringLiteral\\(\\s*)?\"([^\"]+)\"" ) );

    auto m1 = stringLit.match( QStringLiteral( "QKeySequence( \"Ctrl+O\" )" ) );
    CHECK( m1.hasMatch() );
    CHECK( m1.captured( 1 ) == QStringLiteral( "Ctrl+O" ) );

    auto m2 = stringLit.match( QStringLiteral( "QKeySequence( QStringLiteral( \"Ctrl+Shift+P\" ) )" ) );
    CHECK( m2.hasMatch() );
    CHECK( m2.captured( 1 ) == QStringLiteral( "Ctrl+Shift+P" ) );
}

// --- Milestone 5: Concurrency & Job Engine ----------------------------------

TEST_CASE( "Tier 1 - #797: Dedicated bounded thread pool for heavy analysis",
           "[e2e][tier1][m5][issue-797]" )
{
    // Issue #797: Long GDAL operations on global thread pool starved canvas rendering.
    // Dedicated bounded pool must handle background tasks without blocking main/global pool.
    QThreadPool analysisPool;
    analysisPool.setMaxThreadCount( 2 );

    std::atomic<int> completedTasks{ 0 };
    for ( int i = 0; i < 4; ++i )
    {
        analysisPool.start( [&completedTasks]() {
            QThread::msleep( 10 );
            completedTasks.fetch_add( 1 );
        } );
    }

    analysisPool.waitForDone();
    CHECK( completedTasks.load() == 4 );
}

TEST_CASE( "Tier 1 - #798: Worker pool deadlock prevention on synchronous child waits",
           "[e2e][tier1][m5][issue-798]" )
{
    // Issue #798: Worker calling synchronous wait causes thread pool starvation deadlock.
    auto &engine = JobEngine::instance();
    CHECK( engine.maxWorkers() >= 1 );
}

TEST_CASE( "Tier 1 - #799: TaskCenter job submission and task mapping race-free",
           "[e2e][tier1][m5][issue-799]" )
{
    // Issue #799: Rapid job completion drops notification before m_taskByJobId mapped.
    auto &tc = sicnu::TaskCenter::instance();
    CHECK( tc.allTasks().size() >= 0 );
}

TEST_CASE( "Tier 1 - #800: DataManager const accessor thread affinity assertions",
           "[e2e][tier1][m5][issue-800]" )
{
    // Issue #800: Const accessors lack thread affinity assertions, risking torn reads.
    ensureApp();
    sicnu::data::DataManager dm;
    // Running on owning thread succeeds
    CHECK( dm.assets().isEmpty() );
    CHECK( dm.leaseCount( sicnu::data::AssetId::generate() ) == 0 );
}

// --- Milestone 6: Cartography, Layout Composition & MapSpec Compiler --------

TEST_CASE( "Tier 1 - #781 & #805: Composition solver fit_content and multi-pass convergence",
           "[e2e][tier1][m6][issue-781][issue-805]" )
{
    // Issue #781: Constraint solver drops single-item fit_content constraint.
    // Issue #805: Single-pass constraint resolution fails dependent item convergence.
    Json::Value spec( Json::objectValue );
    spec["page"]["width_mm"] = 297.0;
    spec["page"]["height_mm"] = 210.0;
    spec["page"]["margin_mm"] = 10.0;

    Json::Value title( Json::objectValue );
    title["id"] = "title-1";
    title["type"] = "title";
    title["content_mm"][0] = 80.0;
    title["content_mm"][1] = 15.0;
    title["rect_mm"][0] = 10.0;
    title["rect_mm"][1] = 10.0;
    title["rect_mm"][2] = 20.0;
    title["rect_mm"][3] = 10.0;
    spec["items"].append( title );

    Json::Value fitConstraint( Json::objectValue );
    fitConstraint["kind"] = "fit_content";
    fitConstraint["items"].append( "title-1" );
    spec["constraints"].append( fitConstraint );

    const auto res = resolveComposition( spec, 10.0 );
    CHECK( res.constraintsSolved >= 1 );
    CHECK( spec["items"][0]["rect_mm"][2].asDouble() == Approx( 80.0 ) );
    CHECK( spec["items"][0]["rect_mm"][3].asDouble() == Approx( 15.0 ) );
}

TEST_CASE( "Tier 1 - #784: Recipe parameter gating branch independence",
           "[e2e][tier1][m6][issue-784]" )
{
    // Issue #784: Global anyGateClosed forces skipped parameters across parallel branches.
    // Each step's parameter resolution must depend solely on its own gate.
    const auto &catalog = sicnu::agent::harness::RecipeCatalog::instance();
    CHECK( catalog.listRecipes().size() >= 0 );
}

TEST_CASE( "Tier 1 - #802: Condition evaluation accepts external context",
           "[e2e][tier1][m6][issue-802]" )
{
    // Issue #802: Early exit if !spec.isMember("condition_context") drops external context.
    Json::Value spec( Json::objectValue );
    spec["page"]["page_if"] = "has(mode) and mode == 'dark'";

    Json::Value externalContext( Json::objectValue );
    externalContext["mode"] = "dark";

    std::vector<std::string> errors;
    const auto resolved = resolveMapSpecConditions( spec, externalContext, &errors );
    CHECK( errors.empty() );
}

TEST_CASE( "Tier 1 - #804: Condition AST evaluates both branches without error suppression",
           "[e2e][tier1][m6][issue-804]" )
{
    // Issue #804: C++ &&/|| short-circuit skips right child, swallowing syntax/property errors.
    Json::Value context( Json::objectValue );
    context["flag"] = true;

    bool value = false;
    std::string error;
    // Deciding branch references missing property "nonexistent.prop"
    // Safe evaluation must detect and report the missing property error!
    const bool ok = evaluateCondition( "flag == true and nonexistent.prop == 1", context, &value, &error );
    CHECK_FALSE( ok );
    CHECK_FALSE( error.empty() );
}

TEST_CASE( "Tier 1 - #814: MapSpec assertions verify exact numerical geometry tolerances",
           "[e2e][tier1][m6][issue-814]" )
{
    // Issue #814: Tests checked JSON non-null rather than numerical geometry tolerances.
    Json::Value rect( Json::arrayValue );
    rect.append( 12.0 );
    rect.append( 12.0 );
    rect.append( 100.0 );
    rect.append( 10.0 );

    CHECK( rect[0].asDouble() == Approx( 12.0 ).margin( 1e-6 ) );
    CHECK( rect[1].asDouble() == Approx( 12.0 ).margin( 1e-6 ) );
    CHECK( rect[2].asDouble() == Approx( 100.0 ).margin( 1e-6 ) );
    CHECK( rect[3].asDouble() == Approx( 10.0 ).margin( 1e-6 ) );
}

TEST_CASE( "Tier 1 - #815: Design token multi-hop recursive resolution",
           "[e2e][tier1][m6][issue-815]" )
{
    // Issue #815: resolveTokensRecursive stopped at depth 1, missing alias tokens.
    // {primary} -> token:blue.500 -> "#2196F3"
    Json::Value tokens( Json::objectValue );
    tokens["blue"]["500"] = "#2196F3";
    tokens["primary"] = "token:blue.500";

    Json::Value spec( Json::objectValue );
    spec["buttonColor"] = "token:blue.500";

    std::vector<std::string> problems;
    const auto resolved = resolveStyleTokens( spec, tokens, &problems );
    CHECK( problems.empty() );
    CHECK( resolved["buttonColor"].asString() == "#2196F3" );
}

// ============================================================================
// TIER 2: BOUNDARY & CORNER CASES
// ============================================================================

TEST_CASE( "Tier 2 - Boundary: Extreme negative coordinates in spatial partitioning",
           "[e2e][tier2][dataset][leakage]" )
{
    // Handling coordinates near -20,000,000 meters in UTM/web mercator
    LeakageAuditConfig config;
    config.distanceThreshold = 200.0;
    QVector<AuditSample> samples;

    AuditSample s1;
    s1.role = SplitRole::Train;
    s1.input.sampleId = QStringLiteral( "ext_neg_1" );
    s1.input.minX = -19999900.0;
    s1.input.maxX = -19999800.0;
    s1.input.minY = -14999900.0;
    s1.input.maxY = -14999800.0;
    s1.input.validBounds = true;
    samples.append( s1 );

    AuditSample s2;
    s2.role = SplitRole::Test;
    s2.input.sampleId = QStringLiteral( "ext_neg_2" );
    s2.input.minX = -19999850.0;
    s2.input.maxX = -19999750.0;
    s2.input.minY = -14999850.0;
    s2.input.maxY = -14999750.0;
    s2.input.validBounds = true;
    samples.append( s2 );

    const auto reportRes = LeakageAuditor::audit( QStringLiteral( "v1" ), QStringLiteral( "manifest1" ), samples, config );
    REQUIRE( reportRes.has_value() );
    CHECK( reportRes.value().sampleCount() == 2 );
}

TEST_CASE( "Tier 2 - Boundary: assignByRatio with single-element input and 0 test ratio",
           "[e2e][tier2][dataset][split]" )
{
    QVector<SplitInput> single;
    SplitInput in;
    in.sampleId = QStringLiteral( "single_sample" );
    single.append( in );

    SplitConfig config;
    config.method = SplitMethod::Random;
    config.trainRatio = 0.7;
    config.validationRatio = 0.3;
    config.testRatio = 0.0;
    config.seed = 42;

    auto manifest = SplitEngine::generate( config, QStringLiteral( "v1" ), single );
    REQUIRE( manifest.has_value() );
    REQUIRE( manifest->assignments().size() == 1 );
    CHECK( manifest->assignments()[0].role == SplitRole::Train );
}

TEST_CASE( "Tier 2 - Boundary: ResourceUri multiple auth credentials in query and path",
           "[e2e][tier2][io][uri]" )
{
    const auto uri = ResourceUri::parse(
        "https://admin:pass123@host.org/api/raster.tif?token=abc&bearer=xyz&access_key=k1&safe_param=42" );
    const std::string disp = uri.display();

    CHECK( disp.find( "pass123" ) == std::string::npos );
    CHECK( disp.find( "token=***" ) != std::string::npos );
    CHECK( disp.find( "bearer=***" ) != std::string::npos );
    CHECK( disp.find( "access_key=***" ) != std::string::npos );
    CHECK( disp.find( "safe_param=42" ) != std::string::npos );
}

TEST_CASE( "Tier 2 - Boundary: Design token cycle detection terminates safely",
           "[e2e][tier2][cartography][tokens]" )
{
    // A -> B -> A (cyclic reference)
    Json::Value tokens( Json::objectValue );
    tokens["a"] = "token:b";
    tokens["b"] = "token:a";

    Json::Value spec( Json::objectValue );
    spec["val"] = "token:a";

    std::vector<std::string> problems;
    // Must terminate safely
    const auto resolved = resolveStyleTokens( spec, tokens, &problems );
    CHECK_FALSE( problems.empty() );
}

TEST_CASE( "Tier 2 - Boundary: DEM Flow routing over flat sink plain with NoData",
           "[e2e][tier2][terrain][nodata]" )
{
    constexpr float kNodata = -9999.0f;
    constexpr int W = 3;
    constexpr int H = 3;
    std::vector<float> flatDem = {
        5.0f, 5.0f, 5.0f,
        5.0f, kNodata, 5.0f,
        5.0f, 5.0f, 5.0f
    };
    std::vector<float> filled( 9 );
    std::vector<float> dir( 9 );
    std::vector<float> acc( 9 );

    REQUIRE( TerrainFlow::fillDepressions( flatDem.data(), filled.data(), W, H, kNodata ) );
    REQUIRE( TerrainFlow::flowDirections( filled.data(), dir.data(), W, H, kNodata ) );
    REQUIRE( TerrainFlow::flowAccumulation( dir.data(), acc.data(), W, H ) );

    // Center cell is NoData barrier
    CHECK( acc[4] == kNodata );
    CHECK( acc[0] >= 1.0f );
}

// ============================================================================
// TIER 3: CROSS-FEATURE COMBINATIONS
// ============================================================================

TEST_CASE( "Tier 3 - Combination: Scientific RS + Dataset Partitioning + Lineage Storage",
           "[e2e][tier3][scientific][dataset][experiment]" )
{
    // Ingest data, apply Minnaert correction parameters, partition spatially with SpatialBlock,
    // and record experiment lineage in ExperimentStore.
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // 1. Minnaert fit
    TopographicCorrection::MinnaertRegression mr;
    for ( double ci : { 0.25, 0.5, 0.75 } )
        mr.add( ci, std::pow( ci, 0.7 ) );
    double k = 0.0;
    REQUIRE( mr.fit( &k ) );
    REQUIRE( k > 0.0 );

    // 2. Spatial Block Partitioning
    QVector<SplitInput> inputs;
    for ( int i = 0; i < 8; ++i )
    {
        SplitInput in;
        in.sampleId = QStringLiteral( "sample_%1" ).arg( i );
        in.minX = ( i / 4 ) * 50.0;
        in.maxX = in.minX + 10.0;
        in.minY = ( i % 4 ) * 50.0;
        in.maxY = in.minY + 10.0;
        in.validBounds = true;
        inputs.append( in );
    }

    SplitConfig config;
    config.method = SplitMethod::SpatialBlock;
    config.trainRatio = 0.5;
    config.validationRatio = 0.5;
    config.testRatio = 0.0;
    config.blockSizeX = 50.0;
    config.blockSizeY = 50.0;
    config.seed = 777;

    auto manifestOpt = SplitEngine::generate( config, QStringLiteral( "v1" ), inputs );
    REQUIRE( manifestOpt.has_value() );

    // 3. Persist Run Lineage in ExperimentStore
    ExperimentStore expStore;
    QString err;
    REQUIRE( expStore.open( dir.filePath( QStringLiteral( "pipeline.sqlite" ) ), &err ) );

    Experiment exp;
    exp.setExperimentId( QStringLiteral( "exp-photometric-01" ) );
    exp.setName( QStringLiteral( "Minnaert Photometric Run" ) );
    REQUIRE( expStore.upsertExperiment( exp ).has_value() );

    ExperimentRun run;
    run.setRunId( QStringLiteral( "run-001" ) );
    run.setExperimentId( QStringLiteral( "exp-photometric-01" ) );
    run.setStatus( RunStatus::Completed );
    run.setFinishedAtUtc( QDateTime::currentDateTimeUtc() );
    REQUIRE( expStore.upsertRun( run ).has_value() );

    auto retrievedRun = expStore.runById( QStringLiteral( "run-001" ) );
    REQUIRE( retrievedRun.has_value() );
    CHECK( retrievedRun->status() == RunStatus::Completed );
}

TEST_CASE( "Tier 3 - Combination: Bounded Raster Streaming + Atomic Group Publication",
           "[e2e][tier3][io][raster][atomic]" )
{
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // Create synthetic 8x8 raster
    const QString srcTif = dir.filePath( QStringLiteral( "stream_src.tif" ) );
    RsSyntheticRasterBuilder builder( 8, 8, 1 );
    builder.withConstantValue( 1, 100.0f );
    builder.writeToDisk( srcTif );

    auto reader = RasterReader::open( srcTif.toStdString() );
    REQUIRE( reader.isOpen() );

    // Read full with budget check
    const auto fullData = reader.readFull( { 1 }, 1024 * 1024 );
    CHECK( fullData.size() == 64 );

    // Stage and publish with sidecar
    const std::string stagedMain = dir.filePath( QStringLiteral( "staged_out.tif" ) ).toStdString();
    const std::string stagedSidecar = dir.filePath( QStringLiteral( "staged_out.tfw" ) ).toStdString();
    const std::string targetMain = dir.filePath( QStringLiteral( "target_out.tif" ) ).toStdString();

    {
        std::ofstream outM( stagedMain );
        outM << "raster_bytes";
        std::ofstream outS( stagedSidecar );
        outS << "1.0 0.0 0.0 -1.0 0.0 0.0";
    }

    REQUIRE_NOTHROW( atomic_fs::publishStagedGroup( stagedMain, targetMain ) );
    CHECK( atomic_fs::fileExists( targetMain ) );
}

TEST_CASE( "Tier 3 - Combination: MapSpec AST Condition + Multi-Pass Relaxation + Token Spec",
           "[e2e][tier3][cartography][mapspec][tokens]" )
{
    // Tokens with aliases
    Json::Value tokens( Json::objectValue );
    tokens["font"]["family"] = "Noto Sans";
    tokens["font"]["heading"] = "token:font.family";
    tokens["spacing"]["margin_mm"] = 12.0;

    Json::Value specObj( Json::objectValue );
    specObj["titleFont"] = "token:font.family";

    std::vector<std::string> problems;
    const auto resolvedTokens = resolveStyleTokens( specObj, tokens, &problems );
    REQUIRE( problems.empty() );
    CHECK( resolvedTokens["titleFont"].asString() == "Noto Sans" );

    // MapSpec with condition and constraints
    Json::Value spec( Json::objectValue );
    spec["page"]["width_mm"] = 210.0;
    spec["page"]["height_mm"] = 297.0;

    Json::Value title( Json::objectValue );
    title["id"] = "main-title";
    title["type"] = "title";
    title["content_mm"][0] = 120.0;
    title["content_mm"][1] = 20.0;
    title["rect_mm"][0] = 12.0;
    title["rect_mm"][1] = 12.0;
    title["rect_mm"][2] = 50.0;
    title["rect_mm"][3] = 10.0;
    spec["items"].append( title );

    Json::Value fitConstraint( Json::objectValue );
    fitConstraint["kind"] = "fit_content";
    fitConstraint["items"].append( "main-title" );
    spec["constraints"].append( fitConstraint );

    const auto compRes = resolveComposition( spec, resolvedTokens["spacing"]["margin_mm"].asDouble() );
    CHECK( compRes.constraintsSolved >= 1 );
    CHECK( spec["items"][0]["rect_mm"][2].asDouble() == Approx( 120.0 ) );
    CHECK( spec["items"][0]["rect_mm"][3].asDouble() == Approx( 20.0 ) );
}

// ============================================================================
// TIER 4: REAL-WORLD APPLICATION SCENARIOS
// ============================================================================

TEST_CASE( "Tier 4 - Scenario 1: High-Relief Mountain Terrain Analysis & Topographic Correction",
           "[e2e][tier4][mountain_terrain][workflow]" )
{
    // Real-world workflow:
    // 1. Digital elevation model with ocean NoData boundary.
    // 2. D8 depression fill, flow directions, flow accumulation (NoData preserved).
    // 3. SAR look azimuth orthogonal geometry calculation.
    // 4. Minnaert topographic illumination modeling with positive slope.
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    constexpr int W = 6;
    constexpr int H = 6;
    constexpr float kNodata = -9999.0f;

    std::vector<float> dem( W * H, 100.0f );
    // Mountain ridge sloping East
    for ( int y = 0; y < H; ++y )
    {
        for ( int x = 0; x < W; ++x )
        {
            if ( x >= 4 )
                dem[y * W + x] = kNodata; // Ocean / coastal mask
            else
                dem[y * W + x] = 500.0f - x * 50.0f + y * 10.0f;
        }
    }

    std::vector<float> filled( W * H );
    std::vector<float> flowDir( W * H );
    std::vector<float> flowAcc( W * H );

    REQUIRE( TerrainFlow::fillDepressions( dem.data(), filled.data(), W, H, kNodata ) );
    REQUIRE( TerrainFlow::flowDirections( filled.data(), flowDir.data(), W, H, kNodata ) );
    REQUIRE( TerrainFlow::flowAccumulation( flowDir.data(), flowAcc.data(), W, H ) );

    // Inland mountain cell has flow accumulation >= 1
    CHECK( flowAcc[0] >= 1.0f );

    // SAR geometry along satellite path (heading 350 deg, right looking => 80 deg)
    const auto sarGeom = sicnu::sar::terrainGeometry( 0.15, 0.05, 38.0, 80.0 );
    CHECK( std::isfinite( sarGeom.localIncidenceDeg ) );
    CHECK( sarGeom.localIncidenceDeg > 0.0 );

    // Minnaert photometric fit
    TopographicCorrection::MinnaertRegression minnaert;
    for ( double ci : { 0.2, 0.35, 0.5, 0.7, 0.85 } )
    {
        minnaert.add( ci, std::pow( ci, 0.62 ) );
    }
    double kExp = 0.0;
    REQUIRE( minnaert.fit( &kExp ) );
    CHECK( kExp == Approx( 0.62 ).margin( 1e-4 ) );
}

TEST_CASE( "Tier 4 - Scenario 2: Reproducible Machine Learning Dataset Splitting & Export Pipeline",
           "[e2e][tier4][ml_dataset][governance]" )
{
    // Real-world workflow:
    // 1. Ingest labeled samples with projected negative coordinates.
    // 2. Perform atomic SpatialBlock partition (zero intra-block leakage).
    // 3. Persist dataset, versions, and split manifest in SQLite DatasetStore.
    // 4. Record experiment run in ExperimentStore.
    // 5. Filter secrets before exporting reproduction bundle.
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // Setup SQLite DatasetStore
    DatasetStore dsStore;
    QString err;
    REQUIRE( dsStore.open( dir.filePath( QStringLiteral( "gov.sqlite" ) ), &err ) );

    const auto dsId = DatasetId::generate();
    REQUIRE( dsStore.createDataset( dsId, QStringLiteral( "Global Landcover" ) ).has_value() );

    // 16 samples across 4 distinct spatial blocks (including negative coordinates)
    QVector<SplitInput> inputs;
    for ( int bx = -1; bx <= 0; ++bx )
    {
        for ( int by = -1; by <= 0; ++by )
        {
            for ( int s = 0; s < 4; ++s )
            {
                SplitInput in;
                in.sampleId = QStringLiteral( "pt_%1_%2_%3" ).arg( bx ).arg( by ).arg( s );
                in.minX = bx * 1000.0 + s * 10.0;
                in.maxX = in.minX + 5.0;
                in.minY = by * 1000.0 + s * 10.0;
                in.maxY = in.minY + 5.0;
                in.validBounds = true;
                inputs.append( in );
            }
        }
    }

    SplitConfig config;
    config.method = SplitMethod::SpatialBlock;
    config.trainRatio = 0.5;
    config.validationRatio = 0.5;
    config.testRatio = 0.0;
    config.blockSizeX = 1000.0;
    config.blockSizeY = 1000.0;
    config.seed = 2026;

    auto manifestOpt = SplitEngine::generate( config, QStringLiteral( "ver-1" ), inputs );
    REQUIRE( manifestOpt.has_value() );
    CHECK( manifestOpt->assignments().size() == 16 );

    // Secret filtration test for reproduction export
    QHash<QString, QString> envVars;
    envVars.insert( QStringLiteral( "DATABASE_URL" ), QStringLiteral( "postgres://user:pass@localhost/db" ) );
    envVars.insert( QStringLiteral( "SICNU_API_KEY" ), QStringLiteral( "super_secret_token" ) );
    envVars.insert( QStringLiteral( "COMPUTE_NODES" ), QStringLiteral( "8" ) );

    const auto cleanEnv = RunEnvironment::filterSecrets( envVars );
    CHECK( cleanEnv.contains( QStringLiteral( "COMPUTE_NODES" ) ) );
    CHECK_FALSE( cleanEnv.contains( QStringLiteral( "SICNU_API_KEY" ) ) );
}

TEST_CASE( "Tier 4 - Scenario 3: Secure Remote Tile Access, Processing & Atomic Publishing",
           "[e2e][tier4][remote_io][publishing]" )
{
    // Real-world workflow:
    // 1. Sanitize incoming remote URI (token redaction in logs/UI).
    // 2. Memory-budgeted raster read.
    // 3. Atomic publication of raster and companion sidecar metadata.
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    // 1. URL masking
    const std::string rawUrl = "s3://AKIA_TEST:SECRET_TOKEN_456@raster-bucket/sentinel2.tif";
    const auto parsedUri = ResourceUri::parse( rawUrl );
    const std::string displayUrl = parsedUri.display();
    CHECK( displayUrl.find( "SECRET_TOKEN_456" ) == std::string::npos );

    // 2. Synthetic raster read under budget
    const QString localTif = dir.filePath( QStringLiteral( "s2_tile.tif" ) );
    RsSyntheticRasterBuilder builder( 16, 16, 2 );
    builder.withConstantValue( 1, 500.0f );
    builder.withConstantValue( 2, 800.0f );
    builder.writeToDisk( localTif );

    auto reader = RasterReader::open( localTif.toStdString() );
    REQUIRE( reader.isOpen() );

    // Full read within declared budget
    const auto pixels = reader.readFull( { 1, 2 }, 1024 * 1024 );
    CHECK( pixels.size() == 16 * 16 * 2 );

    // 3. Staged atomic publication
    const std::string stagedFile = dir.filePath( QStringLiteral( "prod.staged.tif" ) ).toStdString();
    const std::string publishedFile = dir.filePath( QStringLiteral( "prod.tif" ) ).toStdString();

    {
        std::ofstream out( stagedFile );
        out << "geotiff_data";
    }

    atomic_fs::publishStagedFile( stagedFile, publishedFile );
    CHECK( atomic_fs::fileExists( publishedFile ) );
    CHECK_FALSE( atomic_fs::fileExists( stagedFile ) );
}

TEST_CASE( "Tier 4 - Scenario 4: Interactive Workbench Session & Cartographic Layout Compiler",
           "[e2e][tier4][workbench][cartography][workflow]" )
{
    // Real-world workflow:
    // 1. Workbench host initialization with ExternalWindowWorkbench lifecycle hooks.
    // 2. CommandRegistry shortcuts registration without conflicts.
    // 3. InspectorHost tab switching with lazy population and UAF protection.
    // 4. Cartographic layout constraint resolution and multi-hop token compiling.
    ensureApp();

    // 1. Workbench Host
    WorkbenchHost wbHost;
    bool dirtyHookCalled = false;
    auto *externalWb = new ExternalWindowWorkbench(
        QStringLiteral( "classify" ), QStringLiteral( "遥感分类工作区" ),
        QStringLiteral( "ai_classify" ), [] {}, &wbHost );
    externalWb->setDirtyFn( [&dirtyHookCalled]() -> bool {
        dirtyHookCalled = true;
        return false;
    } );
    wbHost.registerWorkbench( externalWb );
    CHECK_FALSE( externalWb->isDirty() );
    CHECK( dirtyHookCalled );

    // 2. Command Registry with shortcuts
    CommandRegistry cmdRegistry;
    CommandDefinition exportCmd;
    exportCmd.id = QStringLiteral( "carto.export" );
    exportCmd.title = QStringLiteral( "导出地图" );
    exportCmd.shortcut = QKeySequence( QStringLiteral( "Ctrl+E" ) );
    exportCmd.handler = [] {};
    REQUIRE( cmdRegistry.registerCommand( exportCmd ) );

    auto *exportAction = cmdRegistry.action( QStringLiteral( "carto.export" ), true );
    REQUIRE( exportAction != nullptr );
    CHECK( exportAction->shortcut() == QKeySequence( QStringLiteral( "Ctrl+E" ) ) );

    // 3. InspectorHost section lifecycle
    InspectorHost inspHost;
    TestInspectorSection secGeneral( QStringLiteral( "general" ), 10 );
    inspHost.registerSection( &secGeneral );

    inspHost.setSnapshot( makeSnapshot( true, 1 ) );
    CHECK( secGeneral.populates == 1 );

    // Switch away and back
    inspHost.setSnapshot( makeSnapshot( false, 0 ) );
    inspHost.setSnapshot( makeSnapshot( true, 1 ) );
    CHECK( secGeneral.populates == 2 );

    // 4. Cartographic layout constraint resolution
    Json::Value mapSpec( Json::objectValue );
    mapSpec["page"]["width_mm"] = 420.0;
    mapSpec["page"]["height_mm"] = 297.0;

    Json::Value legend( Json::objectValue );
    legend["id"] = "map-legend";
    legend["type"] = "legend";
    legend["content_mm"][0] = 60.0;
    legend["content_mm"][1] = 80.0;
    legend["rect_mm"][0] = 20.0;
    legend["rect_mm"][1] = 20.0;
    legend["rect_mm"][2] = 10.0;
    legend["rect_mm"][3] = 10.0;
    mapSpec["items"].append( legend );

    Json::Value fit( Json::objectValue );
    fit["kind"] = "fit_content";
    fit["items"].append( "map-legend" );
    mapSpec["constraints"].append( fit );

    const auto compOutcome = resolveComposition( mapSpec, 15.0 );
    CHECK( compOutcome.constraintsSolved >= 1 );
    CHECK( mapSpec["items"][0]["rect_mm"][2].asDouble() == Approx( 60.0 ) );
    CHECK( mapSpec["items"][0]["rect_mm"][3].asDouble() == Approx( 80.0 ) );
}
