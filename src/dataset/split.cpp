// split.cpp — deterministic split engines.
#include "split.h"

#include "deterministic_random.h"
#include "dataset_fingerprint.h"

#include <QHash>
#include <QJsonArray>
#include <QMap>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace sicnu::dataset
{

namespace
{

/// Internal control-flow signal for degenerate grouped/temporal walks;
/// caught at the generate() boundary and converted to a typed diagnostic.
struct SplitRoleDegenerate
{
};

Diagnostic splitError( const QString &message )
{
    return Diagnostic{ QStringLiteral( "dataset.split_invalid" ), message,
                       DiagnosticSeverity::Error };
}

bool ratioSumValid( double train, double validation, double test )
{
    return std::abs( train + validation + test - 1.0 ) <= 1e-9;
}

struct IdOrder
{
    bool operator()( const SplitInput &a, const SplitInput &b ) const
    {
        return a.sampleId < b.sampleId;
    }
};

/// Hare-Niemeyer largest-remainder distribution of `total` slots over the
/// three roles. A zero ratio pins its role to zero slots no matter how the
/// remainders fall; the seats freed that way always land on a role the
/// configuration actually requested. Ties on the fractional remainder break
/// deterministically Train > Validation > Test, so the result depends only on
/// (ratios, total) — never on iteration order or accumulation drift.
/// (#788: floor-then-remainder-to-Test starved small Train ratios and let
/// nonzero remainders violate a testRatio of 0.)
void largestRemainderCounts( double train, double validation, double test, int total,
                             int &trainCount, int &validationCount, int &testCount )
{
    const double exact[3] = { train * total, validation * total, test * total };
    int floors[3] = { int( std::floor( exact[0] ) ), int( std::floor( exact[1] ) ),
                      int( std::floor( exact[2] ) ) };
    const double ratios[3] = { train, validation, test };
    for ( int role = 0; role < 3; ++role )
    {
        if ( ratios[role] <= 0.0 )
            floors[role] = 0; // a zero ratio is a hard contract, not a quota
    }
    int seats = total - ( floors[0] + floors[1] + floors[2] );
    // Ratios sum to 1, so seats can only be >= 0 when a zero-ratio clamp
    // freed quota (or float slack); distribute largest-remainder first.
    // At most ONE extra seat per role (Hamilton/Hare-Niemeyer cap): with
    // seats <= 2 a role whose fraction dominates must not take every seat
    // (0.7/0.15/0.15 at 64: floors leave 2 seats → 45/10/9, never 46/9/9).
    bool granted[3] = { false, false, false };
    const int order[3] = { 0, 1, 2 }; // tie-break priority: Train, Validation, Test
    while ( seats > 0 )
    {
        int best = -1;
        double bestRemainder = -1.0;
        for ( const int role : order )
        {
            if ( ratios[role] <= 0.0 || granted[role] )
                continue;
            const double remainder = exact[role] - std::floor( exact[role] );
            if ( remainder > bestRemainder + 1e-12 )
            {
                bestRemainder = remainder;
                best = role;
            }
        }
        if ( best < 0 )
            break; // nothing may receive (all ratios zero) — callers validated ratios
        ++floors[best];
        granted[best] = true;
        --seats;
    }
    // Seats < 0 cannot happen with ratio sums <= 1 + 1e-9 and the zero clamps;
    // clamp defensively so counts stay consistent regardless.
    while ( seats < 0 )
    {
        int worst = -1;
        int worstCount = std::numeric_limits<int>::max();
        for ( const int role : order )
        {
            if ( floors[role] > 0 && floors[role] < worstCount )
            {
                worstCount = floors[role];
                worst = role;
            }
        }
        if ( worst < 0 )
            break;
        --floors[worst];
        ++seats;
    }
    trainCount = floors[0];
    validationCount = floors[1];
    testCount = floors[2];
}

/// Deterministic role assignment by ratio over an already-shuffled list:
/// counts come from the largest-remainder distribution computed once, so the
/// assignment depends on the LIST CONTENT AND ORDER, never on accumulation
/// drift, and every declared ratio (including 0) is honored exactly.
void assignByRatio( const QVector<int> &indices, double train, double validation, double test,
                    QVector<SplitAssignment> &out, const QVector<SplitInput> &inputs )
{
    const int total = indices.size();
    int trainCount = 0;
    int validationCount = 0;
    int testCount = 0;
    largestRemainderCounts( train, validation, test, total, trainCount, validationCount,
                            testCount );
    for ( int position = 0; position < total; ++position )
    {
        SplitAssignment assignment;
        assignment.sampleId = inputs.at( indices.at( position ) ).sampleId;
        if ( position < trainCount )
            assignment.role = SplitRole::Train;
        else if ( position < trainCount + validationCount )
            assignment.role = SplitRole::Validation;
        else
            assignment.role = SplitRole::Test;
        out.append( assignment );
    }
}

/// Groups row indexes by an arbitrary key (own-group fallback for empty keys
/// keeps ungrouped samples splittable).
QMap<QString, QVector<int>> keyTable( const QVector<SplitInput> &inputs,
                                      const std::function<QString( const SplitInput & )> &keyFn )
{
    QMap<QString, QVector<int>> table;
    for ( int i = 0; i < inputs.size(); ++i )
    {
        const QString key = keyFn( inputs.at( i ) );
        table[key.isEmpty() ? QStringLiteral( "sample:%1" ).arg( inputs.at( i ).sampleId )
                            : key].append( i );
    }
    return table;
}

/// Whole-group budget walk: each group takes the first role whose sample
/// budget is still open (train, then validation, then the rest). Greedy and
/// deterministic - a group that straddles a budget boundary stays whole
/// (atomicity beats exact ratios, which is the point of grouping).
/// Degenerate-group guard: when a ratio is non-zero but its role ended up
/// empty (one giant group swallows everything), the split is REFUSED instead
/// of producing a silently unusable manifest.
void requireNonEmptyRoles( const QVector<SplitAssignment> &assignments,
                           const SplitConfig &config, int total )
{
    auto hasRole = [&]( SplitRole role ) {
        for ( const SplitAssignment &assignment : assignments )
        {
            if ( assignment.role == role )
                return true;
        }
        return false;
    };
    int trainTarget = 0;
    int validationTarget = 0;
    int testTarget = 0;
    largestRemainderCounts( config.trainRatio, config.validationRatio, config.testRatio, total,
                            trainTarget, validationTarget, testTarget );
    if ( trainTarget > 0 && !hasRole( SplitRole::Train ) )
        throw SplitRoleDegenerate{};
    if ( validationTarget > 0 && !hasRole( SplitRole::Validation ) )
        throw SplitRoleDegenerate{};
    if ( testTarget > 0 && !hasRole( SplitRole::Test ) )
        throw SplitRoleDegenerate{};
}

int roleIndex( SplitRole role )
{
    // Train/Validation/Test are the first three enumerators (dataset_types.h).
    return static_cast<int>( role );
}

/// Whole-group budget walk overflow: a group that no longer fits either
/// budget lands on the nonzero-ratio role furthest under its target
/// (deterministic Train > Validation > Test tie-break). A zero ratio never
/// receives overflow — that keeps `testRatio: 0` an actual contract for
/// grouped walks too, not just per-sample assignment (#788).
SplitRole overflowRole( const double (&ratios)[3], const int (&targets)[3],
                        const int (&assigned)[3] )
{
    int best = -1;
    int bestDeficit = std::numeric_limits<int>::min();
    for ( int role = 0; role < 3; ++role )
    {
        if ( ratios[role] <= 0.0 )
            continue;
        const int deficit = targets[role] - assigned[role];
        if ( deficit > bestDeficit )
        {
            bestDeficit = deficit;
            best = role;
        }
    }
    return best >= 0 ? static_cast<SplitRole>( best ) : SplitRole::Test;
}

QVector<SplitAssignment> walkGroupsInOrder( const QStringList &order,
                                            const QMap<QString, QVector<int>> &table,
                                            const QVector<SplitInput> &inputs,
                                            const SplitConfig &config )
{
    const int total = inputs.size();
    int trainTarget = 0;
    int validationTarget = 0;
    int testTarget = 0;
    largestRemainderCounts( config.trainRatio, config.validationRatio, config.testRatio, total,
                            trainTarget, validationTarget, testTarget );
    const double ratios[3] = { config.trainRatio, config.validationRatio, config.testRatio };
    const int targets[3] = { trainTarget, validationTarget, testTarget };

    // Greedy walk first: each group takes the role whose budget is open.
    struct GroupInfo
    {
        QString key;
        int size = 0;
        SplitRole role = SplitRole::Test;
    };
    QVector<GroupInfo> groups;
    int assigned[3] = { 0, 0, 0 };
    for ( const QString &key : order )
    {
        GroupInfo info;
        info.key = key;
        info.size = int( table.value( key ).size() );
        if ( assigned[0] < trainTarget )
            info.role = SplitRole::Train;
        else if ( assigned[0] + assigned[1] < trainTarget + validationTarget )
            info.role = SplitRole::Validation;
        else
            info.role = overflowRole( ratios, targets, assigned );
        assigned[roleIndex( info.role )] += info.size;
        groups.append( info );
    }

    // Degenerate repair: a non-zero-ratio role that ended up EMPTY steals the
    // LAST group of the role with the largest overshoot (deterministic, keeps
    // every group atomic). One giant group still refuses via the caller's
    // guard when nothing can be donated.
    auto roleCount = [&]( SplitRole role ) {
        int n = 0;
        for ( const GroupInfo &info : groups )
        {
            if ( info.role == role )
                n += info.size;
        }
        return n;
    };
    struct RoleSpec
    {
        SplitRole role;
        int target;
    };
    const RoleSpec specs[3] = { { SplitRole::Test, testTarget },
                                { SplitRole::Validation, validationTarget },
                                { SplitRole::Train, trainTarget } };
    // Emptiness repair to a fixpoint: a single pass can vacate a role it
    // just fixed (a later steal may take its only group), so the passes
    // repeat until every nonzero-target role is covered or nothing changes.
    // Donors that are the SOLE group of another nonzero-target role are
    // skipped unless there is no alternative — taking them trades one empty
    // role for another (adversarial review FINDING: e.g. group sizes
    // {2,8,2} at 0.81/0.10/0.09 used to refuse a split that has a valid
    // atomic assignment).
    for ( int pass = 0; pass <= groups.size(); ++pass )
    {
        bool repaired = false;
        for ( const RoleSpec &spec : specs )
        {
            if ( spec.target <= 0 || roleCount( spec.role ) > 0 )
                continue;
            // How many groups carry each nonzero-target role (sole-group guard).
            int groupCount[3] = { 0, 0, 0 };
            for ( const GroupInfo &info : groups )
            {
                if ( targets[roleIndex( info.role )] > 0 )
                    ++groupCount[roleIndex( info.role )];
            }
            int donorIndex = -1;
            int donorExcess = std::numeric_limits<int>::min();
            int fallbackIndex = -1;
            int fallbackExcess = std::numeric_limits<int>::min();
            for ( int i = groups.size() - 1; i >= 0; --i )
            {
                const GroupInfo &info = groups.at( i );
                if ( info.role == spec.role )
                    continue;
                const int donorRole = roleIndex( info.role );
                const int excess = roleCount( info.role ) - info.size - targets[donorRole];
                const bool soleDonor =
                    targets[donorRole] > 0 && groupCount[donorRole] <= 1;
                if ( excess >= donorExcess && !soleDonor )
                {
                    donorExcess = excess;
                    donorIndex = i;
                }
                if ( excess >= fallbackExcess )
                {
                    fallbackExcess = excess;
                    fallbackIndex = i;
                }
            }
            const int chosen = donorIndex >= 0 ? donorIndex : fallbackIndex;
            if ( chosen >= 0 )
            {
                groups[chosen].role = spec.role;
                repaired = true;
            }
        }
        if ( !repaired )
            break;
    }

    QVector<SplitAssignment> out;
    for ( const GroupInfo &info : groups )
    {
        for ( const int index : table.value( info.key ) )
        {
            SplitAssignment assignment;
            assignment.sampleId = inputs.at( index ).sampleId;
            assignment.role = info.role;
            out.append( assignment );
        }
    }
    return out;
}

QString groupKeyOf( const SplitInput &input, SplitMethod method )
{
    switch ( method )
    {
        case SplitMethod::LeaveOneSceneOut:
            return input.sceneId.isEmpty() ? input.groupId : input.sceneId;
        case SplitMethod::LeaveOneYearOut:
            return input.year > 0 ? QString::number( input.year ) : input.groupId;
        case SplitMethod::LeaveOneRegionOut:
        case SplitMethod::Grouped:
        case SplitMethod::GroupKFold:
            return input.groupId;
        case SplitMethod::Temporal:
            return input.groupId;
        default:
            return input.groupId;
    }
}

} // namespace

QJsonObject SplitConfig::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "method" ), splitMethodToString( method ) );
    json.insert( QStringLiteral( "train_ratio" ), trainRatio );
    json.insert( QStringLiteral( "validation_ratio" ), validationRatio );
    json.insert( QStringLiteral( "test_ratio" ), testRatio );
    json.insert( QStringLiteral( "seed_hex" ), QString::number( seed, 16 ) );
    if ( foldCount > 0 )
        json.insert( QStringLiteral( "fold_count" ), foldCount );
    if ( blockSizeX != 0.0 || blockSizeY != 0.0 )
    {
        json.insert( QStringLiteral( "block_size_x" ), blockSizeX );
        json.insert( QStringLiteral( "block_size_y" ), blockSizeY );
    }
    if ( bufferDistance != 0.0 )
        json.insert( QStringLiteral( "buffer_distance" ), bufferDistance );
    if ( !regionKey.isEmpty() )
        json.insert( QStringLiteral( "region_key" ), regionKey );
    if ( !extra.isEmpty() )
        json.insert( QStringLiteral( "extra" ), extra );
    return json;
}

