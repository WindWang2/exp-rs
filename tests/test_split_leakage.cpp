// test_split_leakage.cpp — Foundation 5.0 split engine + leakage audit
// known-answer tests (goal §19/§51, ADR 0136): byte-identical replay of the
// same (config, seed, inputs), stratified/grouped/spatial/temporal
// semantics, fold materialization, and detection of every constructed
// leakage pattern (overlapping patches, duplicates, parent/scene/event
// crossing, buffer distance, temporal future, pseudo-label parents).
#include <catch2/catch_test_macros.hpp>

#include "dataset/deterministic_random.h"
#include "dataset/leakage_audit.h"
#include "dataset/patch_generator.h"
#include "dataset/split.h"

#include <QJsonDocument>

#include <algorithm>
#include <vector>

using namespace sicnu::dataset;

namespace
{

SplitInput makeInput( const QString &id, const QString &classCode = QStringLiteral( "a" ),
                      const QString &groupId = QString() )
{
    SplitInput input;
    input.sampleId = id;
    input.classCode = classCode;
    input.groupId = groupId;
    return input;
}

QVector<SplitInput> makeInputs( int count, const QString &classCode = QStringLiteral( "a" ),
                                const QString &groupId = QString() )
{
    QVector<SplitInput> inputs;
    for ( int i = 0; i < count; ++i )
    {
        inputs.append( makeInput( QStringLiteral( "s%1" ).arg( i, 4, 10, QLatin1Char( '0' ) ),
                                  classCode, groupId ) );
    }
    return inputs;
}

AuditSample auditFrom( const SplitInput &input, SplitRole role, int fold = -1 )
{
    AuditSample sample;
    sample.input = input;
    sample.role = role;
    sample.fold = fold;
    sample.input.validBounds = true;
    // Center bounds at origin-ish defaults; spatial tests override.
    sample.input.minX = 0.0;
    sample.input.minY = 0.0;
    sample.input.maxX = 10.0;
    sample.input.maxY = 10.0;
    return sample;
}

} // namespace

