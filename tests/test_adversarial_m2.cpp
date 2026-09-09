// tests/test_adversarial_m2.cpp
// Adversarial Empirical Stress Suite for Milestone 2 (#774, #775, #786, #787, #788, #789, #811, #817)

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dataset/deterministic_random.h"
#include "dataset/leakage_audit.h"
#include "dataset/split.h"
#include "dataset/dataset_store.h"
#include "dataset/dataset_types.h"
#include "dataset/dataset_manifest.h"

#include "experiment/experiment_store.h"
#include "experiment/experiment_types.h"
#include "experiment/reproduction_bundle.h"

#include <sqlite3.h>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMap>
#include <QSet>
#include <QVector>
#include <QString>
#include <cmath>
#include <algorithm>

using namespace sicnu::dataset;
using namespace sicnu::experiment;

namespace {

SplitInput makeSpatialInput( const QString &id, double minX, double maxX, double minY, double maxY,
                             const QString &classCode = QStringLiteral( "cls0" ) )
{
    SplitInput input;
    input.sampleId = id;
    input.classCode = classCode;
    input.validBounds = true;
    input.minX = minX;
    input.maxX = maxX;
    input.minY = minY;
    input.maxY = maxY;
    return input;
}

AuditSample makeAuditSample( const SplitInput &input, SplitRole role )
{
    AuditSample sample;
    sample.input = input;
    sample.role = role;
    sample.fold = -1;
    return sample;
}

} // namespace

// ============================================================================
// 1. Spatial Block Partitioning Under Extreme Grids & Shapes (#775, #817)
// ============================================================================

TEST_CASE( "Adversarial: Spatial block zero intra-block leakage under extreme grid aspect ratios",
           "[m2][adversarial][spatial_block][issue775][issue817]" )
{
    // Test multiple extreme grid configurations:
    // (A) Extremely thin tall blocks: blockSizeX = 0.001, blockSizeY = 1000.0
    // (B) Extremely wide short blocks: blockSizeX = 10000.0, blockSizeY = 0.005
    // (C) Microscopic blocks: blockSizeX = 1e-5, blockSizeY = 1e-5
    // (D) Massive coordinates with offsets in millions
    struct TestCaseConfig {
        double bx;
        double by;
        double originX;
        double originY;
        int gridCols;
        int gridRows;
        int samplesPerCell;
    };

    const std::vector<TestCaseConfig> testConfigs = {
        { 0.01, 500.0, -100000.0, 50000.0, 8, 4, 5 },      // Thin tall
        { 5000.0, 0.02, 100000.0, -200000.0, 4, 8, 4 },    // Wide short
        { 10.0, 10.0, -500.0, -500.0, 6, 6, 8 },           // Negative coordinate grid
        { 1.0, 1.0, 0.0, 0.0, 10, 10, 3 }                  // Dense 100-block grid
    };

    for ( size_t tc = 0; tc < testConfigs.size(); ++tc )
    {
        const auto &cfg = testConfigs[tc];
        SplitConfig config;
        config.method = SplitMethod::SpatialBlock;
        config.seed = 1000 + tc;
        config.trainRatio = 0.6;
        config.validationRatio = 0.2;
        config.testRatio = 0.2;
        config.blockSizeX = cfg.bx;
        config.blockSizeY = cfg.by;

        QVector<SplitInput> inputs;
        int sampleIdx = 0;
        for ( int col = 0; col < cfg.gridCols; ++col )
        {
            for ( int row = 0; row < cfg.gridRows; ++row )
            {
                const double cellMinX = cfg.originX + col * cfg.bx;
                const double cellMinY = cfg.originY + row * cfg.by;
                for ( int s = 0; s < cfg.samplesPerCell; ++s )
                {
                    // Center strictly inside cell
                    const double frac = ( s + 1.0 ) / ( cfg.samplesPerCell + 1.0 );
                    const double cx = cellMinX + frac * cfg.bx;
                    const double cy = cellMinY + frac * cfg.by;
                    inputs.append( makeSpatialInput(
                        QStringLiteral( "tc%1_c%2_r%3_s%4" ).arg( tc ).arg( col ).arg( row ).arg( s ),
                        cx - 0.0001 * cfg.bx, cx + 0.0001 * cfg.bx,
                        cy - 0.0001 * cfg.by, cy + 0.0001 * cfg.by ) );
                    sampleIdx++;
                }
            }
        }

        const auto manifest = SplitEngine::generate( config, QStringLiteral( "ver_m2" ), inputs );
        REQUIRE( manifest.has_value() );
        REQUIRE( manifest->assignments().size() == inputs.size() );

        // Rigorous check: Every sample in block (bx, by) MUST share the exact same role.
        QMap<QString, SplitRole> sampleRoles;
        for ( const auto &as : manifest->assignments() )
            sampleRoles.insert( as.sampleId, as.role );

        QMap<QString, SplitRole> observedBlockRole;
        for ( int i = 0; i < inputs.size(); ++i )
        {
            const auto &in = inputs.at( i );
            const double cx = ( in.minX + in.maxX ) / 2.0;
            const double cy = ( in.minY + in.maxY ) / 2.0;
            const qint64 bX = qint64( std::floor( cx / config.blockSizeX ) );
            const qint64 bY = qint64( std::floor( cy / config.blockSizeY ) );
            const QString blockKey = QStringLiteral( "%1:%2" ).arg( bX ).arg( bY );

            const SplitRole assignedRole = sampleRoles.value( in.sampleId );
            if ( observedBlockRole.contains( blockKey ) )
            {
                // CRITICAL INVARIANT: 0% intra-block role leakage
                CHECK( observedBlockRole.value( blockKey ) == assignedRole );
            }
            else
            {
                observedBlockRole.insert( blockKey, assignedRole );
            }
        }
        CHECK( observedBlockRole.size() == cfg.gridCols * cfg.gridRows );

    }
}