sicnu::data::Result<SplitConfig> SplitConfig::fromJson( const QJsonObject &json )
{
    using Result = sicnu::data::Result<SplitConfig>;
    SplitConfig config;
    const auto method =
        splitMethodFromString( json.value( QStringLiteral( "method" ) ).toString() );
    if ( !method )
        return Result::failure( splitError( QStringLiteral( "split method unknown" ) ) );
    config.method = *method;
    config.trainRatio = json.value( QStringLiteral( "train_ratio" ) ).toDouble( 0.7 );
    config.validationRatio = json.value( QStringLiteral( "validation_ratio" ) ).toDouble( 0.15 );
    config.testRatio = json.value( QStringLiteral( "test_ratio" ) ).toDouble( 0.15 );
    {
        const QString seedHex = json.value( QStringLiteral( "seed_hex" ) ).toString();
        if ( !seedHex.isEmpty() )
        {
            bool ok = false;
            config.seed = seedHex.toULongLong( &ok, 16 );
            if ( !ok )
                return Result::failure( splitError( QStringLiteral( "seed_hex malformed" ) ) );
        }
    }
    config.foldCount = json.value( QStringLiteral( "fold_count" ) ).toInt( 5 );
    config.blockSizeX = json.value( QStringLiteral( "block_size_x" ) ).toDouble();
    config.blockSizeY = json.value( QStringLiteral( "block_size_y" ) ).toDouble();
    config.bufferDistance = json.value( QStringLiteral( "buffer_distance" ) ).toDouble();
    config.regionKey = json.value( QStringLiteral( "region_key" ) ).toString();
    config.extra = json.value( QStringLiteral( "extra" ) ).toObject();
    const auto validated = config.validate();
    if ( !validated )
        return Result::failure( validated.diagnostics() );
    return Result::success( config );
}

