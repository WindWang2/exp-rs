// test_mlops9_split.cpp — Scientific MLOps 9.0 split-boundary validation
// (goal M0). Regression proof for #875 (SpatialKFold division by zero /
// float→int UB) plus the total parameter-validation matrix (NaN/Inf/negative
// ratios, overflow-guarded block grids), degenerate-fold refusals, the
// joint space×time isolation method, and the bounded generation summary
// (role/fold counts + per-role class distribution).
//
// Every refusal test below FAILS against the pre-9.0 engine in at least one
// observable way: either validation accepted the config (producing a
// garbage/UB manifest) or an empty fold was materialized silently.
#include <catch2/catch_test_macros.hpp>

#include "dataset/split.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <limits>
#include <set>

using namespace sicnu::dataset;

namespace
{

SplitInput plainInput( const QString &id, const QString &classCode = QStringLiteral( "a" ) )
{
    SplitInput input;
    input.sampleId = id;
    input.classCode = classCode;
    return input;
}

QVector<SplitInput> makePlainInputs( int count )
{
    QVector<SplitInput> inputs;
    for ( int i = 0; i < count; ++i )
        inputs.append( plainInput( QStringLiteral( "s%1" ).arg( i, 4, 10, QChar( '0' ) ) ) );
    return inputs;
}

SplitInput boundedInput( const QString &id, double minX, double minY, double maxX,
                         double maxY, qint64 timeMs = 0,
                         const QString &classCode = QStringLiteral( "a" ) )
{
    SplitInput input = plainInput( id, classCode );
    input.validBounds = true;
    input.minX = minX;
    input.minY = minY;
    input.maxX = maxX;
    input.maxY = maxY;
    input.timeMs = timeMs;
    return input;
}

/// A deterministic grid of spatial samples: cols×rows cells, one sample
/// per cell, cell size 10, origin (0,0).
QVector<SplitInput> gridInputs( int cols, int rows, qint64 timeMs = 0 )
{
    QVector<SplitInput> inputs;
    for ( int row = 0; row < rows; ++row )
    {
        for ( int col = 0; col < cols; ++col )
        {
            inputs.append( boundedInput(
                QStringLiteral( "s_%1_%2" ).arg( row ).arg( col ),
                col * 10.0, row * 10.0, col * 10.0 + 10.0, row * 10.0 + 10.0,
                timeMs ) );
        }
    }
    return inputs;
}

SplitConfig configWith( SplitMethod method )
{
    SplitConfig config;
    config.method = method;
    config.seed = 0xC0FFEE;
    return config;
}

std::set<QString> idsOf( const SplitManifest &manifest, SplitRole role )
{
    const QStringList ids = manifest.sampleIdsOfRole( role );
    return std::set<QString>( ids.cbegin(), ids.cend() );
}

} // namespace

TEST_CASE( "SpatialKFold refuses a zero block size (#875)",
           "[mlops9][split][spatial]" )
{
    // Old behavior: validate() passed, centerX/0.0 = ±Inf, the float→int
    // cast was UB and every sample collapsed into one fold of a garbage
    // grid index. New behavior: typed refusal BEFORE the engine runs.
    SplitConfig config = configWith( SplitMethod::SpatialKFold );
    config.foldCount = 2;
    config.blockSizeX = 0.0; // the #875 trigger
    config.blockSizeY = 100.0;
    const auto result = SplitEngine::generate( config, QStringLiteral( "v1" ),
                                               gridInputs( 4, 4 ) );
    REQUIRE( !result );
    REQUIRE( result.diagnostics().size() == 1 );
    REQUIRE( result.diagnostics().first().code == QStringLiteral( "dataset.split_invalid" ) );
    INFO( result.diagnostics().first().message.toStdString() );
    REQUIRE( result.diagnostics().first().message.contains( QStringLiteral( "block size" ) ) );
}

TEST_CASE( "SpatialKFold refuses negative and NaN block sizes",
           "[mlops9][split][spatial]" )
{
    for ( const double badX : { -100.0, std::numeric_limits<double>::quiet_NaN() } )
    {
        SplitConfig config = configWith( SplitMethod::SpatialKFold );
        config.foldCount = 2;
        config.blockSizeX = badX;
        config.blockSizeY = 100.0;
        const auto validated = config.validate();
        REQUIRE( !validated );
        const auto result = SplitEngine::generate( config, QStringLiteral( "v1" ),
                                                   gridInputs( 4, 4 ) );
        REQUIRE( !result );
    }
    // NaN passed the old `blockSize <= 0.0` guard on spatial_block too.
    SplitConfig block = configWith( SplitMethod::SpatialBlock );
    block.blockSizeX = std::numeric_limits<double>::quiet_NaN();
    block.blockSizeY = 100.0;
    REQUIRE( !block.validate() );
    REQUIRE( !SplitEngine::generate( block, QStringLiteral( "v1" ), gridInputs( 4, 4 ) ) );
}