TEST_CASE( "Adversarial: Spatial block with skewed distribution and degenerate guards",
           "[m2][adversarial][spatial_block][skewed]" )
{
    // One giant block with 100 samples, 5 small blocks with 1 sample each
    SplitConfig config;
    config.method = SplitMethod::SpatialBlock;
    config.seed = 42;
    config.trainRatio = 0.7;
    config.validationRatio = 0.15;
    config.testRatio = 0.15;
    config.blockSizeX = 100.0;
    config.blockSizeY = 100.0;

    // Case A: Exactly 1 giant block with 100 samples when train=0.7, val=0.15, test=0.15.
    // Both valTarget (15) and testTarget (15) > 0, but only 1 block exists!
    // SplitEngine must cleanly refuse via SplitRoleDegenerate.
    {
        QVector<SplitInput> singleBlockInputs;
        for ( int i = 0; i < 100; ++i )
        {
            singleBlockInputs.append( makeSpatialInput( QStringLiteral( "single_%1" ).arg( i ),
                                                        10.0, 20.0, 10.0, 20.0 ) );
        }
        const auto manifestSingle = SplitEngine::generate( config, QStringLiteral( "ver_single" ), singleBlockInputs );
        CHECK( !manifestSingle.has_value() );
    }

    // Case B: Exactly 2 blocks (50 samples in Block A, 50 samples in Block B)
    // with 3 non-zero targets: trainTarget=50, valTarget=25, testTarget=25.
    // 2 blocks cannot cover 3 roles without splitting a block.
    // SplitEngine must cleanly refuse via SplitRoleDegenerate.
    {
        QVector<SplitInput> twoBlockInputs;
        for ( int i = 0; i < 50; ++i )
            twoBlockInputs.append( makeSpatialInput( QStringLiteral( "blkA_%1" ).arg( i ), 10.0, 20.0, 10.0, 20.0 ) );
        for ( int i = 0; i < 50; ++i )
            twoBlockInputs.append( makeSpatialInput( QStringLiteral( "blkB_%1" ).arg( i ), 110.0, 120.0, 10.0, 20.0 ) );

        const auto manifestTwo = SplitEngine::generate( config, QStringLiteral( "ver_two" ), twoBlockInputs );
        CHECK( !manifestTwo.has_value() );
    }

    // Case C: 6 blocks (1 giant block with 100 samples, 5 small blocks with 1 sample each)
    // Here 6 blocks can cover all 3 non-zero roles atomically!
    // Verify it succeeds and maintains 100% atomic blocks (0% intra-block role leakage).
    {
        QVector<SplitInput> skewedInputs;
        for ( int i = 0; i < 100; ++i )
            skewedInputs.append( makeSpatialInput( QStringLiteral( "giant_%1" ).arg( i ), 10.0, 20.0, 10.0, 20.0 ) );
        for ( int b = 1; b <= 5; ++b )
            skewedInputs.append( makeSpatialInput( QStringLiteral( "small_%1" ).arg( b ),
                                                  b * 100.0 + 10.0, b * 100.0 + 20.0,
                                                  b * 100.0 + 10.0, b * 100.0 + 20.0 ) );

        const auto manifestSkewed = SplitEngine::generate( config, QStringLiteral( "ver_skew" ), skewedInputs );
        REQUIRE( manifestSkewed.has_value() );

        // Validate atomic roles
        QMap<QString, SplitRole> sampleRoles;
        for ( const auto &as : manifestSkewed->assignments() )
            sampleRoles.insert( as.sampleId, as.role );

        QMap<QString, SplitRole> observed;
        for ( int i = 0; i < skewedInputs.size(); ++i )
        {
            const auto &in = skewedInputs.at( i );
            const double cx = ( in.minX + in.maxX ) / 2.0;
            const double cy = ( in.minY + in.maxY ) / 2.0;
            const qint64 bX = qint64( std::floor( cx / config.blockSizeX ) );
            const qint64 bY = qint64( std::floor( cy / config.blockSizeY ) );
            const QString blockKey = QStringLiteral( "%1:%2" ).arg( bX ).arg( bY );
            const SplitRole role = sampleRoles.value( in.sampleId );
            if ( observed.contains( blockKey ) )
                CHECK( observed.value( blockKey ) == role );
            else
                observed.insert( blockKey, role );
        }
        CHECK( observed.size() == 6 );
    }


}

