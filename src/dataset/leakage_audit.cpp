// leakage_audit.cpp — leakage check implementations.
#include "leakage_audit.h"

#include <QHash>
#include <QJsonArray>

#include <algorithm>
#include <cmath>
#include <functional>

namespace sicnu::dataset
{

namespace
{

constexpr const char *kAllChecks[] = {
    "exact_duplicate",         "overlapping_patch",       "same_parent_polygon",
    "same_source_object",      "same_source_scene",       "distance_below_threshold",
    "buffer_overlap",          "augmentation_parent_leakage",
    "pseudo_label_parent_leakage", "temporal_future_leakage",
    "same_event_crossing",     "same_temporal_group_crossing", "pre_post_pair_leakage",
};

bool checkEnabled( const LeakageAuditConfig &config, const QString &name,
                   const QStringList &audited )
{
    if ( !config.checks.isEmpty() )
        return config.checks.contains( name );
    return audited.contains( name );
}

/// x-grid bucketing for spatial pair checks: cell size = max(threshold, 1.0).
qint64 bucketOf( double x, double cell )
{
    return qint64( std::floor( x / cell ) );
}

void addFinding( LeakageReport &report, LeakageKind kind, const AuditSample &a,
                 const AuditSample &b, const QJsonObject &evidence, bool foldBased )
{
    LeakageFinding finding;
    finding.kind = kind;
    // Findings are data-integrity errors; the reporter may downgrade via
    // evidence inspection later. Severity here: pseudo-label and temporal
    // future leakage are errors; overlap/distance are warnings when the
    // pair still shares a class (evidence carries classCode).
    finding.severity = DiagnosticSeverity::Error;
    if ( kind == LeakageKind::OverlappingPatch || kind == LeakageKind::DistanceBelowThreshold ||
         kind == LeakageKind::BufferOverlap || kind == LeakageKind::SameSourceScene )
    {
        finding.severity = a.input.classCode == b.input.classCode
                               ? DiagnosticSeverity::Error
                               : DiagnosticSeverity::Warning;
    }
    finding.sampleA = a.input.sampleId < b.input.sampleId ? a.input.sampleId : b.input.sampleId;
    finding.sampleB = a.input.sampleId < b.input.sampleId ? b.input.sampleId : a.input.sampleId;
    QJsonObject proof = evidence;
    if ( foldBased )
    {
        proof.insert( QStringLiteral( "fold_a" ), a.fold );
        proof.insert( QStringLiteral( "fold_b" ), b.fold );
    }
    else
    {
        proof.insert( QStringLiteral( "role_a" ), splitRoleToString( a.role ) );
        proof.insert( QStringLiteral( "role_b" ), splitRoleToString( b.role ) );
    }
    finding.evidence = proof;
    report.findings().append( finding );
}

} // namespace

QJsonObject LeakageAuditConfig::toJson() const
{
    QJsonObject json;
    if ( !checks.isEmpty() )
        json.insert( QStringLiteral( "checks" ), QJsonArray::fromStringList( checks ) );
    if ( distanceThreshold != 0.0 )
        json.insert( QStringLiteral( "distance_threshold" ), distanceThreshold );
    if ( overlapFractionThreshold != 0.0 )
        json.insert( QStringLiteral( "overlap_fraction_threshold" ), overlapFractionThreshold );
    if ( bufferDistance != 0.0 )
        json.insert( QStringLiteral( "buffer_distance" ), bufferDistance );
    json.insert( QStringLiteral( "cross_split_only" ), crossSplitOnly );
    return json;
}

sicnu::data::Result<LeakageAuditConfig> LeakageAuditConfig::fromJson( const QJsonObject &json )
{
    using Result = sicnu::data::Result<LeakageAuditConfig>;
    LeakageAuditConfig config;
    config.checks = json.value( QStringLiteral( "checks" ) ).toVariant().toStringList();
    for ( const QString &name : config.checks )
    {
        if ( !leakageKindFromString( name ).has_value() )
        {
            return Result::failure( Diagnostic{
                QStringLiteral( "dataset.leakage_unknown_check" ),
                QStringLiteral( "unknown leakage check '%1'" ).arg( name ),
                DiagnosticSeverity::Error } );
        }
    }
    config.distanceThreshold = json.value( QStringLiteral( "distance_threshold" ) ).toDouble();
    config.overlapFractionThreshold =
        json.value( QStringLiteral( "overlap_fraction_threshold" ) ).toDouble();
    config.bufferDistance = json.value( QStringLiteral( "buffer_distance" ) ).toDouble();
    config.crossSplitOnly = json.value( QStringLiteral( "cross_split_only" ) ).toBool( true );
    return Result::success( config );
}

QJsonObject LeakageFinding::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "kind" ), leakageKindToString( kind ) );
    json.insert( QStringLiteral( "severity" ),
                 severity == DiagnosticSeverity::Error
                     ? QStringLiteral( "error" )
                     : ( severity == DiagnosticSeverity::Warning ? QStringLiteral( "warning" )
                                                                 : QStringLiteral( "info" ) ) );
    json.insert( QStringLiteral( "sample_a" ), sampleA );
    json.insert( QStringLiteral( "sample_b" ), sampleB );
    json.insert( QStringLiteral( "evidence" ), evidence );
    return json;
}