TEST_CASE( "spatial methods refuse block grids outside the integer range",
           "[mlops9][split][spatial]" )
{
    // A validated-positive but absurdly small block size pushes the grid
    // quotient past 2^62 — the float→int cast would be UB. The engine must
    // refuse per input, with a typed diagnostic.
    SplitConfig kfold = configWith( SplitMethod::SpatialKFold );
    kfold.foldCount = 2;
    kfold.blockSizeX = 1e-300;
    kfold.blockSizeY = 1e-300;
    auto result = SplitEngine::generate( kfold, QStringLiteral( "v1" ), gridInputs( 4, 4 ) );
    REQUIRE( !result );
    REQUIRE( result.diagnostics().first().message.contains(
        QStringLiteral( "representable" ) ) );

    SplitConfig block = configWith( SplitMethod::SpatialBlock );
    block.blockSizeX = 1e-300;
    block.blockSizeY = 1e-300;
    result = SplitEngine::generate( block, QStringLiteral( "v1" ), gridInputs( 4, 4 ) );
    REQUIRE( !result );
}

TEST_CASE( "spatial_buffer refuses a non-finite buffer distance",
           "[mlops9][split][spatial]" )
{
    // NaN passed `bufferDistance <= 0.0` (comparison is false) and then
    // disabled every exclusion silently.
    SplitConfig config = configWith( SplitMethod::SpatialBuffer );
    config.bufferDistance = std::numeric_limits<double>::quiet_NaN();
    REQUIRE( !config.validate() );
    config.bufferDistance = std::numeric_limits<double>::infinity();
    REQUIRE( !config.validate() );
    config.bufferDistance = 0.0;
    REQUIRE( !config.validate() );
}

TEST_CASE( "plain methods refuse non-finite and negative ratios",
           "[mlops9][split][validation]" )
{
    // A negative train ratio used to pass the sum check and be silently
    // treated as a zero ratio; NaN failed the sum check with a misleading
    // message and -Inf could slip through as a zero clamp.
    SplitConfig config = configWith( SplitMethod::Random );
    config.trainRatio = -0.2;
    config.validationRatio = 0.7;
    config.testRatio = 0.5;
    REQUIRE( !config.validate() );

    config.trainRatio = std::numeric_limits<double>::quiet_NaN();
    config.validationRatio = 0.7;
    config.testRatio = 0.3;
    REQUIRE( !config.validate() );
    REQUIRE( config.validate().diagnostics().first().message.contains(
        QStringLiteral( "finite" ) ) );

    config.trainRatio = std::numeric_limits<double>::infinity();
    REQUIRE( !config.validate() );
}

TEST_CASE( "fold methods refuse degenerate fold geometries",
           "[mlops9][split][folds]" )
{
    // k_fold with fewer samples than folds used to materialize empty
    // (unusable) test folds silently.
    SplitConfig kfold = configWith( SplitMethod::KFold );
    kfold.foldCount = 5;
    auto inputs = makePlainInputs( 3 );
    auto result = SplitEngine::generate( kfold, QStringLiteral( "v1" ), inputs );
    REQUIRE( !result );
    REQUIRE( result.diagnostics().first().message.contains(
        QStringLiteral( "as many samples" ) ) );

    // group_k_fold with fewer groups than folds.
    SplitConfig groupKfold = configWith( SplitMethod::GroupKFold );
    groupKfold.foldCount = 4;
    QVector<SplitInput> grouped;
    for ( int i = 0; i < 8; ++i )
    {
        SplitInput input = plainInput( QStringLiteral( "g%1" ).arg( i ) );
        input.groupId = QStringLiteral( "grp%1" ).arg( i % 2 ); // only 2 groups
        grouped.append( input );
    }
    result = SplitEngine::generate( groupKfold, QStringLiteral( "v1" ), grouped );
    REQUIRE( !result );
    REQUIRE( result.diagnostics().first().message.contains(
        QStringLiteral( "as many groups" ) ) );

    // spatial_k_fold with fewer blocks than folds (valid block size so the
    // config passes validation and reaches the fold-geometry check).
    SplitConfig spatialKfold = configWith( SplitMethod::SpatialKFold );
    spatialKfold.foldCount = 5;
    spatialKfold.blockSizeX = 100.0;
    spatialKfold.blockSizeY = 100.0;
    result = SplitEngine::generate( spatialKfold, QStringLiteral( "v1" ),
                                    gridInputs( 2, 2 ) ); // 4 samples, 1 block @100
    REQUIRE( !result );
    REQUIRE( result.diagnostics().first().message.contains(
        QStringLiteral( "as many spatial blocks" ) ) );

    // leave-one-out with a single key: nothing to hold out from.
    SplitConfig loro = configWith( SplitMethod::LeaveOneRegionOut );
    loro.regionKey = QStringLiteral( "region" );
    QVector<SplitInput> oneRegion;
    for ( int i = 0; i < 6; ++i )
    {
        SplitInput input = plainInput( QStringLiteral( "r%1" ).arg( i ) );
        input.groupId = QStringLiteral( "only-region" );
        oneRegion.append( input );
    }
    result = SplitEngine::generate( loro, QStringLiteral( "v1" ), oneRegion );
    REQUIRE( !result );
    REQUIRE( result.diagnostics().first().message.contains(
        QStringLiteral( "at least two distinct" ) ) );
}