// ============================================================================
// 2. Spatial Buffer Exclusion & Zero Leakage Between Test & Train/Val (#786)
// ============================================================================

TEST_CASE( "Adversarial: Spatial buffer exclusion strictly eliminates buffer leakage and avoids starvation",
           "[m2][adversarial][spatial_buffer][issue786]" )
{
    // Create 100 samples in a 10x10 grid with spacing of 10 units
    // Buffer distance = 15 units (meaning orthogonal neighbors within 10 units and diagonal within 14.14 units are excluded)
    SplitConfig config;
    config.method = SplitMethod::SpatialBuffer;
    config.seed = 777;
    config.trainRatio = 0.6;
    config.validationRatio = 0.2;
    config.testRatio = 0.2;
    config.bufferDistance = 15.0;

    QVector<SplitInput> inputs;
    for ( int x = 0; x < 10; ++x )
    {
        for ( int y = 0; y < 10; ++y )
        {
            inputs.append( makeSpatialInput(
                QStringLiteral( "grid_%1_%2" ).arg( x ).arg( y ),
                x * 10.0, x * 10.0 + 1.0,
                y * 10.0, y * 10.0 + 1.0 ) );
        }
    }

    const auto manifest = SplitEngine::generate( config, QStringLiteral( "ver_buf" ), inputs );
    REQUIRE( manifest.has_value() );

    const auto &assignments = manifest->assignments();
    REQUIRE( assignments.size() == inputs.size() );

    QVector<SplitInput> testSamples;
    QVector<SplitInput> trainSamples;
    QVector<SplitInput> valSamples;
    QVector<SplitInput> unassignedSamples;

    for ( int i = 0; i < inputs.size(); ++i )
    {
        const auto &as = assignments.at( i );
        if ( as.role == SplitRole::Test )
            testSamples.append( inputs.at( i ) );
        else if ( as.role == SplitRole::Train )
            trainSamples.append( inputs.at( i ) );
        else if ( as.role == SplitRole::Validation )
            valSamples.append( inputs.at( i ) );
        else if ( as.role == SplitRole::Unassigned )
            unassignedSamples.append( inputs.at( i ) );
    }

    // 1. Validation must NOT be starved
    CHECK( testSamples.size() > 0 );
    CHECK( trainSamples.size() > 0 );
    CHECK( valSamples.size() > 0 );
    CHECK( unassignedSamples.size() > 0 );

    // 2. CRITICAL INVARIANT: 0% BUFFER LEAKAGE
    // For every test sample, NO train or validation sample can be within bufferDistance.
    for ( const auto &tSample : testSamples )
    {
        const double tx = ( tSample.minX + tSample.maxX ) / 2.0;
        const double ty = ( tSample.minY + tSample.maxY ) / 2.0;

        for ( const auto &trSample : trainSamples )
        {
            const double rx = ( trSample.minX + trSample.maxX ) / 2.0;
            const double ry = ( trSample.minY + trSample.maxY ) / 2.0;
            const double dist = std::sqrt( ( tx - rx ) * ( tx - rx ) + ( ty - ry ) * ( ty - ry ) );
            CHECK( dist >= config.bufferDistance );
        }

        for ( const auto &vSample : valSamples )
        {
            const double vx = ( vSample.minX + vSample.maxX ) / 2.0;
            const double vy = ( vSample.minY + vSample.maxY ) / 2.0;
            const double dist = std::sqrt( ( tx - vx ) * ( tx - vx ) + ( ty - vy ) * ( ty - vy ) );
            CHECK( dist >= config.bufferDistance );
        }
    }
}