sicnu::data::Result<LeakageFinding> LeakageFinding::fromJson( const QJsonObject &json )
{
    using Result = sicnu::data::Result<LeakageFinding>;
    LeakageFinding finding;
    const auto kind = leakageKindFromString( json.value( QStringLiteral( "kind" ) ).toString() );
    if ( !kind )
        return Result::failure( Diagnostic{ QStringLiteral( "dataset.leakage_corrupt" ),
                                            QStringLiteral( "finding kind unknown" ),
                                            DiagnosticSeverity::Error } );
    finding.kind = *kind;
    const QString severity = json.value( QStringLiteral( "severity" ) ).toString();
    finding.severity = severity == QStringLiteral( "warning" )
                           ? DiagnosticSeverity::Warning
                           : ( severity == QStringLiteral( "info" ) ? DiagnosticSeverity::Info
                                                                    : DiagnosticSeverity::Error );
    finding.sampleA = json.value( QStringLiteral( "sample_a" ) ).toString();
    finding.sampleB = json.value( QStringLiteral( "sample_b" ) ).toString();
    finding.evidence = json.value( QStringLiteral( "evidence" ) ).toObject();
    if ( finding.sampleA.isEmpty() || finding.sampleB.isEmpty() )
        return Result::failure( Diagnostic{ QStringLiteral( "dataset.leakage_corrupt" ),
                                            QStringLiteral( "finding without sample refs" ),
                                            DiagnosticSeverity::Error } );
    return Result::success( finding );
}

QJsonObject LeakageReport::summary() const
{
    QJsonObject summary;
    summary.insert( QStringLiteral( "clean" ), isClean() );
    summary.insert( QStringLiteral( "finding_count" ), qint64( m_findings.size() ) );
    summary.insert( QStringLiteral( "audited_checks" ),
                    QJsonArray::fromStringList( m_auditedChecks ) );
    QJsonObject byKind;
    QJsonObject bySeverity;
    for ( const LeakageFinding &finding : m_findings )
    {
        const QString kind = leakageKindToString( finding.kind );
        byKind.insert( kind, byKind.value( kind ).toInteger() + 1 );
        const QString severity =
            finding.severity == DiagnosticSeverity::Error
                ? QStringLiteral( "error" )
                : ( finding.severity == DiagnosticSeverity::Warning
                        ? QStringLiteral( "warning" )
                        : QStringLiteral( "info" ) );
        bySeverity.insert( severity, bySeverity.value( severity ).toInteger() + 1 );
    }
    summary.insert( QStringLiteral( "by_kind" ), byKind );
    summary.insert( QStringLiteral( "by_severity" ), bySeverity );
    return summary;
}