TEST_CASE( "every sample appears exactly once across roles and folds",
           "[mlops9][split][invariants]" )
{
    const int total = 200;
    // Plain method: the three roles partition the ids.
    SplitConfig random = configWith( SplitMethod::Random );
    random.trainRatio = 0.5;
    random.validationRatio = 0.25;
    random.testRatio = 0.25;
    auto inputs = makePlainInputs( total );
    auto manifest = SplitEngine::generate( random, QStringLiteral( "v1" ), inputs );
    REQUIRE( manifest );
    std::set<QString> seen;
    for ( const SplitAssignment &assignment : manifest->assignments() )
        seen.insert( assignment.sampleId );
    REQUIRE( seen.size() == total );
    REQUIRE( idsOf( *manifest, SplitRole::Train ).size() == 100 );
    REQUIRE( idsOf( *manifest, SplitRole::Validation ).size() == 50 );
    REQUIRE( idsOf( *manifest, SplitRole::Test ).size() == 50 );

    // Fold method: folds partition the ids; every materialization covers all.
    SplitConfig kfold = configWith( SplitMethod::KFold );
    kfold.foldCount = 4;
    auto folds = SplitEngine::generate( kfold, QStringLiteral( "v1" ), inputs );
    REQUIRE( folds );
    seen.clear();
    int testTotal = 0;
    for ( int fold = 0; fold < kfold.foldCount; ++fold )
    {
        const auto materialized = folds->materializeFold( fold );
        REQUIRE( materialized );
        REQUIRE( materialized->size() == total );
        for ( const SplitAssignment &assignment : *materialized )
        {
            if ( assignment.role == SplitRole::Test )
            {
                REQUIRE( assignment.fold == fold );
                ++testTotal;
            }
        }
    }
    REQUIRE( testTotal == total );
}