sicnu::data::Result<void> SplitConfig::validate() const
{
    using Result = sicnu::data::Result<void>;
    if ( SplitEngine::methodUsesFolds( method ) )
    {
        if ( foldCount < 2 )
            return Result::failure( splitError( QStringLiteral( "fold count must be >= 2" ) ) );
    }
    else if ( !ratioSumValid( trainRatio, validationRatio, testRatio ) )
    {
        return Result::failure( splitError( QStringLiteral( "train/val/test ratios must sum to 1" ) ) );
    }
    if ( ( method == SplitMethod::SpatialBlock || method == SplitMethod::SpatialKFold ) &&
         ( blockSizeX <= 0.0 || blockSizeY <= 0.0 ) )
        return Result::failure( splitError( QStringLiteral( "spatial block methods require positive block sizes" ) ) );
    if ( method == SplitMethod::SpatialBuffer && bufferDistance <= 0.0 )
        return Result::failure( splitError( QStringLiteral( "spatial_buffer requires a buffer distance" ) ) );
    if ( method == SplitMethod::LeaveOneRegionOut && regionKey.isEmpty() )
        return Result::failure( splitError( QStringLiteral( "leave_one_region_out requires region_key" ) ) );
    return Result::success();
}

QStringList SplitManifest::sampleIdsOfRole( SplitRole role ) const
{
    QStringList ids;
    for ( const SplitAssignment &assignment : m_assignments )
    {
        if ( assignment.role == role )
            ids.append( assignment.sampleId );
    }
    return ids;
}