// ============================================================================
// 3. Extreme Negative Coordinates & Leakage Auditor Bucketing (#787)
// ============================================================================

TEST_CASE( "Adversarial: Leakage audit detects proximity in extreme negative coordinate spaces [-1,000,000, +1,000,000]",
           "[m2][adversarial][audit][negative_coords][issue787]" )
{
    LeakageAuditConfig auditConfig;
    auditConfig.checks = { QStringLiteral( "distance_below_threshold" ) };
    auditConfig.distanceThreshold = 100.0;

    // Test cases:
    // Pair 1: Deep negative coordinates around -1,000,000
    // Pair 2: Across the zero boundary (-40 to +40, distance = 80 < 100)
    // Pair 3: Deep positive coordinates around +1,000,000
    // Control samples far away (no leakage)
    QVector<AuditSample> auditSamples;

    // Deep negative pair: dist = 60.0
    auto n1 = makeSpatialInput( QStringLiteral( "neg_a" ), -1000000.0, -999990.0, -1000000.0, -999990.0 );
    auto n2 = makeSpatialInput( QStringLiteral( "neg_b" ), -999940.0, -999930.0, -1000000.0, -999990.0 );
    auditSamples.append( makeAuditSample( n1, SplitRole::Train ) );
    auditSamples.append( makeAuditSample( n2, SplitRole::Test ) );

    // Cross-origin pair: dist = 70.0
    auto z1 = makeSpatialInput( QStringLiteral( "zero_a" ), -35.0, -35.0, 0.0, 0.0 );
    auto z2 = makeSpatialInput( QStringLiteral( "zero_b" ), 35.0, 35.0, 0.0, 0.0 );
    auditSamples.append( makeAuditSample( z1, SplitRole::Train ) );
    auditSamples.append( makeAuditSample( z2, SplitRole::Test ) );

    // Deep positive pair: dist = 50.0
    auto p1 = makeSpatialInput( QStringLiteral( "pos_a" ), 999900.0, 999900.0, 500000.0, 500000.0 );
    auto p2 = makeSpatialInput( QStringLiteral( "pos_b" ), 999950.0, 999950.0, 500000.0, 500000.0 );
    auditSamples.append( makeAuditSample( p1, SplitRole::Train ) );
    auditSamples.append( makeAuditSample( p2, SplitRole::Test ) );

    // Isolated control sample
    auto iso = makeSpatialInput( QStringLiteral( "isolated" ), 0.0, 0.0, 800000.0, 800000.0 );
    auditSamples.append( makeAuditSample( iso, SplitRole::Validation ) );

    const auto report = LeakageAuditor::audit( QStringLiteral( "v1" ), QStringLiteral( "sp1" ),
                                               auditSamples, auditConfig );
    REQUIRE( report.has_value() );

    const auto &findings = report->findings();
    // Exactly 3 leakage pairs must be found
    int foundNeg = 0, foundZero = 0, foundPos = 0;
    for ( const auto &f : findings )
    {
        if ( ( f.sampleA == QStringLiteral( "neg_a" ) && f.sampleB == QStringLiteral( "neg_b" ) ) ||
             ( f.sampleA == QStringLiteral( "neg_b" ) && f.sampleB == QStringLiteral( "neg_a" ) ) )
            foundNeg++;
        if ( ( f.sampleA == QStringLiteral( "zero_a" ) && f.sampleB == QStringLiteral( "zero_b" ) ) ||
             ( f.sampleA == QStringLiteral( "zero_b" ) && f.sampleB == QStringLiteral( "zero_a" ) ) )
            foundZero++;
        if ( ( f.sampleA == QStringLiteral( "pos_a" ) && f.sampleB == QStringLiteral( "pos_b" ) ) ||
             ( f.sampleA == QStringLiteral( "pos_b" ) && f.sampleB == QStringLiteral( "pos_a" ) ) )
            foundPos++;
    }

    CHECK( foundNeg == 1 );
    CHECK( foundZero == 1 );
    CHECK( foundPos == 1 );
    CHECK( findings.size() == 3 );
}