QJsonObject LeakageReport::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "schema_version" ), kLeakageReportSerializationVersion );
    json.insert( QStringLiteral( "dataset_version_id" ), m_datasetVersionId );
    json.insert( QStringLiteral( "split_manifest_id" ), m_splitManifestId );
    json.insert( QStringLiteral( "audited_checks" ),
                QJsonArray::fromStringList( m_auditedChecks ) );
    QJsonArray findingArray;
    for ( const LeakageFinding &finding : m_findings )
        findingArray.append( finding.toJson() );
    json.insert( QStringLiteral( "findings" ), findingArray );
    json.insert( QStringLiteral( "summary" ), summary() );
    return json;
}

sicnu::data::Result<LeakageReport> LeakageReport::fromJson( const QJsonObject &json )
{
    using Result = sicnu::data::Result<LeakageReport>;
    const qint64 schemaVersion = json.value( QStringLiteral( "schema_version" ) ).toInteger();
    if ( schemaVersion != kLeakageReportSerializationVersion )
    {
        return Result::failure( Diagnostic{
            QStringLiteral( "dataset.leakage_version" ),
            QStringLiteral( "leakage report version %1 not supported (expected %2)" )
                .arg( schemaVersion )
                .arg( kLeakageReportSerializationVersion ),
            DiagnosticSeverity::Error } );
    }
    LeakageReport report;
    report.m_datasetVersionId = json.value( QStringLiteral( "dataset_version_id" ) ).toString();
    report.m_splitManifestId = json.value( QStringLiteral( "split_manifest_id" ) ).toString();
    report.m_auditedChecks =
        json.value( QStringLiteral( "audited_checks" ) ).toVariant().toStringList();
    for ( const QJsonValue &value : json.value( QStringLiteral( "findings" ) ).toArray() )
    {
        auto finding = LeakageFinding::fromJson( value.toObject() );
        if ( !finding )
            return Result::failure( finding.diagnostics() );
        report.m_findings.append( finding.value() );
    }
    return Result::success( report );
}

bool LeakageAuditor::crossSplit( const AuditSample &a, const AuditSample &b, bool foldBased )
{
    if ( foldBased )
        return a.fold >= 0 && b.fold >= 0 && a.fold != b.fold;
    return a.role != b.role;
}