TEST_CASE( "spatiotemporal_block keeps space×time cells atomic and separable",
           "[mlops9][split][spatiotemporal]" )
{
    // Four sites (10-unit spacing), each observed in two years. Block size
    // 10 puts every site in its own spatial block; the temporal window (one
    // year) then yields 4 × 2 = 8 distinct cells — while a same-year
    // revisited site stays inside one cell, the exact case neither
    // spatial_block nor temporal handles alone.
    const qint64 yearMs = 365ll * 24 * 60 * 60 * 1000;
    const qint64 base = qint64( 1577836800000ll ); // 2020-01-01T00:00Z
    QVector<SplitInput> inputs;
    for ( int site = 0; site < 4; ++site )
    {
        for ( int year = 0; year < 2; ++year )
        {
            inputs.append( boundedInput(
                QStringLiteral( "site%1_y%2" ).arg( site ).arg( year ),
                site * 10.0, 0.0, site * 10.0 + 10.0, 10.0,
                base + year * yearMs ) );
        }
    }
    SplitConfig config = configWith( SplitMethod::SpatioTemporalBlock );
    config.trainRatio = 0.5;
    config.validationRatio = 0.25;
    config.testRatio = 0.25;
    config.blockSizeX = 10.0;
    config.blockSizeY = 10.0;
    config.temporalWindowMs = yearMs;
    REQUIRE( config.validate() );

    auto manifest = SplitEngine::generate( config, QStringLiteral( "v1" ), inputs );
    REQUIRE( manifest );
    REQUIRE( manifest->config().method == SplitMethod::SpatioTemporalBlock );
    REQUIRE( manifest->determinism() == DeterminismGrade::Strict );

    // Determinism: the same (config, seed, inputs) replays identically —
    // fingerprint equality is the contract (manifest id/time may differ).
    auto replay = SplitEngine::generate( config, QStringLiteral( "v1" ), inputs );
    REQUIRE( replay );
    REQUIRE( splitManifestFingerprint( *manifest ) ==
             splitManifestFingerprint( *replay ) );

    // Atomicity: both observations of a site in the SAME year (same cell)
    // would share a role — with one sample per cell this is trivially true,
    // so build a two-samples-per-cell variant and check role equality.
    QVector<SplitInput> doubled;
    for ( const SplitInput &input : inputs )
    {
        doubled.append( input );
        SplitInput copy = input;
        copy.sampleId = input.sampleId + QStringLiteral( "_b" );
        doubled.append( copy );
    }
    auto doubledResult = SplitEngine::generate( config, QStringLiteral( "v1" ), doubled );
    REQUIRE( doubledResult );
    QHash<QString, SplitRole> roleOf;
    for ( const SplitAssignment &assignment : doubledResult->assignments() )
        roleOf.insert( assignment.sampleId, assignment.role );
    for ( const SplitInput &input : inputs )
    {
        REQUIRE( roleOf.value( input.sampleId ) ==
                 roleOf.value( input.sampleId + QStringLiteral( "_b" ) ) );
    }

    // Separability: the two years of the dataset must not all land in one
    // role (a same-role collapse would defeat the joint isolation). Both
    // (site, year) groups of year-0 cells vs year-1 cells are distinct
    // cells; with 8 cells and a 4/2/2 split both years reach train+test.
    QHash<QString, QSet<int>> yearRoleSet[2];
    QHash<int, int> yearSampleCount;
    for ( const SplitAssignment &assignment : manifest->assignments() )
    {
        const int year = assignment.sampleId.endsWith( QStringLiteral( "_y1" ) ) ? 1 : 0;
        yearRoleSet[year][assignment.sampleId].insert( int( assignment.role ) );
        ++yearSampleCount[year];
        // Cells are atomic: store nothing else here; the doubled check
        // above already proved intra-cell atomicity.
    }
    REQUIRE( !yearRoleSet[0].isEmpty() );
    REQUIRE( !yearRoleSet[1].isEmpty() );
    // At least one year contributes to both train and test pools.
    bool yearSplitsPools = false;
    for ( const auto &byYear : yearRoleSet )
    {
        bool hasTrain = false;
        bool hasTest = false;
        for ( auto it = byYear.constBegin(); it != byYear.constEnd(); ++it )
        {
            hasTrain = hasTrain || it.value().contains( int( SplitRole::Train ) );
            hasTest = hasTest || it.value().contains( int( SplitRole::Test ) );
        }
        if ( hasTrain && hasTest )
            yearSplitsPools = true;
    }
    REQUIRE( yearSplitsPools );
    // Both years contributed samples.
    REQUIRE( yearSampleCount.value( 0 ) > 0 );
    REQUIRE( yearSampleCount.value( 1 ) > 0 );
}

TEST_CASE( "spatiotemporal_block refuses incomplete inputs",
           "[mlops9][split][spatiotemporal]" )
{
    SplitConfig config = configWith( SplitMethod::SpatioTemporalBlock );
    config.blockSizeX = 100.0;
    config.blockSizeY = 100.0;
    config.temporalWindowMs = 1000;

    // No bounds.
    auto noBounds = SplitEngine::generate( config, QStringLiteral( "v1" ),
                                           makePlainInputs( 4 ) );
    REQUIRE( !noBounds );
    REQUIRE( noBounds.diagnostics().first().message.contains(
        QStringLiteral( "bounds" ) ) );

    // Bounds but no observation time.
    auto noTime = SplitEngine::generate( config, QStringLiteral( "v1" ),
                                         gridInputs( 2, 2 ) );
    REQUIRE( !noTime );
    REQUIRE( noTime.diagnostics().first().message.contains(
        QStringLiteral( "observation time" ) ) );

    // Non-positive window refused at validation.
    SplitConfig zeroWindow = config;
    zeroWindow.temporalWindowMs = 0;
    REQUIRE( !zeroWindow.validate() );
}