std::optional<QVector<SplitAssignment>> SplitManifest::materializeFold( int foldIndex ) const
{
    if ( !SplitEngine::methodUsesFolds( m_config.method ) || m_assignments.isEmpty() )
        return std::nullopt;
    bool anyFoldMatch = false;
    QVector<SplitAssignment> materialized;
    materialized.reserve( m_assignments.size() );
    for ( const SplitAssignment &assignment : m_assignments )
    {
        SplitAssignment mapped = assignment;
        if ( assignment.fold == foldIndex )
        {
            mapped.role = SplitRole::Test;
            anyFoldMatch = true;
        }
        else
        {
            mapped.role = SplitRole::Train;
        }
        materialized.append( mapped );
    }
    if ( !anyFoldMatch )
        return std::nullopt;
    return materialized;
}

std::optional<SplitAssignment> SplitManifest::assignmentOf( const QString &sampleId ) const
{
    for ( const SplitAssignment &assignment : m_assignments )
    {
        if ( assignment.sampleId == sampleId )
            return assignment;
    }
    return std::nullopt;
}

QJsonObject SplitManifest::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "schema_version" ), kSplitManifestSerializationVersion );
    json.insert( QStringLiteral( "manifest_id" ), m_manifestId );
    json.insert( QStringLiteral( "dataset_version_id" ), m_datasetVersionId );
    json.insert( QStringLiteral( "config" ), m_config.toJson() );
    json.insert( QStringLiteral( "determinism" ), determinismGradeToString( m_determinism ) );
    if ( !m_determinismNote.isEmpty() )
        json.insert( QStringLiteral( "determinism_note" ), m_determinismNote );
    QJsonArray assignmentArray;
    for ( const SplitAssignment &assignment : m_assignments )
    {
        QJsonObject item;
        item.insert( QStringLiteral( "sample_id" ), assignment.sampleId );
        item.insert( QStringLiteral( "role" ), splitRoleToString( assignment.role ) );
        if ( assignment.fold >= 0 )
            item.insert( QStringLiteral( "fold" ), assignment.fold );
        assignmentArray.append( item );
    }
    json.insert( QStringLiteral( "assignments" ), assignmentArray );
    if ( !m_leakageSummary.isEmpty() )
        json.insert( QStringLiteral( "leakage_summary" ), m_leakageSummary );
    if ( m_createdAtUtc.isValid() )
        json.insert( QStringLiteral( "created_at_utc" ),
                     m_createdAtUtc.toString( Qt::ISODateWithMs ) );
    if ( !m_note.isEmpty() )
        json.insert( QStringLiteral( "note" ), m_note );
    if ( !m_fingerprint.isEmpty() )
        json.insert( QStringLiteral( "fingerprint" ), m_fingerprint );
    return json;
}

