// test_leakage_audit_coverage.cpp — Track 13 R4 WP-C: leakage audit coverage
// matrix + report reproducibility.
//
// Every constructed-leakage row plants a KNOWN layout and asserts the
// auditor reports exactly the planted findings, located to sample ids
// (report set == constructed set, zero tolerance). The permutation rows
// pin the report-order invariance contract ("findings accumulate in stable
// order … so reports are reproducible", leakage_audit.h).
#include <catch2/catch_test_macros.hpp>

#include "dataset/fold_audit.h"
#include "dataset/leakage_audit.h"
#include "dataset/split.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

using namespace sicnu::dataset;

namespace
{

AuditSample sampleWith( const QString &id, SplitRole role, const QString &classCode )
{
    AuditSample sample;
    sample.input.sampleId = id;
    sample.input.classCode = classCode;
    sample.input.groupId = QStringLiteral( "g-%1" ).arg( id );
    sample.role = role;
    sample.fold = -1;
    // Spatially locatable by default (10×10 ground footprint); spatial rows
    // override the bounds via spaced().
    sample.input.validBounds = true;
    sample.input.minX = 0.0;
    sample.input.maxX = 10.0;
    sample.input.minY = 0.0;
    sample.input.maxY = 10.0;
    return sample;
}

/// Two samples on opposite sides of a split, both carrying @p key in the
/// given lineage field — one planted cross-split contact.
QVector<AuditSample> plantedPair( AuditSample a, AuditSample b )
{
    return { a, b };
}

} // namespace