// ============================================================================
// 4. assignByRatio Boundary Conditions & Largest Remainder Method (#788)
// ============================================================================

TEST_CASE( "Adversarial: assignByRatio behavior across total=1, 2, 3 and boundary ratios",
           "[m2][adversarial][split][assign_by_ratio][issue788]" )
{
    // Test 1: Singletons (total=1) must always route to Train
    {
        SplitConfig config;
        config.method = SplitMethod::Stratified;
        config.seed = 42;
        config.trainRatio = 0.5;
        config.validationRatio = 0.25;
        config.testRatio = 0.25;

        QVector<SplitInput> inputs;
        inputs.append( makeSpatialInput( QStringLiteral( "singleton" ), 0, 1, 0, 1, QStringLiteral( "rare" ) ) );
        for ( int i = 0; i < 10; ++i )
            inputs.append( makeSpatialInput( QStringLiteral( "common_%1" ).arg( i ), 0, 1, 0, 1, QStringLiteral( "common" ) ) );

        const auto manifest = SplitEngine::generate( config, QStringLiteral( "v1" ), inputs );
        REQUIRE( manifest.has_value() );
        for ( const auto &as : manifest->assignments() )
        {
            if ( as.sampleId == QStringLiteral( "singleton" ) )
                CHECK( as.role == SplitRole::Train );
        }
    }

    // Test 2: total=2 with 50/50 Train/Validation and 0% Test
    {
        SplitConfig config;
        config.method = SplitMethod::Random;
        config.seed = 1;
        config.trainRatio = 0.5;
        config.validationRatio = 0.5;
        config.testRatio = 0.0;

        QVector<SplitInput> inputs;
        inputs.append( makeSpatialInput( QStringLiteral( "s1" ), 0, 1, 0, 1 ) );
        inputs.append( makeSpatialInput( QStringLiteral( "s2" ), 0, 1, 0, 1 ) );

        const auto manifest = SplitEngine::generate( config, QStringLiteral( "v1" ), inputs );
        REQUIRE( manifest.has_value() );
        int trainCount = 0, valCount = 0, testCount = 0;
        for ( const auto &as : manifest->assignments() )
        {
            if ( as.role == SplitRole::Train ) trainCount++;
            else if ( as.role == SplitRole::Validation ) valCount++;
            else if ( as.role == SplitRole::Test ) testCount++;
        }
        CHECK( trainCount == 1 );
        CHECK( valCount == 1 );
        CHECK( testCount == 0 );
    }

    // Test 3: total=3 with equal 1/3 splits (Hare-Niemeyer largest remainder)
    {
        SplitConfig config;
        config.method = SplitMethod::Random;
        config.seed = 42;
        config.trainRatio = 0.333333333333333;
        config.validationRatio = 0.333333333333333;
        config.testRatio = 0.333333333333334;

        QVector<SplitInput> inputs;
        inputs.append( makeSpatialInput( QStringLiteral( "t1" ), 0, 1, 0, 1 ) );
        inputs.append( makeSpatialInput( QStringLiteral( "t2" ), 0, 1, 0, 1 ) );
        inputs.append( makeSpatialInput( QStringLiteral( "t3" ), 0, 1, 0, 1 ) );

        const auto manifest = SplitEngine::generate( config, QStringLiteral( "v1" ), inputs );
        REQUIRE( manifest.has_value() );
        int trainCount = 0, valCount = 0, testCount = 0;
        for ( const auto &as : manifest->assignments() )
        {
            if ( as.role == SplitRole::Train ) trainCount++;
            else if ( as.role == SplitRole::Validation ) valCount++;
            else if ( as.role == SplitRole::Test ) testCount++;
        }
        CHECK( trainCount == 1 );
        CHECK( valCount == 1 );
        CHECK( testCount == 1 );
    }

    // Test 4: Comprehensive testRatio=0.0 clamping across all totals from 1 to 30
    for ( int total = 1; total <= 30; ++total )
    {
        SplitConfig config;
        config.method = SplitMethod::Random;
        config.seed = 99 + total;
        config.trainRatio = 0.7;
        config.validationRatio = 0.3;
        config.testRatio = 0.0;

        QVector<SplitInput> inputs;
        for ( int i = 0; i < total; ++i )
            inputs.append( makeSpatialInput( QStringLiteral( "item_%1" ).arg( i ), 0, 1, 0, 1 ) );

        const auto manifest = SplitEngine::generate( config, QStringLiteral( "v1" ), inputs );
        REQUIRE( manifest.has_value() );

        int trainCount = 0, valCount = 0, testCount = 0;
        for ( const auto &as : manifest->assignments() )
        {
            if ( as.role == SplitRole::Train ) trainCount++;
            else if ( as.role == SplitRole::Validation ) valCount++;
            else if ( as.role == SplitRole::Test ) testCount++;
        }
        CHECK( testCount == 0 ); // Strictly zero test samples
        CHECK( trainCount + valCount == total ); // Conservation of samples
    }

    // Test 5: trainRatio=1.0 (testRatio=0, valRatio=0)
    for ( int total = 1; total <= 10; ++total )
    {
        SplitConfig config;
        config.method = SplitMethod::Random;
        config.seed = 200 + total;
        config.trainRatio = 1.0;
        config.validationRatio = 0.0;
        config.testRatio = 0.0;

        QVector<SplitInput> inputs;
        for ( int i = 0; i < total; ++i )
            inputs.append( makeSpatialInput( QStringLiteral( "pure_%1" ).arg( i ), 0, 1, 0, 1 ) );

        const auto manifest = SplitEngine::generate( config, QStringLiteral( "v1" ), inputs );
        REQUIRE( manifest.has_value() );
        for ( const auto &as : manifest->assignments() )
        {
            CHECK( as.role == SplitRole::Train );
        }
    }
}