sicnu::data::Result<SplitManifest> SplitManifest::fromJson( const QJsonObject &json )
{
    using Result = sicnu::data::Result<SplitManifest>;
    const qint64 schemaVersion = json.value( QStringLiteral( "schema_version" ) ).toInteger();
    if ( schemaVersion != kSplitManifestSerializationVersion )
    {
        return Result::failure( Diagnostic{
            QStringLiteral( "dataset.split_version" ),
            QStringLiteral( "split manifest version %1 not supported (expected %2)" )
                .arg( schemaVersion )
                .arg( kSplitManifestSerializationVersion ),
            DiagnosticSeverity::Error,
        } );
    }
    SplitManifest manifest;
    manifest.m_manifestId = json.value( QStringLiteral( "manifest_id" ) ).toString();
    manifest.m_datasetVersionId = json.value( QStringLiteral( "dataset_version_id" ) ).toString();
    if ( manifest.m_manifestId.isEmpty() || manifest.m_datasetVersionId.isEmpty() )
        return Result::failure( splitError( QStringLiteral( "manifest requires ids" ) ) );

    const auto config = SplitConfig::fromJson( json.value( QStringLiteral( "config" ) ).toObject() );
    if ( !config )
        return Result::failure( config.diagnostics() );
    manifest.m_config = config.value();

    const auto determinism = determinismGradeFromString(
        json.value( QStringLiteral( "determinism" ) ).toString() );
    if ( !determinism )
        return Result::failure( splitError( QStringLiteral( "determinism grade unknown" ) ) );
    manifest.m_determinism = *determinism;
    manifest.m_determinismNote =
        json.value( QStringLiteral( "determinism_note" ) ).toString();
    if ( manifest.m_determinism != DeterminismGrade::Strict &&
         manifest.m_determinismNote.isEmpty() )
    {
        // Non-strict randomness must say why (goal §30).
        return Result::failure(
            splitError( QStringLiteral( "non-strict determinism requires a note" ) ) );
    }

    for ( const QJsonValue &value : json.value( QStringLiteral( "assignments" ) ).toArray() )
    {
        const QJsonObject item = value.toObject();
        SplitAssignment assignment;
        assignment.sampleId = item.value( QStringLiteral( "sample_id" ) ).toString();
        const auto role = splitRoleFromString( item.value( QStringLiteral( "role" ) ).toString() );
        if ( assignment.sampleId.isEmpty() || !role )
            return Result::failure( splitError( QStringLiteral( "assignment malformed" ) ) );
        assignment.role = *role;
        assignment.fold = item.value( QStringLiteral( "fold" ) ).toInt( -1 );
        manifest.m_assignments.append( assignment );
    }
    if ( manifest.m_assignments.isEmpty() )
        return Result::failure( splitError( QStringLiteral( "manifest without assignments" ) ) );

    manifest.m_leakageSummary =
        json.value( QStringLiteral( "leakage_summary" ) ).toObject();
    manifest.m_createdAtUtc = QDateTime::fromString(
        json.value( QStringLiteral( "created_at_utc" ) ).toString(), Qt::ISODateWithMs );
    manifest.m_note = json.value( QStringLiteral( "note" ) ).toString();
    manifest.m_fingerprint = json.value( QStringLiteral( "fingerprint" ) ).toString();
    return Result::success( manifest );
}

QString splitManifestFingerprint( const SplitManifest &manifest )
{
    // Content identity only: the logical id and the creation stamp are
    // deliberately excluded, so two runs over the same (config, seed,
    // inputs) share one fingerprint while keeping distinct identities.
    SplitManifest copy = manifest;
    copy.setFingerprint( QString() );
    copy.setManifestId( QString() );
    copy.setCreatedAtUtc( QDateTime() );
    return makeDatasetFingerprint( copy.toJson() ).toHex();
}

bool SplitEngine::methodUsesFolds( SplitMethod method )
{
    switch ( method )
    {
        case SplitMethod::KFold:
        case SplitMethod::SpatialKFold:
        case SplitMethod::GroupKFold:
        case SplitMethod::LeaveOneRegionOut:
        case SplitMethod::LeaveOneSceneOut:
        case SplitMethod::LeaveOneYearOut:
            return true;
        default:
            return false;
    }
}