TEST_CASE( "leakage audit coverage matrix: every planted contact surface is reported",
           "[dataset][leakage][coverage][matrix]" )
{
    struct CoverageRow
    {
        const char *check;
        QVector<AuditSample> samples;
        QString expectedA;
        QString expectedB;
        LeakageKind kind;
        LeakageAuditConfig config;
    };

    auto configured = []( const QString &id, SplitRole role, const QString &groupId,
                          qint64 timeMs ) {
        AuditSample s = sampleWith( id, role, QStringLiteral( "a" ) );
        s.input.groupId = groupId;
        s.input.timeMs = timeMs;
        return s;
    };
    auto pseudoChild = []( const QString &id, SplitRole role, const QString &parent ) {
        AuditSample s = sampleWith( id, role, QStringLiteral( "a" ) );
        s.parentSampleId = parent;
        s.pseudoDerived = true;
        return s;
    };
    auto withDigest = []( AuditSample s, const QString &digest ) {
        s.contentDigest = digest;
        return s;
    };
    auto withField = []( AuditSample s, void ( *setter )( AuditSample &, const QString & ),
                         const QString &value ) {
        setter( s, value );
        return s;
    };
    auto setParentPolygon = []( AuditSample &s, const QString &v ) { s.parentPolygonId = v; };
    auto setSourceObject = []( AuditSample &s, const QString &v ) { s.sourceObjectId = v; };
    auto setScene = []( AuditSample &s, const QString &v ) { s.sceneId = v; };
    auto setEventGroup = []( AuditSample &s, const QString &v ) { s.eventGroup = v; };
    auto setParentSample = []( AuditSample &s, const QString &v ) { s.parentSampleId = v; };
    auto setCounterpart = []( AuditSample &s, const QString &v ) { s.pairCounterpartId = v; };

    LeakageAuditConfig overlapConfig;
    overlapConfig.overlapFractionThreshold = 0.0;

    LeakageAuditConfig distanceConfig;
    distanceConfig.distanceThreshold = 100.0;

    LeakageAuditConfig bufferConfig;
    bufferConfig.bufferDistance = 50.0;

    auto spaced = []( const QString &id, SplitRole role, double x, double y ) {
        AuditSample s = sampleWith( id, role, QStringLiteral( "a" ) );
        s.input.minX = x - 5.0;
        s.input.maxX = x + 5.0;
        s.input.minY = y - 5.0;
        s.input.maxY = y + 5.0;
        return s;
    };

    auto patchPair = [&]( const QString &a, const QString &b, SplitRole roleA, SplitRole roleB ) {
        // The predicate intersects the GROUND bounds; windowWidth/Height gate
        // the check. Half-overlapping footprints (5 of 10 units).
        AuditSample left = spaced( a, roleA, 100.0, 100.0 );
        left.windowWidth = 32.0;
        left.windowHeight = 32.0;
        AuditSample right = spaced( b, roleB, 105.0, 100.0 );
        right.windowWidth = 32.0;
        right.windowHeight = 32.0;
        return plantedPair( left, right );
    };

    const QVector<CoverageRow> rows = {
        { "exact_duplicate",
          plantedPair( withDigest( sampleWith( "dup-a", SplitRole::Train, "a" ), "d1" ),
                       withDigest( sampleWith( "dup-b", SplitRole::Test, "a" ), "d1" ) ),
          "dup-a", "dup-b", LeakageKind::ExactDuplicate, LeakageAuditConfig{} },
        { "same_parent_polygon",
          plantedPair( withField( sampleWith( "poly-a", SplitRole::Train, "a" ), setParentPolygon,
                                  "P1" ),
                       withField( sampleWith( "poly-b", SplitRole::Test, "a" ), setParentPolygon,
                                  "P1" ) ),
          "poly-a", "poly-b", LeakageKind::SameParentPolygon, LeakageAuditConfig{} },
        { "same_source_object",
          plantedPair( withField( sampleWith( "obj-a", SplitRole::Train, "a" ), setSourceObject,
                                  "O1" ),
                       withField( sampleWith( "obj-b", SplitRole::Test, "a" ), setSourceObject,
                                  "O1" ) ),
          "obj-a", "obj-b", LeakageKind::SameSourceObject, LeakageAuditConfig{} },
        { "same_source_scene",
          plantedPair( withField( sampleWith( "scene-a", SplitRole::Train, "a" ), setScene, "S1" ),
                       withField( sampleWith( "scene-b", SplitRole::Test, "a" ), setScene, "S1" ) ),
          "scene-a", "scene-b", LeakageKind::SameSourceScene, LeakageAuditConfig{} },
        { "same_event_crossing",
          plantedPair( withField( sampleWith( "event-a", SplitRole::Train, "a" ), setEventGroup,
                                  "E1" ),
                       withField( sampleWith( "event-b", SplitRole::Test, "a" ), setEventGroup,
                                  "E1" ) ),
          "event-a", "event-b", LeakageKind::SameEventCrossing, LeakageAuditConfig{} },
        { "same_temporal_group_crossing",
          plantedPair( configured( "grp-a", SplitRole::Train, "G1", 0 ),
                       configured( "grp-b", SplitRole::Test, "G1", 0 ) ),
          "grp-a", "grp-b", LeakageKind::SameTemporalGroupCrossing, LeakageAuditConfig{} },
        { "augmentation_parent_leakage",
          plantedPair( withField( sampleWith( "aug-child", SplitRole::Test, "a" ), setParentSample,
                                  "aug-parent" ),
                       sampleWith( "aug-parent", SplitRole::Train, "a" ) ),
          "aug-child", "aug-parent", LeakageKind::AugmentationParentLeakage,
          LeakageAuditConfig{} },
        { "pseudo_label_parent_leakage",
          plantedPair( pseudoChild( "pseudo-child", SplitRole::Test,
                                    QStringLiteral( "pseudo-parent" ) ),
                       sampleWith( "pseudo-parent", SplitRole::Train, "a" ) ),
          "pseudo-child", "pseudo-parent", LeakageKind::PseudoLabelParentLeakage,
          LeakageAuditConfig{} },
        { "overlapping_patch",
          patchPair( "patch-a", "patch-b", SplitRole::Train, SplitRole::Test ),
          "patch-a", "patch-b", LeakageKind::OverlappingPatch, overlapConfig },
        { "distance_below_threshold",
          plantedPair( spaced( "near-a", SplitRole::Train, 100.0, 100.0 ),
                       spaced( "near-b", SplitRole::Test, 130.0, 100.0 ) ),
          "near-a", "near-b", LeakageKind::DistanceBelowThreshold, distanceConfig },
        { "buffer_overlap",
          plantedPair( spaced( "buf-a", SplitRole::Train, 100.0, 100.0 ),
                       spaced( "buf-b", SplitRole::Test, 120.0, 100.0 ) ),
          "buf-a", "buf-b", LeakageKind::BufferOverlap, bufferConfig },
        { "pre_post_pair_leakage",
          plantedPair( withField( sampleWith( "pair-a", SplitRole::Train, "a" ), setCounterpart,
                                  "pair-b" ),
                       withField( sampleWith( "pair-b", SplitRole::Test, "a" ), setCounterpart,
                                  "pair-a" ) ),
          "pair-a", "pair-b", LeakageKind::PrePostPairLeakage, LeakageAuditConfig{} },
        { "temporal_future_leakage",
          plantedPair( configured( "future-train", SplitRole::Train, "T1", 2000 ),
                       configured( "future-test", SplitRole::Test, "T1", 1000 ) ),
          "future-test", "future-train", LeakageKind::TemporalFutureLeakage,
          LeakageAuditConfig{} },
    };

    for ( const CoverageRow &row : rows )
    {
        INFO( "coverage row: " << row.check );
        // The check under test must be the only claimed audit surface.
        LeakageAuditConfig config = row.config;
        config.checks = QStringList{ QString::fromLatin1( row.check ) };
        const auto report = LeakageAuditor::audit(
            QStringLiteral( "version-x" ), QStringLiteral( "split-x" ), row.samples, config );
        REQUIRE( report.has_value() );
        CHECK( report.value().auditedChecks() ==
               QStringList{ QString::fromLatin1( row.check ) } );

        // Report set == constructed set: exactly the planted pair, located
        // to sample ids (sampleA/sampleB sorted by id per the contract).
        const QString sampleA = row.expectedA < row.expectedB ? row.expectedA : row.expectedB;
        const QString sampleB = row.expectedA < row.expectedB ? row.expectedB : row.expectedA;
        REQUIRE( report.value().findings().size() == 1 );
        const LeakageFinding &finding = report.value().findings().first();
        CHECK( finding.kind == row.kind );
        CHECK( finding.sampleA == sampleA );
        CHECK( finding.sampleB == sampleB );
        CHECK( finding.severity == DiagnosticSeverity::Error );
    }
}