sicnu::data::Result<LeakageReport> LeakageAuditor::audit( const QString &datasetVersionId,
                                                          const QString &splitManifestId,
                                                          const QVector<AuditSample> &samples,
                                                          const LeakageAuditConfig &config )
{
    using Result = sicnu::data::Result<LeakageReport>;
    if ( samples.isEmpty() )
        return Result::failure( Diagnostic{ QStringLiteral( "dataset.leakage_empty" ),
                                            QStringLiteral( "no samples to audit" ),
                                            DiagnosticSeverity::Error } );

    const bool foldBased = std::any_of( samples.cbegin(), samples.cend(),
                                        []( const AuditSample &sample ) {
                                            return sample.fold >= 0;
                                        } );

    LeakageReport report;
    report.setDatasetVersionId( datasetVersionId );
    report.setSplitManifestId( splitManifestId );

    // The checks the evidence could support; a named-but-unrunnable check is
    // dropped from auditedChecks (the report never claims it ran).
    QStringList supported;
    for ( const char *name : kAllChecks )
        supported.append( QString::fromLatin1( name ) );
    if ( foldBased )
    {
        // Temporal future leakage is defined against final train/test roles;
        // fold manifests materialize per fold, so this check would need a
        // per-fold audit — it is NOT claimed here (goal §19 honesty rule).
        supported.removeAll( QStringLiteral( "temporal_future_leakage" ) );
    }
    QStringList audited;
    for ( const QString &name : supported )
    {
        if ( checkEnabled( config, name, supported ) )
            audited.append( name );
    }
    report.setAuditedChecks( audited );

    const int count = samples.size();

    // --- exact duplicates (hash map) ---------------------------------------
    if ( checkEnabled( config, QStringLiteral( "exact_duplicate" ), supported ) )
    {
        QHash<QString, QVector<int>> byDigest;
        for ( int i = 0; i < count; ++i )
        {
            if ( !samples.at( i ).contentDigest.isEmpty() )
                byDigest[samples.at( i ).contentDigest].append( i );
        }
        for ( auto it = byDigest.constBegin(); it != byDigest.constEnd(); ++it )
        {
            const QVector<int> &indices = it.value();
            for ( int a = 0; a < indices.size(); ++a )
            {
                for ( int b = a + 1; b < indices.size(); ++b )
                {
                    const AuditSample &left = samples.at( indices.at( a ) );
                    const AuditSample &right = samples.at( indices.at( b ) );
                    if ( config.crossSplitOnly && !crossSplit( left, right, foldBased ) )
                        continue;
                    QJsonObject evidence;
                    evidence.insert( QStringLiteral( "digest" ), it.key() );
                    addFinding( report, LeakageKind::ExactDuplicate, left, right, evidence,
                                foldBased );
                }
            }
        }
    }

    // --- hash-keyed parent/group collisions ---------------------------------
    auto hashKeyedCheck = [&]( LeakageKind kind,
                               const std::function<QString( const AuditSample & )> &keyFn ) {
        QHash<QString, QVector<int>> byKey;
        for ( int i = 0; i < count; ++i )
        {
            const QString key = keyFn( samples.at( i ) );
            if ( !key.isEmpty() )
                byKey[key].append( i );
        }
        for ( auto it = byKey.constBegin(); it != byKey.constEnd(); ++it )
        {
            const QVector<int> &indices = it.value();
            for ( int a = 0; a < indices.size(); ++a )
            {
                for ( int b = a + 1; b < indices.size(); ++b )
                {
                    const AuditSample &left = samples.at( indices.at( a ) );
                    const AuditSample &right = samples.at( indices.at( b ) );
                    if ( config.crossSplitOnly && !crossSplit( left, right, foldBased ) )
                        continue;
                    QJsonObject evidence;
                    evidence.insert( QStringLiteral( "key" ), it.key() );
                    addFinding( report, kind, left, right, evidence, foldBased );
                }
            }
        }
    };

    if ( checkEnabled( config, QStringLiteral( "same_parent_polygon" ), supported ) )
        hashKeyedCheck( LeakageKind::SameParentPolygon,
                        []( const AuditSample &s ) { return s.parentPolygonId; } );
    if ( checkEnabled( config, QStringLiteral( "same_source_object" ), supported ) )
        hashKeyedCheck( LeakageKind::SameSourceObject,
                        []( const AuditSample &s ) { return s.sourceObjectId; } );
    if ( checkEnabled( config, QStringLiteral( "same_source_scene" ), supported ) )
        hashKeyedCheck( LeakageKind::SameSourceScene,
                        []( const AuditSample &s ) { return s.sceneId; } );
    if ( checkEnabled( config, QStringLiteral( "same_event_crossing" ), supported ) )
        hashKeyedCheck( LeakageKind::SameEventCrossing,
                        []( const AuditSample &s ) { return s.eventGroup; } );
    if ( checkEnabled( config, QStringLiteral( "same_temporal_group_crossing" ), supported ) )
        hashKeyedCheck( LeakageKind::SameTemporalGroupCrossing,
                        []( const AuditSample &s ) { return s.input.groupId; } );

    // --- parent-sample leakage (augmentation / pseudo) -----------------------
    auto parentLeakCheck = [&]( const QString &check, LeakageKind kind, bool pseudoOnly ) {
        // Parent must participate in the same audit set for the edge to be
        // observable; otherwise the parent reference is dangling (reported
        // by lineage, not leakage).
        QHash<QString, int> byId;
        for ( int i = 0; i < count; ++i )
            byId.insert( samples.at( i ).input.sampleId, i );
        for ( int i = 0; i < count; ++i )
        {
            const AuditSample &sample = samples.at( i );
            if ( pseudoOnly && !sample.pseudoDerived )
                continue;
            if ( sample.parentSampleId.isEmpty() )
                continue;
            const auto parentIt = byId.constFind( sample.parentSampleId );
            if ( parentIt == byId.constEnd() )
                continue;
            const AuditSample &parent = samples.at( *parentIt );
            if ( config.crossSplitOnly && !crossSplit( sample, parent, foldBased ) )
                continue;
            QJsonObject evidence;
            evidence.insert( QStringLiteral( "parent_sample" ), parent.input.sampleId );
            addFinding( report, kind, sample, parent, evidence, foldBased );
        }
    };
    if ( checkEnabled( config, QStringLiteral( "augmentation_parent_leakage" ), supported ) )
        parentLeakCheck( QStringLiteral( "augmentation_parent_leakage" ),
                         LeakageKind::AugmentationParentLeakage, false );
    if ( checkEnabled( config, QStringLiteral( "pseudo_label_parent_leakage" ), supported ) )
        parentLeakCheck( QStringLiteral( "pseudo_label_parent_leakage" ),
                         LeakageKind::PseudoLabelParentLeakage, true );

    // --- pre/post pair leakage -------------------------------------------------
    if ( checkEnabled( config, QStringLiteral( "pre_post_pair_leakage" ), supported ) )
    {
        QHash<QString, int> byId;
        for ( int i = 0; i < count; ++i )
            byId.insert( samples.at( i ).input.sampleId, i );
        for ( int i = 0; i < count; ++i )
        {
            const AuditSample &sample = samples.at( i );
            if ( sample.pairCounterpartId.isEmpty() )
                continue;
            const auto otherIt = byId.constFind( sample.pairCounterpartId );
            if ( otherIt == byId.constEnd() )
                continue;
            const AuditSample &other = samples.at( *otherIt );
            if ( config.crossSplitOnly && !crossSplit( sample, other, foldBased ) )
                continue;
            QJsonObject evidence;
            evidence.insert( QStringLiteral( "counterpart" ), other.input.sampleId );
            addFinding( report, LeakageKind::PrePostPairLeakage, sample, other, evidence,
                        foldBased );
        }
    }

    // --- temporal future leakage -------------------------------------------------
    if ( checkEnabled( config, QStringLiteral( "temporal_future_leakage" ), supported ) )
    {
        // Same group where a TRAIN sample is observed at/after a TEST sample:
        // the model would train on the future of a test-series.
        QHash<QString, QVector<int>> byGroup;
        for ( int i = 0; i < count; ++i )
        {
            if ( samples.at( i ).input.timeMs > 0 )
                byGroup[samples.at( i ).input.groupId].append( i );
        }
        for ( auto it = byGroup.constBegin(); it != byGroup.constEnd(); ++it )
        {
            const QVector<int> &indices = it.value();
            for ( int a = 0; a < indices.size(); ++a )
            {
                for ( int b = a + 1; b < indices.size(); ++b )
                {
                    const AuditSample &left = samples.at( indices.at( a ) );
                    const AuditSample &right = samples.at( indices.at( b ) );
                    if ( !crossSplit( left, right, foldBased ) )
                        continue;
                    const AuditSample &trainSample =
                        left.role == SplitRole::Train ? left : right;
                    const AuditSample &testSample =
                        left.role == SplitRole::Train ? right : left;
                    if ( trainSample.input.timeMs < testSample.input.timeMs )
                        continue;
                    QJsonObject evidence;
                    evidence.insert( QStringLiteral( "train_time_ms" ),
                                     trainSample.input.timeMs );
                    evidence.insert( QStringLiteral( "test_time_ms" ), testSample.input.timeMs );
                    addFinding( report, LeakageKind::TemporalFutureLeakage, left, right,
                                evidence, foldBased );
                }
            }
        }
    }

    // --- spatial pair checks (bucketed) --------------------------------------
    const bool overlapCheck =
        checkEnabled( config, QStringLiteral( "overlapping_patch" ), supported );
    const bool distanceCheck =
        config.distanceThreshold > 0.0 &&
        checkEnabled( config, QStringLiteral( "distance_below_threshold" ), supported );
    const bool bufferCheck =
        config.bufferDistance > 0.0 &&
        checkEnabled( config, QStringLiteral( "buffer_overlap" ), supported );
    if ( overlapCheck || distanceCheck || bufferCheck )
    {
        const double cell = qMax( 1.0, qMax( config.distanceThreshold, config.bufferDistance ) );
        QHash<qint64, QVector<int>> buckets;
        for ( int i = 0; i < count; ++i )
        {
            const AuditSample &sample = samples.at( i );
            if ( sample.input.validBounds )
                buckets[bucketOf( ( sample.input.minX + sample.input.maxX ) / 2.0, cell )]
                    .append( i );
        }
        for ( auto it = buckets.constBegin(); it != buckets.constEnd(); ++it )
        {
            const QVector<int> &bucketIndices = it.value();
            for ( int a = 0; a < bucketIndices.size(); ++a )
            {
                for ( int b = a + 1; b < bucketIndices.size(); ++b )
                {
                    const AuditSample &left = samples.at( bucketIndices.at( a ) );
                    const AuditSample &right = samples.at( bucketIndices.at( b ) );
                    if ( config.crossSplitOnly && !crossSplit( left, right, foldBased ) )
                        continue;
                    const double distance = qMax(
                        0.0,
                        qMax( qMax( left.input.minX - right.input.maxX,
                                    right.input.minX - left.input.maxX ),
                              qMax( left.input.minY - right.input.maxY,
                                    right.input.minY - left.input.maxY ) ) );
                    if ( distanceCheck && distance < config.distanceThreshold )
                    {
                        QJsonObject evidence;
                        evidence.insert( QStringLiteral( "distance" ), distance );
                        addFinding( report, LeakageKind::DistanceBelowThreshold, left, right,
                                    evidence, foldBased );
                    }
                    if ( bufferCheck && distance < config.bufferDistance )
                    {
                        QJsonObject evidence;
                        evidence.insert( QStringLiteral( "buffer_distance" ),
                                         config.bufferDistance );
                        addFinding( report, LeakageKind::BufferOverlap, left, right, evidence,
                                    foldBased );
                    }
                    if ( overlapCheck && distance <= 0.0 && left.windowWidth > 0.0 &&
                         left.windowHeight > 0.0 )
                    {
                        // Overlap fraction on the ground bounds relative to
                        // the smaller footprint area.
                        const double intersectionWidth =
                            qMin( left.input.maxX, right.input.maxX ) -
                            qMax( left.input.minX, right.input.minX );
                        const double intersectionHeight =
                            qMin( left.input.maxY, right.input.maxY ) -
                            qMax( left.input.minY, right.input.minY );
                        if ( intersectionWidth > 0.0 && intersectionHeight > 0.0 )
                        {
                            const double areaA =
                                ( left.input.maxX - left.input.minX ) *
                                ( left.input.maxY - left.input.minY );
                            const double areaB =
                                ( right.input.maxX - right.input.minX ) *
                                ( right.input.maxY - right.input.minY );
                            const double intersection =
                                intersectionWidth * intersectionHeight;
                            const double fraction =
                                intersection / qMin( areaA, qMax( areaB, 1e-12 ) );
                            if ( fraction >= qMax( config.overlapFractionThreshold, 0.0 ) )
                            {
                                QJsonObject evidence;
                                evidence.insert( QStringLiteral( "overlap_fraction" ), fraction );
                                addFinding( report, LeakageKind::OverlappingPatch, left, right,
                                            evidence, foldBased );
                            }
                        }
                    }
                }
            }
        }
    }

    // Stable order: findings sorted by (sampleA, sampleB, kind) so identical
    // inputs produce identical reports.
    std::sort( report.findings().begin(), report.findings().end(),
               []( const LeakageFinding &a, const LeakageFinding &b ) {
                   if ( a.sampleA != b.sampleA )
                       return a.sampleA < b.sampleA;
                   if ( a.sampleB != b.sampleB )
                       return a.sampleB < b.sampleB;
                   return leakageKindToString( a.kind ) < leakageKindToString( b.kind );
               } );
    return Result::success( report );
}

} // namespace sicnu::dataset