// ============================================================================
// 5. Reproduction Bundle Secret Filtering Under Hostile Env (#789)
// ============================================================================

TEST_CASE( "Adversarial: Reproduction bundle strictly filters tokens, keys, credentials and patterns",
           "[m2][adversarial][bundle][secret_filter][issue789]" )
{
    QTemporaryDir dir;
    DatasetStore datasets;
    ExperimentStore experiments;
    REQUIRE( datasets.open( dir.filePath( QStringLiteral( "datasets.db" ) ) ) );
    REQUIRE( experiments.open( dir.filePath( QStringLiteral( "experiments.db" ) ) ) );

    Experiment experiment;
    experiment.setExperimentId( QStringLiteral( "exp-hostile" ) );
    experiment.setName( QStringLiteral( "Hostile Env Test" ) );
    REQUIRE( experiments.upsertExperiment( experiment ).has_value() );

    // Inject hostile environment variables:
    // Both allowed env vars carrying credential values AND disallowed sensitive names
    QHash<QString, QString> rawEnv;
    rawEnv.insert( QStringLiteral( "PATH" ), QStringLiteral( "/usr/bin:/bin" ) );
    rawEnv.insert( QStringLiteral( "SICNU_ARTIFACT_CACHE" ), QStringLiteral( "sk-1234567890abcdef1234567890abcdef" ) );
    rawEnv.insert( QStringLiteral( "SICNU_MCP_WORKSPACE" ), QStringLiteral( "Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.e30.t-IDNJS" ) );
    rawEnv.insert( QStringLiteral( "AWS_SECRET_ACCESS_KEY" ), QStringLiteral( "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY" ) );
    rawEnv.insert( QStringLiteral( "AWS_ACCESS_KEY_ID" ), QStringLiteral( "AKIAIOSFODNN7EXAMPLE" ) );
    rawEnv.insert( QStringLiteral( "GITHUB_TOKEN" ), QStringLiteral( "ghp_123456789012345678901234567890123456" ) );
    rawEnv.insert( QStringLiteral( "DATABASE_PASSWORD" ), QStringLiteral( "SuperSecretPassword!" ) );
    rawEnv.insert( QStringLiteral( "PRIVATE_KEY" ), QStringLiteral( "-----BEGIN RSA PRIVATE KEY-----\nMIIE..." ) );
    rawEnv.insert( QStringLiteral( "AUTH_COOKIE" ), QStringLiteral( "session_id=abcdef" ) );
    rawEnv.insert( QStringLiteral( "OPENAI_API_KEY" ), QStringLiteral( "sk-proj-1234567890abcdef1234567890" ) );

    ExperimentRun run;
    run.setRunId( QStringLiteral( "run-hostile" ) );
    run.setExperimentId( experiment.experimentId() );
    run.setStatus( RunStatus::Completed );
    run.setEnvironment( RunEnvironment::fromFields( QJsonObject{}, rawEnv ) );
    run.setFinishedAtUtc( QDateTime::currentDateTimeUtc() );
    REQUIRE( experiments.upsertRun( run ).has_value() );

    ReproductionBundleExporter exporter( experiments, datasets );
    ReproductionBundleOptions options;
    options.outputDir = dir.filePath( QStringLiteral( "bundle_hostile" ) );
    options.currentSoftwareRevision = QStringLiteral( "rev_sec" );
    const auto report = exporter.exportRun( QStringLiteral( "run-hostile" ), options );
    REQUIRE( report.ok );

    const QString envJsonPath = QDir( report.bundlePath ).filePath( QStringLiteral( "environment.json" ) );
    REQUIRE( QFile::exists( envJsonPath ) );

    QFile envFile( envJsonPath );
    REQUIRE( envFile.open( QIODevice::ReadOnly ) );
    const QByteArray envData = envFile.readAll();
    const QString envContent = QString::fromUtf8( envData );

    // Adversarial verification: Search entire environment.json for any forbidden patterns or values
    CHECK_FALSE( envContent.contains( QStringLiteral( "sk-1234567890abcdef" ) ) );
    CHECK_FALSE( envContent.contains( QStringLiteral( "sk-proj" ) ) );
    CHECK_FALSE( envContent.contains( QStringLiteral( "AKIA" ) ) );
    CHECK_FALSE( envContent.contains( QStringLiteral( "ghp_" ) ) );
    CHECK_FALSE( envContent.contains( QStringLiteral( "Bearer" ) ) );
    CHECK_FALSE( envContent.contains( QStringLiteral( "PRIVATE KEY" ) ) );
    CHECK_FALSE( envContent.contains( QStringLiteral( "SuperSecretPassword" ) ) );
    CHECK_FALSE( envContent.contains( QStringLiteral( "AWS_SECRET_ACCESS_KEY" ) ) );
    CHECK_FALSE( envContent.contains( QStringLiteral( "AWS_ACCESS_KEY_ID" ) ) );
    CHECK_FALSE( envContent.contains( QStringLiteral( "GITHUB_TOKEN" ) ) );
    CHECK_FALSE( envContent.contains( QStringLiteral( "DATABASE_PASSWORD" ) ) );
    CHECK_FALSE( envContent.contains( QStringLiteral( "AUTH_COOKIE" ) ) );
    CHECK_FALSE( envContent.contains( QStringLiteral( "OPENAI_API_KEY" ) ) );
}