TEST_CASE( "leakage findings order is invariant under input permutation",
           "[dataset][leakage][determinism][matrix]" )
{
    // Multiple simultaneous contact surfaces; the report sequence must not
    // depend on the order samples were handed to the auditor (QHash bucket
    // order is unspecified in Qt6 — the final sort is the contract).
    auto planted = [] {
        QVector<AuditSample> samples;
        auto addDigest = [&]( const QString &id, SplitRole role, const QString &digest ) {
            AuditSample s = sampleWith( id, role, QStringLiteral( "a" ) );
            s.contentDigest = digest;
            samples.append( s );
        };
        addDigest( "x1", SplitRole::Train, "digest-1" );
        addDigest( "x2", SplitRole::Test, "digest-1" );
        addDigest( "x3", SplitRole::Train, "digest-2" );
        addDigest( "x4", SplitRole::Test, "digest-2" );
        auto withPolygon = [&]( const QString &id, SplitRole role ) {
            AuditSample s = sampleWith( id, role, QStringLiteral( "a" ) );
            s.parentPolygonId = QStringLiteral( "P1" );
            samples.append( s );
            return s;
        };
        withPolygon( "x5", SplitRole::Train );
        withPolygon( "x6", SplitRole::Test );
        return samples;
    }();

    LeakageAuditConfig config;
    config.crossSplitOnly = true;
    const auto forward = LeakageAuditor::audit( QStringLiteral( "v" ), QStringLiteral( "s" ),
                                                planted, config );
    std::reverse( planted.begin(), planted.end() );
    const auto reversed = LeakageAuditor::audit( QStringLiteral( "v" ), QStringLiteral( "s" ),
                                                 planted, config );
    REQUIRE( forward.has_value() );
    REQUIRE( reversed.has_value() );
    REQUIRE( forward.value().findings().size() >= 3 );
    CHECK( forward.value().findings().size() == reversed.value().findings().size() );

    // Serializations must be byte-identical: same findings, same order.
    CHECK( QJsonDocument( forward.value().toJson() ).toJson( QJsonDocument::Compact ) ==
           QJsonDocument( reversed.value().toJson() ).toJson( QJsonDocument::Compact ) );

    // And the order is the canonical (sampleA, sampleB, kind) order.
    const QVector<LeakageFinding> findings = forward.value().findings();
    for ( int i = 1; i < findings.size(); ++i )
    {
        const auto &previous = findings.at( i - 1 );
        const auto &current = findings.at( i );
        CHECK( ( previous.sampleA < current.sampleA ||
                 ( previous.sampleA == current.sampleA &&
                   ( previous.sampleB < current.sampleB ||
                     ( previous.sampleB == current.sampleB &&
                       leakageKindToString( previous.kind ) <=
                           leakageKindToString( current.kind ) ) ) ) ) );
    }
}

TEST_CASE( "fold audit reports honest gaps and per-fold findings",
           "[dataset][leakage][fold][coverage]" )
{
    // Build a k-fold manifest, then audit every fold. The digest-unknown
    // count must quantify the un-audited surface honestly (no samples carry
    // digests here), and the replay check must actually run and match.
    const int count = 40;
    QVector<SplitInput> inputs;
    for ( int i = 0; i < count; ++i )
    {
        SplitInput input;
        input.sampleId = QStringLiteral( "f%1" ).arg( i, 3, 10, QLatin1Char( '0' ) );
        input.classCode = QStringLiteral( "class-%1" ).arg( i % 2 );
        input.validBounds = true;
        inputs.append( input );
    }
    SplitConfig config;
    config.method = SplitMethod::KFold;
    config.seed = 17;
    config.foldCount = 4;
    const auto manifest = SplitEngine::generate( config, QStringLiteral( "version-f" ), inputs );
    REQUIRE( manifest.has_value() );

    QVector<AuditSample> samples;
    for ( const SplitInput &input : inputs )
    {
        AuditSample sample;
        sample.input = input;
        sample.fold = manifest.value().assignmentOf( input.sampleId )->fold;
        samples.append( sample );
    }
    const auto summary = FoldAuditor::auditFolds( manifest.value(), inputs );
    REQUIRE( summary.has_value() );
    CHECK( summary.value().foldCount == 4 );
    CHECK( summary.value().folds.size() == 4 );
    CHECK( summary.value().replayVerified );
    CHECK( summary.value().replayMatches );

    // Honest gaps: no digest evidence was supplied — every fold report must
    // quantify that gap instead of claiming an unevidenced "clean".
    for ( const FoldAuditItem &item : summary.value().folds )
    {
        CHECK( item.report.digestUnknownCount() == count );
        CHECK( item.report.sampleCount() == count );
    }
}