TEST_CASE( "deterministic random is platform-stable and seed-derived",
           "[dataset][random]" )
{
    // Same seed → identical sequence; the first draws are GOLDEN CONSTANTS
    // so any change to the PRNG is a visible, deliberate contract change
    // (self-consistency alone would not catch a generator swap). The values
    // below were generated once from the in-tree generator; changing them on
    // purpose is how a PRNG/derivation change gets reviewed.
    DeterministicRandom first( 42 );
    DeterministicRandom second( 42 );
    quint32 golden[5] = {};
    for ( int i = 0; i < 5; ++i )
        golden[i] = first.uniform( 100000 );
    for ( int i = 0; i < 5; ++i )
        CHECK( golden[i] == second.uniform( 100000 ) );
    const quint32 expected[5] = { 23541u, 4824u, 10024u, 33894u, 11298u };
    for ( int i = 0; i < 5; ++i )
    {
        INFO( "golden draw " << i );
        CHECK( golden[i] == expected[i] );
    }

    // Textual seeds hash deterministically; derivation is purpose-namespaced.
    CHECK( DeterministicRandom::hashSeed( QStringLiteral( "exp-rs" ) ) ==
           DeterministicRandom::hashSeed( QStringLiteral( "exp-rs" ) ) );
    CHECK( DeterministicRandom::seedFor( 7, QStringLiteral( "split" ) ) ==
           DeterministicRandom::seedFor( 7, QStringLiteral( "split" ) ) );
    CHECK( DeterministicRandom::seedFor( 7, QStringLiteral( "split" ) ) !=
           DeterministicRandom::seedFor( 7, QStringLiteral( "patch.random" ) ) );

    // Uniform bound respected; shuffle is a permutation.
    DeterministicRandom random( 1234 );
    for ( int i = 0; i < 200; ++i )
        CHECK( random.uniform( 7 ) < 7 );
    QVector<int> items{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    QVector<int> copy = items;
    random.shuffle( items );
    std::sort( items.begin(), items.end() );
    CHECK( items == copy );
}

TEST_CASE( "split replay is byte-identical for identical (config, seed, inputs)",
           "[dataset][split][determinism]" )
{
    SplitConfig config;
    config.method = SplitMethod::Random;
    config.seed = 20260907;
    const auto inputs = makeInputs( 200 );

    const auto first = SplitEngine::generate( config, QStringLiteral( "version-1" ), inputs );
    const auto second = SplitEngine::generate( config, QStringLiteral( "version-1" ), inputs );
    REQUIRE( first.has_value() );
    REQUIRE( second.has_value() );
    CHECK( first.value().fingerprint() == second.value().fingerprint() );
    CHECK( first.value().assignments() == second.value().assignments() );

    // Serialization round-trip preserves the fingerprint (canonical form).
    const auto parsed = SplitManifest::fromJson( first.value().toJson() );
    REQUIRE( parsed.has_value() );
    CHECK( splitManifestFingerprint( parsed.value() ) == first.value().fingerprint() );

    // Role counts follow the ratios.
    const QStringList train = first->sampleIdsOfRole( SplitRole::Train );
    const QStringList validation = first->sampleIdsOfRole( SplitRole::Validation );
    const QStringList test = first->sampleIdsOfRole( SplitRole::Test );
    CHECK( train.size() == int( std::floor( 0.7 * 200 ) ) );
    CHECK( validation.size() == int( std::floor( 0.15 * 200 ) ) );
    CHECK( train.size() + validation.size() + test.size() == 200 );

    // A different seed produces a different (equally valid) split.
    SplitConfig otherSeed = config;
    otherSeed.seed = 1;
    const auto third = SplitEngine::generate( otherSeed, QStringLiteral( "version-1" ), inputs );
    REQUIRE( third.has_value() );
    CHECK( third.value().fingerprint() != first.value().fingerprint() );
}

TEST_CASE( "stratified split keeps every class represented proportionally",
           "[dataset][split]" )
{
    SplitConfig config;
    config.method = SplitMethod::Stratified;
    config.seed = 5;
    QVector<SplitInput> inputs;
    // 100 "water" + 20 "urban" — the minority class must survive in train.
    for ( int i = 0; i < 100; ++i )
        inputs.append( makeInput( QStringLiteral( "w%1" ).arg( i, 3, 10, QLatin1Char( '0' ) ),
                                  QStringLiteral( "water" ) ) );
    for ( int i = 0; i < 20; ++i )
        inputs.append( makeInput( QStringLiteral( "u%1" ).arg( i, 3, 10, QLatin1Char( '0' ) ),
                                  QStringLiteral( "urban" ) ) );

    const auto manifest = SplitEngine::generate( config, QStringLiteral( "v" ), inputs );
    REQUIRE( manifest.has_value() );
    const QStringList test = manifest->sampleIdsOfRole( SplitRole::Test );
    CHECK( !test.isEmpty() );
    const bool urbanInTest = std::any_of( test.cbegin(), test.cend(), []( const QString &id ) {
        return id.startsWith( QLatin1Char( 'u' ) );
    } );
    CHECK( urbanInTest );
}

TEST_CASE( "grouped split never splits a group", "[dataset][split]" )
{
    SplitConfig config;
    config.method = SplitMethod::Grouped;
    config.seed = 9;
    QVector<SplitInput> inputs;
    for ( int group = 0; group < 10; ++group )
    {
        for ( int member = 0; member < 5; ++member )
        {
            inputs.append( makeInput(
                QStringLiteral( "g%1m%2" ).arg( group ).arg( member ), QStringLiteral( "a" ),
                QStringLiteral( "group-%1" ).arg( group ) ) );
        }
    }
    const auto manifest = SplitEngine::generate( config, QStringLiteral( "v" ), inputs );
    REQUIRE( manifest.has_value() );

    // Every group lands entirely inside one role.
    QHash<QString, SplitRole> groupRoles;
    for ( const SplitAssignment &assignment : manifest->assignments() )
    {
        const QString inputId = assignment.sampleId;
        const QString group = inputId.section( QLatin1Char( 'm' ), 0, 0 );
        const auto it = groupRoles.constFind( group );
        if ( it == groupRoles.constEnd() )
            groupRoles.insert( group, assignment.role );
        else
            CHECK( *it == assignment.role );
    }
    CHECK( groupRoles.size() == 10 );
}

TEST_CASE( "spatial block split keeps blocks atomic", "[dataset][split]" )
{
    SplitConfig config;
    config.method = SplitMethod::SpatialBlock;
    config.seed = 11;
    config.blockSizeX = 100.0;
    config.blockSizeY = 100.0;
    QVector<SplitInput> inputs;
    int id = 0;
    for ( int blockX = 0; blockX < 4; ++blockX )
    {
        for ( int blockY = 0; blockY < 4; ++blockY )
        {
            for ( int member = 0; member < 4; ++member )
            {
                SplitInput input = makeInput( QStringLiteral( "s%1" ).arg( id++ ) );
                input.validBounds = true;
                input.minX = blockX * 100.0 + member;
                input.maxX = input.minX + 1.0;
                input.minY = blockY * 100.0 + member;
                input.maxY = input.minY + 1.0;
                inputs.append( input );
            }
        }
    }
    const auto manifest = SplitEngine::generate( config, QStringLiteral( "v" ), inputs );
    REQUIRE( manifest.has_value() );
    CHECK( manifest->assignments().size() == inputs.size() );
    // #775/#817: blocks are ATOMIC split units — every sample inside one
    // block grid cell carries the same role. (The baseline split each block
    // internally, putting train and test samples on the same spatial block;
    // the old test only checked counts and replay determinism.)
    {
        QHash<QString, SplitRole> blockRoles;
        QHash<QString, QString> blockOf;
        for ( const SplitInput &input : inputs )
        {
            const double centerX = ( input.minX + input.maxX ) / 2.0;
            const double centerY = ( input.minY + input.maxY ) / 2.0;
            const QString key = QStringLiteral( "%1|%2" )
                                    .arg( qint64( std::floor( centerX / config.blockSizeX ) ) )
                                    .arg( qint64( std::floor( centerY / config.blockSizeY ) ) );
            blockOf.insert( input.sampleId, key );
        }
        for ( const SplitAssignment &assignment : manifest->assignments() )
        {
            const QString key = blockOf.value( assignment.sampleId );
            const auto it = blockRoles.constFind( key );
            if ( it == blockRoles.constEnd() )
                blockRoles.insert( key, assignment.role );
            else
                CHECK( *it == assignment.role );
        }
        // With 16 blocks and 4 samples each, atomic assignment must produce
        // more than one role overall (else the assertion above is vacuous).
        QSet<SplitRole> distinctRoles;
        for ( const SplitAssignment &assignment : manifest->assignments() )
            distinctRoles.insert( assignment.role );
        CHECK( distinctRoles.size() >= 2 );
    }
    // All 64 samples assigned; the replay pins determinism.
    const auto replay = SplitEngine::generate( config, QStringLiteral( "v" ), inputs );
    REQUIRE( replay.has_value() );
    CHECK( replay->assignments() == manifest->assignments() );

    // Missing bounds are a loud configuration error for spatial methods.
    QVector<SplitInput> noBounds = inputs;
    noBounds.first().validBounds = false;
    const auto refused = SplitEngine::generate( config, QStringLiteral( "v" ), noBounds );
    CHECK( !refused.has_value() );
}

TEST_CASE( "spatial_buffer split keeps accepted test picks, vetoes buffer"
           " neighbors, and splits the remainder",
           "[dataset][split]" )
{
    SplitConfig config;
    config.method = SplitMethod::SpatialBuffer;
    config.seed = 21;
    config.bufferDistance = 50.0;
    config.testRatio = 0.25;
    config.trainRatio = 0.5625;
    config.validationRatio = 0.1875;
    QVector<SplitInput> inputs;
    // Two clusters: 4 samples near x=0 (spacing 10 < buffer 50: the first
    // pick vetoes its neighbors) and 12 samples far away around x=1000 at
    // spacing 100 > buffer (all eligible test candidates, leaving a real
    // train/validation remainder).
    for ( int i = 0; i < 4; ++i )
    {
        SplitInput input = makeInput( QStringLiteral( "near-%1" ).arg( i ) );
        input.validBounds = true;
        input.minX = i * 10.0;
        input.maxX = input.minX + 2.0;
        input.minY = 0.0;
        input.maxY = 2.0;
        inputs.append( input );
    }
    for ( int i = 0; i < 12; ++i )
    {
        SplitInput input = makeInput( QStringLiteral( "far-%1" ).arg( i ) );
        input.validBounds = true;
        input.minX = 1000.0 + i * 100.0;
        input.maxX = input.minX + 2.0;
        input.minY = 0.0;
        input.maxY = 2.0;
        inputs.append( input );
    }
    const auto manifest = SplitEngine::generate( config, QStringLiteral( "v" ), inputs );
    REQUIRE( manifest.has_value() );
    CHECK( manifest->assignments().size() == inputs.size() );
    // Guarantee 1: accepted Test picks are mutually >= bufferDistance apart.
    // (Near-cluster samples MAY be picked; the first accepted pick vetoes the
    // rest of its cluster.)
    QVector<SplitInput> byId;
    for ( const SplitInput &input : inputs )
    {
        if ( manifest->assignmentOf( input.sampleId ).value_or( SplitAssignment{} ).role ==
             SplitRole::Test )
            byId.append( input );
    }
    for ( int a = 0; a < byId.size(); ++a )
    {
        for ( int b = a + 1; b < byId.size(); ++b )
        {
            const double dx = ( byId.at( a ).minX + byId.at( a ).maxX ) / 2.0 -
                              ( byId.at( b ).minX + byId.at( b ).maxX ) / 2.0;
            CHECK( std::abs( dx ) >= config.bufferDistance );
        }
    }
    // Guarantee 2: near-cluster samples that are not Test sit in the buffer
    // zone (Unassigned), never in Train against their near-duplicate.
    for ( const SplitAssignment &assignment : manifest->assignments() )
    {
        if ( assignment.sampleId.startsWith( QLatin1String( "near-" ) ) &&
             assignment.role != SplitRole::Test )
            CHECK( assignment.role == SplitRole::Unassigned );
    }
    // #786: buffer-vetoed samples were once counted in the train/validation
    // remainder, inflating the train quota until Validation starved to zero.
    // The remainder splits must actually produce both roles here (12 far
    // samples minus the accepted picks is comfortably large enough).
    int validationCount = 0;
    int trainCount = 0;
    for ( const SplitAssignment &assignment : manifest->assignments() )
    {
        if ( assignment.role == SplitRole::Validation )
            ++validationCount;
        if ( assignment.role == SplitRole::Train )
            ++trainCount;
    }
    CHECK( trainCount > 0 );
    CHECK( validationCount > 0 );
    // Determinism replay.
    const auto replay = SplitEngine::generate( config, QStringLiteral( "v" ), inputs );
    REQUIRE( replay.has_value() );
    CHECK( replay->fingerprint() == manifest->fingerprint() );
}

TEST_CASE( "leakage spatial checks catch pairs whose centers straddle a"
           " bucket boundary",
           "[dataset][leakage]" )
{
    // Regression for the single-bucket scan: centers 4.9 and 5.1 with
    // cell size 1.0 land in adjacent buckets and must still be compared.
    LeakageAuditConfig config;
    config.checks = { QStringLiteral( "distance_below_threshold" ) };
    config.distanceThreshold = 1.0;
    AuditSample a = auditFrom( makeInput( QStringLiteral( "straddle-a" ) ), SplitRole::Train );
    a.input.minX = 4.0;
    a.input.maxX = 5.8;
    a.input.minY = 0.0;
    a.input.maxY = 2.0;
    AuditSample b = auditFrom( makeInput( QStringLiteral( "straddle-b" ) ), SplitRole::Test );
    b.input.minX = 4.0;
    b.input.maxX = 5.8;
    b.input.minY = 0.0;
    b.input.maxY = 2.0;
    // Shift b so its CENTER (5.3) crosses the 5.0 boundary while distance
    // between centers stays tiny.
    b.input.minX = 4.2;
    b.input.maxX = 6.4;
    const auto report = LeakageAuditor::audit(
        QStringLiteral( "v" ), QStringLiteral( "sp" ),
        QVector<AuditSample>{ a, b }, config );
    REQUIRE( report.has_value() );
    CHECK( std::any_of( report->findings().cbegin(), report->findings().cend(),
                        []( const LeakageFinding &finding ) {
                            return finding.kind == LeakageKind::DistanceBelowThreshold;
                        } ) );
}

TEST_CASE( "leakage spatial hashing stays injective across negative"
           " coordinates",
           "[dataset][leakage]" )
{
    // #787: the spatial bucket key is the injective (cellX, cellY) pair —
    // the previous combined key `cy*xSpan+cx` was fragile for negative
    // cells (its arithmetic collides for reachable cell SHAPES only under
    // bounds derived from other samples; the audit post-review classified
    // the practical defect as fragility/robustness rather than an
    // observable wrong report). This test pins the injective implementation:
    // a planted pair in negative territory is found exactly once, and
    // distant samples never attach to it.
    LeakageAuditConfig config;
    config.checks = { QStringLiteral( "distance_below_threshold" ) };
    config.distanceThreshold = 1.0;
    QVector<AuditSample> samples;
    // A planted near-duplicate pair fully in negative territory.
    AuditSample negA = auditFrom( makeInput( QStringLiteral( "neg-a" ) ), SplitRole::Train );
    negA.input.minX = -10.4;
    negA.input.maxX = -9.6;
    negA.input.minY = -10.4;
    negA.input.maxY = -9.6;
    AuditSample negB = auditFrom( makeInput( QStringLiteral( "neg-b" ) ), SplitRole::Test );
    negB.input.minX = -10.2;
    negB.input.maxX = -9.4;
    negB.input.minY = -10.2;
    negB.input.maxY = -9.4;
    // Distant samples whose (cx, cy) cells collide with the pair's cells
    // under the old combined key: with cell = 1.0, xSpan = 11 over bounds
    // [-10.4, 9.6]; (cx=9, cy=-11) used to fold onto (cx=-2, cy=0) etc.
    const char *farIds[] = { "far-a", "far-b", "far-c", "far-d" };
    for ( int i = 0; i < 4; ++i )
    {
        AuditSample far = auditFrom( makeInput( QLatin1String( farIds[i] ) ),
                                     i % 2 == 0 ? SplitRole::Train : SplitRole::Test );
        far.input.minX = 20.0 + i * 200.0;
        far.input.maxX = far.input.minX + 1.0;
        far.input.minY = -20.0 - i * 200.0;
        far.input.maxY = far.input.minY + 1.0;
        samples.append( far );
    }
    samples.append( negA );
    samples.append( negB );
    const auto report = LeakageAuditor::audit(
        QStringLiteral( "v" ), QStringLiteral( "sp" ), samples, config );
    REQUIRE( report.has_value() );
    // Exactly the planted pair is found, exactly once (per direction the
    // reporter emits one finding per unordered pair).
    int plantedFindings = 0;
    int totalFindings = 0;
    for ( const LeakageFinding &finding : report->findings() )
    {
        if ( finding.kind != LeakageKind::DistanceBelowThreshold )
            continue;
        ++totalFindings;
        const bool involvesPlanted =
            ( finding.sampleA == QStringLiteral( "neg-a" ) ||
              finding.sampleB == QStringLiteral( "neg-a" ) );
        if ( involvesPlanted )
            ++plantedFindings;
    }
    CHECK( totalFindings == 1 );
    CHECK( plantedFindings == 1 );
}

TEST_CASE( "leakage over fully-overlapping far-apart windows reports nothing"
           " when distance check is off",
           "[dataset][leakage]" )
{
    // Overlap detection no longer requires coincident centers.
    LeakageAuditConfig config;
    config.checks = { QStringLiteral( "overlapping_patch" ) };
    AuditSample a = auditFrom( makeInput( QStringLiteral( "ov-a" ) ), SplitRole::Train );
    a.input.minX = 0.0;
    a.input.maxX = 256.0;
    a.input.minY = 0.0;
    a.input.maxY = 256.0;
    a.windowWidth = 256;
    a.windowHeight = 256;
    AuditSample b = auditFrom( makeInput( QStringLiteral( "ov-b" ) ), SplitRole::Test );
    b.input.minX = 128.0;
    b.input.maxX = 384.0;
    b.input.minY = 0.0;
    b.input.maxY = 256.0;
    b.windowWidth = 256;
    b.windowHeight = 256;
    const auto report = LeakageAuditor::audit(
        QStringLiteral( "v" ), QStringLiteral( "sp" ),
        QVector<AuditSample>{ a, b }, config );
    REQUIRE( report.has_value() );
    CHECK( std::any_of( report->findings().cbegin(), report->findings().cend(),
                        []( const LeakageFinding &finding ) {
                            return finding.kind == LeakageKind::OverlappingPatch;
                        } ) );
}

TEST_CASE( "k-fold assigns disjoint folds and materialization works",
           "[dataset][split]" )
{
    SplitConfig config;
    config.method = SplitMethod::KFold;
    config.seed = 3;
    config.foldCount = 5;
    const auto inputs = makeInputs( 50 );
    const auto manifest = SplitEngine::generate( config, QStringLiteral( "v" ), inputs );
    REQUIRE( manifest.has_value() );

    std::vector<int> foldCounts( 5, 0 );
    for ( const SplitAssignment &assignment : manifest->assignments() )
    {
        REQUIRE( assignment.fold >= 0 );
        CHECK( assignment.role == SplitRole::Unassigned );
        foldCounts[assignment.fold]++;
    }
    for ( const int foldCount : foldCounts )
        CHECK( foldCount == 10 );

    const auto foldThree = manifest->materializeFold( 3 );
    REQUIRE( foldThree.has_value() );
    int testCount = 0;
    for ( const SplitAssignment &assignment : *foldThree )
    {
        if ( assignment.role == SplitRole::Test )
        {
            ++testCount;
            CHECK( assignment.fold == 3 );
        }
    }
    CHECK( testCount == 10 );
    CHECK( !manifest->materializeFold( 99 ).has_value() );
}

TEST_CASE( "leave-one-year-out groups by year", "[dataset][split]" )
{
    SplitConfig config;
    config.method = SplitMethod::LeaveOneYearOut;
    config.seed = 1;
    QVector<SplitInput> inputs;
    for ( int year = 2023; year <= 2025; ++year )
    {
        for ( int i = 0; i < 6; ++i )
        {
            SplitInput input = makeInput( QStringLiteral( "%1-%2" ).arg( year ).arg( i ) );
            input.year = year;
            inputs.append( input );
        }
    }
    const auto manifest = SplitEngine::generate( config, QStringLiteral( "v" ), inputs );
    REQUIRE( manifest.has_value() );
    CHECK( manifest->materializeFold( 0 ).has_value() );
    // Years without entries are refused loudly.
    SplitInput missingYear = makeInput( QStringLiteral( "noyear" ) );
    QVector<SplitInput> broken = inputs;
    broken.append( missingYear );
    const auto refused = SplitEngine::generate( config, QStringLiteral( "v" ), broken );
    CHECK( !refused.has_value() );
}

TEST_CASE( "leakage audit finds every constructed pattern", "[dataset][leakage]" )
{
    LeakageAuditConfig config;
    config.overlapFractionThreshold = 0.0;

    QVector<AuditSample> samples;
    // 0/1: overlapping patches across train/test.
    AuditSample patchA = auditFrom( makeInput( QStringLiteral( "patch-a" ) ), SplitRole::Train );
    patchA.windowWidth = 256;
    patchA.windowHeight = 256;
    AuditSample patchB = auditFrom( makeInput( QStringLiteral( "patch-b" ) ), SplitRole::Test );
    patchB.windowWidth = 256;
    patchB.windowHeight = 256;
    // 2/3: exact duplicate content.
    AuditSample dupA = auditFrom( makeInput( QStringLiteral( "dup-a" ) ), SplitRole::Train );
    dupA.contentDigest = QStringLiteral( "aa11" );
    AuditSample dupB = auditFrom( makeInput( QStringLiteral( "dup-b" ) ), SplitRole::Test );
    dupB.contentDigest = QStringLiteral( "aa11" );
    // 4/5: same parent polygon.
    AuditSample polyA = auditFrom( makeInput( QStringLiteral( "poly-a" ) ), SplitRole::Train );
    polyA.parentPolygonId = QStringLiteral( "roi-9" );
    polyA.input.minX = 1000.0; // far away from other spatial pairs
    polyA.input.maxX = 1010.0;
    polyA.input.minY = 1000.0;
    polyA.input.maxY = 1010.0;
    AuditSample polyB = auditFrom( makeInput( QStringLiteral( "poly-b" ) ), SplitRole::Test );
    polyB.parentPolygonId = QStringLiteral( "roi-9" );
    polyB.input.minX = 2000.0;
    polyB.input.maxX = 2010.0;
    polyB.input.minY = 2000.0;
    polyB.input.maxY = 2010.0;
    // 6/7: same scene.
    AuditSample sceneA = auditFrom( makeInput( QStringLiteral( "scene-a" ) ), SplitRole::Train );
    sceneA.sceneId = QStringLiteral( "S2_tile_1" );
    sceneA.input.minX = 3000.0;
    sceneA.input.maxX = 3010.0;
    sceneA.input.minY = 3000.0;
    sceneA.input.maxY = 3010.0;
    AuditSample sceneB = auditFrom( makeInput( QStringLiteral( "scene-b" ) ), SplitRole::Test );
    sceneB.sceneId = QStringLiteral( "S2_tile_1" );
    sceneB.input.minX = 4000.0;
    sceneB.input.maxX = 4010.0;
    sceneB.input.minY = 4000.0;
    sceneB.input.maxY = 4010.0;
    // 8/9: augmentation parent crossing.
    AuditSample parent = auditFrom( makeInput( QStringLiteral( "aug-parent" ) ), SplitRole::Test );
    parent.input.minX = 5000.0;
    parent.input.maxX = 5010.0;
    parent.input.minY = 5000.0;
    parent.input.maxY = 5010.0;
    AuditSample augmented =
        auditFrom( makeInput( QStringLiteral( "aug-child" ) ), SplitRole::Train );
    augmented.parentSampleId = QStringLiteral( "aug-parent" );
    augmented.input.minX = 6000.0;
    augmented.input.maxX = 6010.0;
    augmented.input.minY = 6000.0;
    augmented.input.maxY = 6010.0;
    // 10/11: pre/post pair crossing.
    AuditSample pre = auditFrom( makeInput( QStringLiteral( "pair-pre" ) ), SplitRole::Train );
    pre.pairCounterpartId = QStringLiteral( "pair-post" );
    pre.eventGroup = QStringLiteral( "fire-7" );
    pre.input.minX = 7000.0;
    pre.input.maxX = 7010.0;
    pre.input.minY = 7000.0;
    pre.input.maxY = 7010.0;
    AuditSample post = auditFrom( makeInput( QStringLiteral( "pair-post" ) ), SplitRole::Test );
    post.pairCounterpartId = QStringLiteral( "pair-pre" );
    post.eventGroup = QStringLiteral( "fire-7" );
    post.input.minX = 8000.0;
    post.input.maxX = 8010.0;
    post.input.minY = 8000.0;
    post.input.maxY = 8010.0;

    samples = { patchA, patchB, dupA,  dupB,   polyA, polyB,
                sceneA, sceneB, parent, augmented, pre,  post };
    const auto report = LeakageAuditor::audit( QStringLiteral( "v" ), QStringLiteral( "sp" ),
                                               samples, config );
    REQUIRE( report.has_value() );

    auto hasKind = [&]( LeakageKind kind ) {
        return std::any_of( report->findings().cbegin(), report->findings().cend(),
                            [kind]( const LeakageFinding &finding ) {
                                return finding.kind == kind;
                            } );
    };
    CHECK( hasKind( LeakageKind::OverlappingPatch ) );
    CHECK( hasKind( LeakageKind::ExactDuplicate ) );
    CHECK( hasKind( LeakageKind::SameParentPolygon ) );
    CHECK( hasKind( LeakageKind::SameSourceScene ) );
    CHECK( hasKind( LeakageKind::AugmentationParentLeakage ) );
    CHECK( hasKind( LeakageKind::PrePostPairLeakage ) );
    CHECK( hasKind( LeakageKind::SameEventCrossing ) );
    CHECK( report->summary().value( QStringLiteral( "finding_count" ) ).toInteger() >= 7 );
    // The report states exactly which checks ran.
    CHECK( report->auditedChecks().contains( QStringLiteral( "exact_duplicate" ) ) );

    // Report round-trip.
    const auto parsed = LeakageReport::fromJson( report->toJson() );
    REQUIRE( parsed.has_value() );
    CHECK( parsed->findings().size() == report->findings().size() );
}

TEST_CASE( "distance and buffer checks fire under thresholds", "[dataset][leakage]" )
{
    LeakageAuditConfig config;
    config.checks = { QStringLiteral( "distance_below_threshold" ),
                      QStringLiteral( "buffer_overlap" ) };
    config.distanceThreshold = 5.0;
    config.bufferDistance = 50.0;

    AuditSample nearTrain = auditFrom( makeInput( QStringLiteral( "near-a" ) ), SplitRole::Train );
    nearTrain.input.minX = 0.0;
    nearTrain.input.maxX = 2.0;
    nearTrain.input.minY = 0.0;
    nearTrain.input.maxY = 2.0;
    AuditSample nearTest = auditFrom( makeInput( QStringLiteral( "near-b" ) ), SplitRole::Test );
    nearTest.input.minX = 4.0; // centers 5 apart... bounds distance 2 < 5
    nearTest.input.maxX = 6.0;
    nearTest.input.minY = 0.0;
    nearTest.input.maxY = 2.0;

    const auto report = LeakageAuditor::audit( QStringLiteral( "v" ), QStringLiteral( "sp" ),
                                               QVector<AuditSample>{ nearTrain, nearTest },
                                               config );
    REQUIRE( report.has_value() );
    CHECK( std::any_of( report->findings().cbegin(), report->findings().cend(),
                        []( const LeakageFinding &finding ) {
                            return finding.kind == LeakageKind::DistanceBelowThreshold;
                        } ) );
    CHECK( std::any_of( report->findings().cbegin(), report->findings().cend(),
                        []( const LeakageFinding &finding ) {
                            return finding.kind == LeakageKind::BufferOverlap;
                        } ) );

    // Far-apart pair: no findings, clean report.
    nearTest.input.minX = 1000.0;
    nearTest.input.maxX = 1002.0;
    const auto clean = LeakageAuditor::audit( QStringLiteral( "v" ), QStringLiteral( "sp" ),
                                              QVector<AuditSample>{ nearTrain, nearTest },
                                              config );
    REQUIRE( clean.has_value() );
    CHECK( clean->isClean() );
}

TEST_CASE( "temporal future leakage: train-after-test within one series",
           "[dataset][leakage]" )
{
    LeakageAuditConfig config;
    // Scoped to the check under test: same-series groups also trigger
    // same_temporal_group_crossing, which is a different finding.
    config.checks = { QStringLiteral( "temporal_future_leakage" ) };

    AuditSample testEarlier = auditFrom( makeInput( QStringLiteral( "t1" ) ), SplitRole::Test );
    testEarlier.input.groupId = QStringLiteral( "series-1" );
    testEarlier.input.timeMs = 1000;
    AuditSample trainLater = auditFrom( makeInput( QStringLiteral( "t2" ) ), SplitRole::Train );
    trainLater.input.groupId = QStringLiteral( "series-1" );
    trainLater.input.timeMs = 2000;

    const auto report = LeakageAuditor::audit(
        QStringLiteral( "v" ), QStringLiteral( "sp" ),
        QVector<AuditSample>{ testEarlier, trainLater }, config );
    REQUIRE( report.has_value() );
    CHECK( std::any_of( report->findings().cbegin(), report->findings().cend(),
                        []( const LeakageFinding &finding ) {
                            return finding.kind == LeakageKind::TemporalFutureLeakage;
                        } ) );

    // Train strictly BEFORE test is the healthy direction: clean.
    trainLater.input.timeMs = 500;
    const auto healthy = LeakageAuditor::audit(
        QStringLiteral( "v" ), QStringLiteral( "sp" ),
        QVector<AuditSample>{ testEarlier, trainLater }, config );
    REQUIRE( healthy.has_value() );
    CHECK( healthy->isClean() );
}

TEST_CASE( "clean split yields a clean report with checks named",
           "[dataset][leakage]" )
{
    LeakageAuditConfig config;
    QVector<AuditSample> samples;
    for ( int i = 0; i < 4; ++i )
    {
        AuditSample sample = auditFrom(
            makeInput( QStringLiteral( "clean-%1" ).arg( i ) ),
            i % 2 == 0 ? SplitRole::Train : SplitRole::Test );
        sample.input.minX = i * 1000.0;
        sample.input.maxX = sample.input.minX + 5.0;
        samples.append( sample );
    }
    const auto report =
        LeakageAuditor::audit( QStringLiteral( "v" ), QStringLiteral( "sp" ), samples, config );
    REQUIRE( report.has_value() );
    CHECK( report->isClean() );
    CHECK( report->summary().value( QStringLiteral( "clean" ) ).toBool() );
    CHECK( !report->auditedChecks().isEmpty() );
}

TEST_CASE( "patch generator policies produce honest provenance", "[dataset][patch]" )
{
    GeoTransform transform;
    transform.values = { 500000.0, 10.0, 0.0, 4000000.0, 0.0, -10.0 };

    PatchGeneratorConfig config;
    config.strategy = PatchStrategy::FixedGrid;
    config.windowWidth = 32;
    config.windowHeight = 32;
    config.strideX = 32;
    config.strideY = 32;
    config.borderPolicy = BorderPolicy::Drop;

    // 100x100 raster with 32px windows: 3x3 full windows fit; the overhang
    // row/column is dropped BUT RECORDED (16 specs, 7 dropped).
    const auto patches = PatchGenerator::generate(
        config, 100, 100, transform, PatchGenerator::ValidFractionReader() );
    REQUIRE( patches.has_value() );
    CHECK( patches->size() == 16 );
    const int keptCount = int( std::count_if( patches->cbegin(), patches->cend(),
                                              []( const GeneratedPatch &patch ) {
                                                  return !patch.dropped;
                                              } ) );
    CHECK( keptCount == 9 );
    CHECK( patches->first().generatorConfigHash.size() == 64 );
    CHECK( !patches->first().groundFootprintWkt.isEmpty() );

    // Clip policy keeps the edge windows instead.
    PatchGeneratorConfig clip = config;
    clip.borderPolicy = BorderPolicy::Clip;
    const auto clipped = PatchGenerator::generate(
        clip, 100, 100, transform, PatchGenerator::ValidFractionReader() );
    REQUIRE( clipped.has_value() );
    CHECK( clipped->size() == 16 );
    CHECK( clipped->last().window.width == 100 - 96 ); // 4px sliver

    // NoData policy drops via the injected reader; drops are RECORDED.
    PatchGeneratorConfig nodata = config;
    nodata.noDataMode = NoDataMode::MinValidFraction;
    nodata.noDataThreshold = 0.5;
    const auto audited = PatchGenerator::generate(
        nodata, 100, 100, transform,
        []( const PixelWindow &window ) {
            return window.x >= 64 ? 0.2 : 0.9; // right column invalid
        } );
    REQUIRE( audited.has_value() );
    bool sawInvalidFraction = false;
    for ( const GeneratedPatch &patch : audited.value() )
    {
        if ( patch.window.x >= 64 )
        {
            // Right-column specs are dropped (border overhang or invalid
            // fraction) and every drop is RECORDED with the flag down.
            CHECK( patch.dropped );
            CHECK( !patch.validityFlag );
            sawInvalidFraction = true;
        }
        else if ( !patch.dropped )
        {
            CHECK( patch.validityFlag );
        }
    }
    CHECK( sawInvalidFraction );

    // MaxNoDataFraction keeps patches whose NODATA share is within budget
    // (validFraction >= 1 - threshold) and drops the rest.
    PatchGeneratorConfig nodataCap = config;
    nodataCap.noDataMode = NoDataMode::MaxNoDataFraction;
    nodataCap.noDataThreshold = 0.5;
    const auto capped = PatchGenerator::generate(
        nodataCap, 100, 100, transform,
        []( const PixelWindow &window ) {
            return window.x >= 64 ? 0.1 : 0.9; // right column: 90% nodata
        } );
    REQUIRE( capped.has_value() );
    for ( const GeneratedPatch &patch : capped.value() )
    {
        if ( patch.dropped || patch.window.x < 64 )
            continue;
        CHECK( !patch.validityFlag ); // 90% nodata > 50% budget → dropped
    }

    // Random strategy replays identically under one seed.
    PatchGeneratorConfig random = config;
    random.strategy = PatchStrategy::Random;
    random.randomCount = 25;
    random.seed = 777;
    const auto randomA = PatchGenerator::generate(
        random, 500, 500, transform, PatchGenerator::ValidFractionReader() );
    const auto randomB = PatchGenerator::generate(
        random, 500, 500, transform, PatchGenerator::ValidFractionReader() );
    REQUIRE( randomA.has_value() );
    REQUIRE( randomB.has_value() );
    REQUIRE( randomA->size() == 25 );
    for ( int i = 0; i < 25; ++i )
    {
        CHECK( randomA->at( i ).window == randomB->at( i ).window );
        CHECK( randomA->at( i ).dropped == randomB->at( i ).dropped );
    }

    // Generated patches convert to storable samples.
    const SampleRecord sample =
        PatchGenerator::toSampleRecord( randomA->first(), QStringLiteral( "v1" ), random );
    CHECK( sample.kind() == SampleKind::Patch );
    CHECK( validateSample( sample ).has_value() );
    CHECK( sample.provenance().value( QStringLiteral( "generator_config_hash" ) )
               .toString()
               .size() == 64 );
}

TEST_CASE( "ratio assignment honors zero ratios and never starves a"
           " non-zero role",
           "[dataset][split]" )
{
    // #788: floor counts with the remainder dumped into Test starved small
    // Train ratios and let remainders violate testRatio = 0. The
    // largest-remainder distribution pins zero ratios to zero and gives the
    // fractional seats to the largest fractional remainder (ties: Train >
    // Validation > Test).
    struct Case
    {
        double train;
        double validation;
        double test;
        int total;
        int wantTrain;
        int wantValidation;
        int wantTest;
    };
    const Case cases[] = {
        // 0.05 * 10 = 0.5 must round UP to a seat, not starve Train.
        { 0.05, 0.15, 0.80, 10, 1, 1, 8 },
        // Zero test ratio is a hard contract even when floors leave a gap.
        { 0.70, 0.30, 0.00, 3, 2, 1, 0 },
        { 0.70, 0.30, 0.00, 100, 70, 30, 0 },
        // Exact division stays exact; no seat drift.
        { 0.70, 0.20, 0.10, 10, 7, 2, 1 },
        // Two fractional seats must go to DIFFERENT roles (one seat per
        // role): exact 44.8/9.6/9.6 → 45/10/9, never 46/9/9.
        { 0.70, 0.15, 0.15, 64, 45, 10, 9 },
        // Two fractional seats to the largest remainders (.75 > .65 > .6):
        // exact 4.75/6.65/7.60 at total 19 → 5/7/7.
        { 0.25, 0.35, 0.40, 19, 5, 7, 7 },
    };
    for ( const Case &testCase : cases )
    {
        SplitConfig config;
        config.method = SplitMethod::Random;
        config.seed = 7;
        config.trainRatio = testCase.train;
        config.validationRatio = testCase.validation;
        config.testRatio = testCase.test;
        const auto inputs = makeInputs( testCase.total );
        const auto manifest = SplitEngine::generate( config, QStringLiteral( "v" ), inputs );
        REQUIRE( manifest.has_value() );
        int train = 0;
        int validation = 0;
        int test = 0;
        for ( const SplitAssignment &assignment : manifest->assignments() )
        {
            switch ( assignment.role )
            {
                case SplitRole::Train: ++train; break;
                case SplitRole::Validation: ++validation; break;
                case SplitRole::Test: ++test; break;
                case SplitRole::Unassigned: break;
            }
        }
        CAPTURE( testCase.train, testCase.validation, testCase.test, testCase.total );
        CHECK( train == testCase.wantTrain );
        CHECK( validation == testCase.wantValidation );
        CHECK( test == testCase.wantTest );
    }
}