sicnu::data::Result<SplitManifest> SplitEngine::generate( const SplitConfig &config,
                                                          const QString &datasetVersionId,
                                                          const QVector<SplitInput> &inputs )
{
    using Result = sicnu::data::Result<SplitManifest>;
    const auto validated = config.validate();
    if ( !validated )
        return Result::failure( validated.diagnostics() );
    if ( inputs.isEmpty() )
        return Result::failure( splitError( QStringLiteral( "cannot split zero samples" ) ) );

    QSet<QString> seen;
    for ( const SplitInput &input : inputs )
    {
        if ( input.sampleId.isEmpty() )
            return Result::failure( splitError( QStringLiteral( "input without sample id" ) ) );
        if ( seen.contains( input.sampleId ) )
            return Result::failure(
                splitError( QStringLiteral( "duplicate input sample id %1" ).arg( input.sampleId ) ) );
        seen.insert( input.sampleId );
    }

    SplitManifest manifest;
    manifest.setManifestId( SplitManifestId::generate().toString() );
    manifest.setDatasetVersionId( datasetVersionId );
    manifest.setConfig( config );
    manifest.setDeterminism( DeterminismGrade::Strict );
    manifest.setCreatedAtUtc( QDateTime::currentDateTimeUtc() );

    try
    {
        return SplitEngine::methodUsesFolds( config.method )
                   ? generateFolds( manifest, inputs )
                   : generatePlain( manifest, inputs );
    }
    catch ( const SplitRoleDegenerate & )
    {
        return Result::failure( splitError(
            QStringLiteral( "grouped/temporal split left a non-zero-ratio role empty"
                            " (a single group covers all samples); split refused" ) ) );
    }
}