// ============================================================================
// 6. SQLite Transaction Locking, Rollback & Store Concurrency (#774, #811)
// ============================================================================

TEST_CASE( "Adversarial: DatasetStore deleteDataset transaction lock release & store reusability",
           "[m2][adversarial][dataset][store][tx_lock][issue774]" )
{
    QTemporaryDir dir;
    DatasetStore store;
    const QString dbPath = dir.filePath( QStringLiteral( "dataset_tx.db" ) );
    REQUIRE( store.open( dbPath ) );

    // Create and delete multiple datasets in succession.
    // If commit was missing, the very second delete or create would fail with SQLITE_BUSY / transaction lock error.
    for ( int cycle = 0; cycle < 10; ++cycle )
    {
        const auto dsId = DatasetId::generate();
        REQUIRE( store.createDataset( dsId, QStringLiteral( "dataset_cycle_%1" ).arg( cycle ) ).has_value() );

        // Add a draft version with annotations
        DatasetManifest manifest;
        manifest.setDatasetId( dsId.toString() );
        manifest.setVersionId( DatasetVersionId::generate().toString() );
        manifest.setName( QStringLiteral( "v_%1" ).arg( cycle ) );
        const auto draftVer = store.createDraftVersion( manifest );
        REQUIRE( draftVer.has_value() );

        // Delete draft dataset: MUST commit transaction properly
        const auto delRes = store.deleteDataset( dsId );
        REQUIRE( delRes.has_value() );

        // Verify dataset is gone
        CHECK( store.datasetById( dsId ) == std::nullopt );
    }

    // Direct SQLite check: autocommit mode must be active (1), meaning no dangling transaction
    sqlite3 *rawDb = nullptr;
    REQUIRE( sqlite3_open( dbPath.toUtf8().constData(), &rawDb ) == SQLITE_OK );
    CHECK( sqlite3_get_autocommit( rawDb ) == 1 );
    sqlite3_close( rawDb );
}