TEST_CASE( "generation summary reports role counts and class distribution",
           "[mlops9][split][summary]" )
{
    QVector<SplitInput> inputs;
    for ( int i = 0; i < 100; ++i )
    {
        const QString classCode = i % 2 == 0 ? QStringLiteral( "water" )
                                             : QStringLiteral( "forest" );
        inputs.append( plainInput( QStringLiteral( "s%1" ).arg( i, 4, 10, QChar( '0' ) ),
                                   classCode ) );
    }
    // Stratified: classes are quota-split individually, so the class
    // distribution per role is a known answer (50 per class → 25/13/12).
    SplitConfig config = configWith( SplitMethod::Stratified );
    config.trainRatio = 0.5;
    config.validationRatio = 0.25;
    config.testRatio = 0.25;
    auto manifest = SplitEngine::generate( config, QStringLiteral( "v1" ), inputs );
    REQUIRE( manifest );

    const QJsonObject summary = manifest->summary();
    REQUIRE( summary.value( QStringLiteral( "total" ) ).toInt() == 100 );
    const QJsonObject roles =
        summary.value( QStringLiteral( "roles" ) ).toObject();
    // Per class of 50: floors 25/12/12 + one remainder seat to Validation
    // (deterministic Train > Validation > Test tie-break) → 25/13/12 per
    // class, i.e. 50/26/24 over both classes.
    REQUIRE( roles.value( QStringLiteral( "train" ) ).toInt() == 50 );
    REQUIRE( roles.value( QStringLiteral( "validation" ) ).toInt() == 26 );
    REQUIRE( roles.value( QStringLiteral( "test" ) ).toInt() == 24 );

    const QJsonObject distribution =
        summary.value( QStringLiteral( "class_distribution" ) ).toObject();
    // The 50/50 classes follow the largest-remainder split of each role.
    const int trainWater = distribution.value( QStringLiteral( "train" ) )
                               .toObject()
                               .value( QStringLiteral( "water" ) )
                               .toInt();
    const int trainForest = distribution.value( QStringLiteral( "train" ) )
                                .toObject()
                                .value( QStringLiteral( "forest" ) )
                                .toInt();
    REQUIRE( trainWater + trainForest == 50 );
    REQUIRE( trainWater == 25 );
    REQUIRE( trainForest == 25 );

    // Round-trip: the summary survives JSON persistence.
    const auto restored = SplitManifest::fromJson(
        QJsonDocument( manifest->toJson() ).object() );
    REQUIRE( restored );
    REQUIRE( restored->summary() == manifest->summary() );
}

TEST_CASE( "generation summary reports fold counts and truncates wide class sets",
           "[mlops9][split][summary]" )
{
    // Fold methods: per-fold counts, everything unassigned before
    // materialization.
    SplitConfig kfold = configWith( SplitMethod::KFold );
    kfold.foldCount = 4;
    auto inputs = makePlainInputs( 40 );
    auto folds = SplitEngine::generate( kfold, QStringLiteral( "v1" ), inputs );
    REQUIRE( folds );
    const QJsonObject foldCounts =
        folds->summary().value( QStringLiteral( "folds" ) ).toObject();
    REQUIRE( foldCounts.size() == 4 );
    int foldTotal = 0;
    for ( const QString &key : foldCounts.keys() )
        foldTotal += foldCounts.value( key ).toInt();
    REQUIRE( foldTotal == 40 );
    REQUIRE( folds->summary()
                 .value( QStringLiteral( "roles" ) )
                 .toObject()
                 .value( QStringLiteral( "unassigned" ) )
                 .toInt() == 40 );

    // Wide class set: the distribution is capped with an explicit flag.
    QVector<SplitInput> wide;
    for ( int i = 0; i < 3000; ++i )
    {
        wide.append( plainInput( QStringLiteral( "w%1" ).arg( i, 5, 10, QChar( '0' ) ),
                                 QStringLiteral( "class_%1" ).arg( i % 300 ) ) );
    }
    SplitConfig random = configWith( SplitMethod::Random );
    random.trainRatio = 0.9;
    random.validationRatio = 0.05;
    random.testRatio = 0.05;
    auto wideResult = SplitEngine::generate( random, QStringLiteral( "v1" ), wide );
    REQUIRE( wideResult );
    const QJsonObject wideSummary = wideResult->summary();
    REQUIRE( wideSummary.value( QStringLiteral( "class_distribution_truncated" ) )
                 .toBool() );
    const QJsonObject trainDistribution =
        wideSummary.value( QStringLiteral( "class_distribution" ) )
            .toObject()
            .value( QStringLiteral( "train" ) )
            .toObject();
    REQUIRE( trainDistribution.size() <= kSplitSummaryMaxClasses );
}