sicnu::data::Result<SplitManifest> SplitEngine::generatePlain( SplitManifest manifest,
                                                               const QVector<SplitInput> &inputs )
{
    using Result = sicnu::data::Result<SplitManifest>;
    const SplitConfig config = manifest.config();
    const quint64 seed = DeterministicRandom::seedFor( config.seed, QStringLiteral( "split" ) );
    DeterministicRandom random( seed );
    QVector<SplitAssignment> assignments;

    switch ( config.method )
    {
        case SplitMethod::Random:
        {
            QVector<int> indices( inputs.size() );
            for ( int i = 0; i < inputs.size(); ++i )
                indices[i] = i;
            random.shuffle( indices );
            assignByRatio( indices, config.trainRatio, config.validationRatio,
                           config.testRatio, assignments, inputs );
            break;
        }
        case SplitMethod::Stratified:
        {
            // Group row indices by class; shuffle within class; concatenate
            // class blocks in the SHUFFLED CLASS-NAME ORDER so class listing
            // order never leaks into the result.
            QMap<QString, QVector<int>> byClass;
            for ( int i = 0; i < inputs.size(); ++i )
                byClass[inputs.at( i ).classCode].append( i );
            QStringList classNames = byClass.keys();
            random.shuffle( classNames );
            for ( const QString &className : classNames )
            {
                QVector<int> indices = byClass.value( className );
                random.shuffle( indices );
                assignByRatio( indices, config.trainRatio, config.validationRatio,
                               config.testRatio, assignments, inputs );
            }
            break;
        }
        case SplitMethod::Grouped:
        {
            const auto table = keyTable( inputs, []( const SplitInput &input ) {
                return input.groupId;
            } );
            QStringList groupNames = table.keys();
            random.shuffle( groupNames );
            assignments = walkGroupsInOrder( groupNames, table, inputs, config );
            requireNonEmptyRoles( assignments, config, inputs.size() );
            break;
        }
        case SplitMethod::SpatialBlock:
        {
            // Block id = integer grid cell of the bounds center. Blocks are
            // ATOMIC units: the shuffled block list is walked with the same
            // whole-group budget walk as the grouped engine, so every sample
            // inside a block carries one role (#775 — splitting per block
            // put train and test samples on the same spatial block, the
            // exact autocorrelation leakage this method exists to prevent).
            QMap<QString, QVector<int>> byBlock;
            for ( int i = 0; i < inputs.size(); ++i )
            {
                const SplitInput &input = inputs.at( i );
                if ( !input.validBounds )
                    return Result::failure( splitError(
                        QStringLiteral( "spatial_block requires valid bounds on every sample" ) ) );
                const double centerX = ( input.minX + input.maxX ) / 2.0;
                const double centerY = ( input.minY + input.maxY ) / 2.0;
                // Floor on the signed division keeps the grid injective
                // across the axes (truncation would merge cells straddling
                // 0); negative block ids are fine — the key below encodes
                // them losslessly.
                const qint64 blockX = qint64( std::floor( centerX / config.blockSizeX ) );
                const qint64 blockY = qint64( std::floor( centerY / config.blockSizeY ) );
                byBlock[QStringLiteral( "%1|%2" ).arg( blockX ).arg( blockY )].append( i );
            }
            QStringList blockKeys = byBlock.keys();
            random.shuffle( blockKeys );
            assignments = walkGroupsInOrder( blockKeys, byBlock, inputs, config );
            requireNonEmptyRoles( assignments, config, inputs.size() );
            break;
        }
        case SplitMethod::SpatialBuffer:
        {
            // Greedy test selection in shuffled order with an exclusion
            // radius around every accepted test sample. Three outcomes per
            // sample: accepted → Test; within the buffer of a test pick →
            // Unassigned (may not train against a near-duplicate test
            // sample); everything else → Train/Validation by ratio.
            QVector<int> order( inputs.size() );
            for ( int i = 0; i < inputs.size(); ++i )
                order[i] = i;
            random.shuffle( order );
            QSet<int> accepted;
            QSet<int> excluded;
            const int targetTest = int( std::floor( config.testRatio * inputs.size() ) );
            for ( const int candidate : order )
            {
                const SplitInput &input = inputs.at( candidate );
                if ( !input.validBounds )
                    continue;
                const double centerX = ( input.minX + input.maxX ) / 2.0;
                const double centerY = ( input.minY + input.maxY ) / 2.0;
                bool tooClose = false;
                for ( const int testIndex : qAsConst( accepted ) )
                {
                    const SplitInput &test = inputs.at( testIndex );
                    const double dx = centerX - ( test.minX + test.maxX ) / 2.0;
                    const double dy = centerY - ( test.minY + test.maxY ) / 2.0;
                    if ( std::sqrt( dx * dx + dy * dy ) < config.bufferDistance )
                    {
                        tooClose = true;
                        break;
                    }
                }
                if ( !tooClose && accepted.size() < targetTest )
                    accepted.insert( candidate );
                else if ( tooClose )
                    excluded.insert( candidate );
            }
            // Remainder (accepted AND buffer-vetoed removed — #786: counting
            // vetoed samples inflated the train count and starved
            // Validation down to zero) splits Train/Validation by the
            // train:validation share of the config ratios, with the
            // largest-remainder tie going to Train.
            QVector<int> remaining;
            for ( const int candidate : order )
            {
                if ( accepted.contains( candidate ) || excluded.contains( candidate ) )
                    continue;
                remaining.append( candidate );
            }
            const double ratioSum = config.trainRatio + config.validationRatio;
            const double trainShare = ratioSum > 0.0 ? config.trainRatio / ratioSum : 1.0;
            const double trainExact = trainShare * remaining.size();
            int trainCount = int( std::floor( trainExact ) );
            // Two-role largest remainder: the fractional seat goes to Train
            // on an exact tie (deterministic, favors the role a model needs
            // more when the remainder cannot be split).
            if ( trainExact - std::floor( trainExact ) >= 0.5 - 1e-12 )
                ++trainCount;
            trainCount = std::min( trainCount, int( remaining.size() ) );
            int position = 0;
            for ( const int candidate : order )
            {
                SplitAssignment assignment;
                assignment.sampleId = inputs.at( candidate ).sampleId;
                if ( accepted.contains( candidate ) )
                {
                    assignment.role = SplitRole::Test;
                }
                else if ( excluded.contains( candidate ) )
                {
                    assignment.role = SplitRole::Unassigned;
                }
                else
                {
                    assignment.role =
                        position < trainCount ? SplitRole::Train : SplitRole::Validation;
                    ++position;
                }
                assignments.append( assignment );
            }
            break;
        }
        case SplitMethod::Temporal:
        {
            // Groups ordered by earliest member time; whole groups move
            // together (older data trains, newer data tests).
            const auto table = keyTable( inputs, []( const SplitInput &input ) {
                return input.groupId;
            } );
            QStringList groupNames = table.keys();
            std::sort( groupNames.begin(), groupNames.end(),
                       [&table, &inputs]( const QString &a, const QString &b ) {
                           qint64 timeA = std::numeric_limits<qint64>::max();
                           for ( const int index : table.value( a ) )
                               timeA = qMin( timeA, inputs.at( index ).timeMs );
                           qint64 timeB = std::numeric_limits<qint64>::max();
                           for ( const int index : table.value( b ) )
                               timeB = qMin( timeB, inputs.at( index ).timeMs );
                           if ( timeA != timeB )
                               return timeA < timeB;
                           return a < b;
                       } );
            assignments = walkGroupsInOrder( groupNames, table, inputs, config );
            requireNonEmptyRoles( assignments, config, inputs.size() );
            break;
        }
        case SplitMethod::KFold:
        case SplitMethod::SpatialKFold:
        case SplitMethod::GroupKFold:
        case SplitMethod::LeaveOneRegionOut:
        case SplitMethod::LeaveOneSceneOut:
        case SplitMethod::LeaveOneYearOut:
            return Result::failure( splitError( QStringLiteral( "method requires fold engine" ) ) );
    }

    manifest.assignments() = assignments;
    manifest.setFingerprint( splitManifestFingerprint( manifest ) );
    return Result::success( manifest );
}