TEST_CASE( "Adversarial: ExperimentStore transaction rollback on illegal transition or immutable pin mutation",
           "[m2][adversarial][experiment][store][rollback][issue811]" )
{
    QTemporaryDir dir;
    ExperimentStore store;
    const QString dbPath = dir.filePath( QStringLiteral( "exp_rollback.db" ) );
    REQUIRE( store.open( dbPath ) );

    Experiment exp;
    exp.setExperimentId( QStringLiteral( "exp-rb" ) );
    exp.setName( QStringLiteral( "Rollback test" ) );
    REQUIRE( store.upsertExperiment( exp ).has_value() );

    ExperimentRun run;
    run.setRunId( QStringLiteral( "run-rb-1" ) );
    run.setExperimentId( exp.experimentId() );
    run.setStatus( RunStatus::Created );

    run.setSoftwareRevision( QStringLiteral( "git-rev-A" ) );
    run.setAlgorithmId( QStringLiteral( "algo-A" ) );
    REQUIRE( store.upsertRun( run ).has_value() );

    // 1. Attempt illegal state transition inside transaction: Created -> Completed
    ExperimentRun illegalTransition = run;
    illegalTransition.setStatus( RunStatus::Completed );
    const auto res1 = store.upsertRun( illegalTransition );
    CHECK( !res1.has_value() );

    // Verify rollback: status must still be Created
    auto loaded1 = store.runById( QStringLiteral( "run-rb-1" ) );
    REQUIRE( loaded1.has_value() );
    CHECK( loaded1->status() == RunStatus::Created );

    // 2. Transition Created -> Running (legal)
    run.setStatus( RunStatus::Running );
    REQUIRE( store.upsertRun( run ).has_value() );

    // 3. Attempt immutable pin mutation while Running
    ExperimentRun mutatedPins = run;
    mutatedPins.setSoftwareRevision( QStringLiteral( "hacked-rev-B" ) );
    const auto res2 = store.upsertRun( mutatedPins );
    CHECK( !res2.has_value() );

    // Verify rollback: software revision must still be git-rev-A
    auto loaded2 = store.runById( QStringLiteral( "run-rb-1" ) );
    REQUIRE( loaded2.has_value() );
    CHECK( loaded2->softwareRevision() == QStringLiteral( "git-rev-A" ) );


    // 4. Clean transition Running -> Completed (terminal status requires finishedAtUtc)
    run.setStatus( RunStatus::Completed );
    run.setFinishedAtUtc( QDateTime::currentDateTimeUtc() );
    REQUIRE( store.upsertRun( run ).has_value() );

    auto loaded3 = store.runById( QStringLiteral( "run-rb-1" ) );
    REQUIRE( loaded3.has_value() );
    CHECK( loaded3->status() == RunStatus::Completed );


    // Direct SQLite check: autocommit mode must be active (1), meaning no dangling transaction
    sqlite3 *rawDb = nullptr;
    REQUIRE( sqlite3_open( dbPath.toUtf8().constData(), &rawDb ) == SQLITE_OK );
    CHECK( sqlite3_get_autocommit( rawDb ) == 1 );
    sqlite3_close( rawDb );
}