sicnu::data::Result<SplitManifest> SplitEngine::generateFolds( SplitManifest manifest,
                                                               const QVector<SplitInput> &inputs )
{
    using Result = sicnu::data::Result<SplitManifest>;
    const SplitConfig config = manifest.config();
    const quint64 seed = DeterministicRandom::seedFor( config.seed, QStringLiteral( "split" ) );
    DeterministicRandom random( seed );
    QVector<SplitAssignment> assignments;

    switch ( config.method )
    {
        case SplitMethod::KFold:
        {
            QVector<int> indices( inputs.size() );
            for ( int i = 0; i < inputs.size(); ++i )
                indices[i] = i;
            random.shuffle( indices );
            for ( int position = 0; position < indices.size(); ++position )
            {
                SplitAssignment assignment;
                assignment.sampleId = inputs.at( indices.at( position ) ).sampleId;
                assignment.fold = position % int( config.foldCount );
                assignments.append( assignment );
            }
            break;
        }
        case SplitMethod::GroupKFold:
        {
            // Groups shuffled once; round-robin over folds keeps group sizes
            // approximately balanced without any group crossing folds.
            QMap<QString, QVector<int>> byGroup;
            for ( int i = 0; i < inputs.size(); ++i )
                byGroup[inputs.at( i ).groupId].append( i );
            QStringList groupNames = byGroup.keys();
            random.shuffle( groupNames );
            int foldCursor = 0;
            for ( const QString &group : groupNames )
            {
                const int fold = foldCursor % int( config.foldCount );
                foldCursor++;
                for ( const int index : byGroup.value( group ) )
                {
                    SplitAssignment assignment;
                    assignment.sampleId = inputs.at( index ).sampleId;
                    assignment.fold = fold;
                    assignments.append( assignment );
                }
            }
            break;
        }
        case SplitMethod::SpatialKFold:
        {
            // Blocks sorted spatially, then shuffled, then folds assigned.
            QMap<QPair<qint64, qint64>, QVector<int>> byBlock;
            for ( int i = 0; i < inputs.size(); ++i )
            {
                const SplitInput &input = inputs.at( i );
                if ( !input.validBounds )
                    return Result::failure( splitError(
                        QStringLiteral( "spatial_k_fold requires valid bounds on every sample" ) ) );
                const double centerX = ( input.minX + input.maxX ) / 2.0;
                const double centerY = ( input.minY + input.maxY ) / 2.0;
                const qint64 blockX = qint64( std::floor( centerX / config.blockSizeX ) );
                const qint64 blockY = qint64( std::floor( centerY / config.blockSizeY ) );
                byBlock[qMakePair( blockX, blockY )].append( i );
            }
            QList<QPair<qint64, qint64>> blocks = byBlock.keys();
            std::sort( blocks.begin(), blocks.end() );
            random.shuffle( blocks );
            int foldCursor = 0;
            for ( const auto &block : blocks )
            {
                const int fold = foldCursor % int( config.foldCount );
                foldCursor++;
                for ( const int index : byBlock.value( block ) )
                {
                    SplitAssignment assignment;
                    assignment.sampleId = inputs.at( index ).sampleId;
                    assignment.fold = fold;
                    assignments.append( assignment );
                }
            }
            break;
        }
        case SplitMethod::LeaveOneRegionOut:
        case SplitMethod::LeaveOneSceneOut:
        case SplitMethod::LeaveOneYearOut:
        {
            // One fold per distinct key, keys sorted for determinism.
            QMap<QString, QVector<int>> byKey;
            for ( int i = 0; i < inputs.size(); ++i )
            {
                QString key = groupKeyOf( inputs.at( i ), config.method );
                if ( config.method == SplitMethod::LeaveOneYearOut && inputs.at( i ).year <= 0 )
                    return Result::failure( splitError(
                        QStringLiteral( "leave_one_year_out requires a year on every sample" ) ) );
                if ( key.isEmpty() )
                    return Result::failure( splitError(
                        QStringLiteral( "leave-one-out requires the grouping key on every sample" ) ) );
                byKey[key].append( i );
            }
            const QList<QString> keys = byKey.keys();
            int fold = 0;
            for ( const QString &key : keys )
            {
                for ( const int index : byKey.value( key ) )
                {
                    SplitAssignment assignment;
                    assignment.sampleId = inputs.at( index ).sampleId;
                    assignment.fold = fold;
                    assignments.append( assignment );
                }
                ++fold;
            }
            break;
        }
        default:
            return Result::failure( splitError( QStringLiteral( "method does not use folds" ) ) );
    }

    manifest.assignments() = assignments;
    manifest.setFingerprint( splitManifestFingerprint( manifest ) );
    return Result::success( manifest );
}

} // namespace sicnu::dataset
