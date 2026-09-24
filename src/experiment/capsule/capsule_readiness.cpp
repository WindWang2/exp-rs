// capsule_readiness.cpp — see capsule_readiness.h.
#include "capsule_readiness.h"

#include "dataset/dataset_store.h"

#include <QJsonArray>

namespace sicnu::experiment::capsule
{

using sicnu::dataset::DatasetStore;
using sicnu::dataset::ReproductionLevel;
using ReplayStatus = sicnu::experiment::ReplayCheckStatus;

namespace
{

QJsonObject findInput( const CapsuleDocument &doc, const QString &kind )
{
    const QJsonArray inputs = doc.root().value( QStringLiteral( "inputs" ) ).toArray();
    for ( const auto &item : inputs )
    {
        const QJsonObject pin = item.toObject();
        if ( pin.value( QStringLiteral( "kind" ) ).toString() == kind )
            return pin;
    }
    return {};
}

void rollup( ReplayReadinessReport &report )
{
    // The report struct defaults to Impossible (fail-conservative); roll the
    // observed checks DOWN from Exact instead.
    report.level = ReproductionLevel::Exact;
    bool anyUnknown = false;
    for ( const auto &check : report.checks )
    {
        if ( check.status == ReplayStatus::Missing || check.status == ReplayStatus::Mismatched )
        {
            report.level = ReproductionLevel::Impossible;
            return;
        }
        if ( check.status == ReplayStatus::Unknown )
            anyUnknown = true;
    }
    report.level = anyUnknown ? ReproductionLevel::BestEffort : ReproductionLevel::Exact;
}

} // namespace

ReplayReadinessReport CapsuleReadiness::assess( const CapsuleDocument &doc,
                                                const DatasetStore *datasetStore,
                                                const CapsuleHooks &hooks,
                                                const CapsuleReadinessHooks &readinessHooks )
{
    ReplayReadinessReport report;

    // Dataset version pin: required. Unresolvable or fingerprint-drifted
    // pins block replay outright.
    {
        sicnu::experiment::ReplayCheck check;
        check.dependency = QStringLiteral( "dataset_version" );
        const QJsonObject pin = findInput( doc, QStringLiteral( "dataset_version" ) );
        if ( pin.isEmpty() )
        {
            check.status = ReplayStatus::Missing;
            check.detail = QStringLiteral( "capsule pins no dataset version" );
        }
        else
        {
            const QString id = pin.value( QStringLiteral( "id" ) ).toString();
            const QString recordedFingerprint = pin.value( QStringLiteral( "digest" ) ).toString();
            const auto versionId = sicnu::dataset::DatasetVersionId::fromString( id );
            const auto record =
                datasetStore && versionId ? datasetStore->versionById( versionId.value() )
                                          : std::optional<sicnu::dataset::DatasetVersionRecord>{};
            if ( !record )
            {
                check.status = datasetStore ? ReplayStatus::Missing : ReplayStatus::Unknown;
                check.detail = datasetStore ? QStringLiteral( "version %1 is not in this store" ).arg( id )
                                            : QStringLiteral( "no dataset store wired" );
            }
            else if ( recordedFingerprint.isEmpty() )
            {
                // Same doctrine as the capability/plan checks: an empty pin
                // cannot be compared, and "no evidence" must never roll up
                // as Ok on a machine whose data may have drifted.
                check.status = ReplayStatus::Unknown;
                check.detail = QStringLiteral( "capsule pins no dataset fingerprint" );
            }
            else if ( record->fingerprint() != recordedFingerprint )
            {
                check.status = ReplayStatus::Mismatched;
                check.detail = QStringLiteral( "local fingerprint %1 != pinned %2" )
                                   .arg( record->fingerprint(), recordedFingerprint );
            }
            else
            {
                check.status = ReplayStatus::Ok;
                check.detail = record->fingerprint();
            }
        }
        report.checks.append( check );
    }

    // Split manifest pin: required (same doctrine as the run-level readiness).
    {
        sicnu::experiment::ReplayCheck check;
        check.dependency = QStringLiteral( "split_manifest" );
        const QJsonObject pin = findInput( doc, QStringLiteral( "split" ) );
        if ( pin.isEmpty() )
        {
            check.status = ReplayStatus::Missing;
            check.detail = QStringLiteral( "capsule pins no split manifest" );
        }
        else
        {
            const QString id = pin.value( QStringLiteral( "id" ) ).toString();
            const QString recordedFingerprint = pin.value( QStringLiteral( "digest" ) ).toString();
            const auto manifest =
                datasetStore ? datasetStore->splitManifestById( id )
                             : std::optional<sicnu::dataset::SplitManifest>{};
            if ( !manifest )
            {
                check.status = datasetStore ? ReplayStatus::Missing : ReplayStatus::Unknown;
                check.detail = datasetStore
                                   ? QStringLiteral( "manifest %1 is not in this store" ).arg( id )
                                   : QStringLiteral( "no dataset store wired" );
            }
            else if ( recordedFingerprint.isEmpty() )
            {
                check.status = ReplayStatus::Unknown;
                check.detail = QStringLiteral( "capsule pins no split fingerprint" );
            }
            else if ( manifest->fingerprint() != recordedFingerprint )
            {
                check.status = ReplayStatus::Mismatched;
                check.detail = QStringLiteral( "local fingerprint %1 != pinned %2" )
                                   .arg( manifest->fingerprint(), recordedFingerprint );
            }
            else
            {
                check.status = ReplayStatus::Ok;
                check.detail = manifest->fingerprint();
            }
        }
        report.checks.append( check );
    }

    // Capability pin: the descriptor in force when the capsule was built
    // must still match what this install would use.
    {
        sicnu::experiment::ReplayCheck check;
        const QJsonObject plan = doc.root().value( QStringLiteral( "plan" ) ).toObject();
        const QString algorithmId = plan.value( QStringLiteral( "algorithm_id" ) ).toString();
        check.dependency = QStringLiteral( "capability:%1" ).arg( algorithmId );
        const QJsonArray capabilities =
            doc.root().value( QStringLiteral( "capabilities" ) ).toArray();
        const QString pinnedDigest =
            capabilities.isEmpty()
                ? QString()
                : capabilities.at( 0 ).toObject().value( QStringLiteral( "digest" ) ).toString();
        if ( algorithmId.isEmpty() )
        {
            check.status = ReplayStatus::Missing;
            check.detail = QStringLiteral( "capsule records no algorithm" );
        }
        else if ( !hooks.capabilityDescriptor )
        {
            check.status = ReplayStatus::Unknown;
            check.detail = QStringLiteral( "no capability catalog hook wired" );
        }
        else
        {
            const QJsonObject descriptor = hooks.capabilityDescriptor( algorithmId );
            if ( descriptor.isEmpty() )
            {
                check.status = ReplayStatus::Missing;
                check.detail =
                    QStringLiteral( "no capability descriptor installed for %1" ).arg( algorithmId );
            }
            else if ( pinnedDigest.isEmpty() )
            {
                check.status = ReplayStatus::Unknown;
                check.detail = QStringLiteral( "capsule carries no pinned descriptor digest" );
            }
            else if ( capsuleDigest( descriptor ) != pinnedDigest )
            {
                check.status = ReplayStatus::Mismatched;
                check.detail = QStringLiteral( "installed descriptor digest differs from the pinned one" );
            }
            else
            {
                check.status = ReplayStatus::Ok;
                check.detail = QStringLiteral( "descriptor digest matches" );
            }
        }
        report.checks.append( check );
    }

    // Plan definition digest: verifiable only when pinned AND a hook can
    // resolve the local definition.
    {
        sicnu::experiment::ReplayCheck check;
        const QJsonObject plan = doc.root().value( QStringLiteral( "plan" ) ).toObject();
        const QString algorithmId = plan.value( QStringLiteral( "algorithm_id" ) ).toString();
        const QString pinnedDigest =
            plan.value( QStringLiteral( "definition_digest" ) ).toString();
        check.dependency = QStringLiteral( "plan_definition" );
        if ( pinnedDigest.isEmpty() )
        {
            check.status = ReplayStatus::Unknown;
            check.detail = QStringLiteral( "capsule does not pin a plan definition digest" );
        }
        else if ( !hooks.planDefinitionDigest )
        {
            check.status = ReplayStatus::Unknown;
            check.detail = QStringLiteral( "no plan definition hook wired" );
        }
        else
        {
            const QString localDigest = hooks.planDefinitionDigest( algorithmId );
            if ( localDigest == pinnedDigest )
            {
                check.status = ReplayStatus::Ok;
                check.detail = QStringLiteral( "definition digest matches" );
            }
            else
            {
                check.status = ReplayStatus::Mismatched;
                check.detail = QStringLiteral( "local definition digest %1 != pinned %2" )
                                   .arg( localDigest, pinnedDigest );
            }
        }
        report.checks.append( check );
    }

    // Software revision: a required execution pin (ADR 0137) — drift is a
    // Mismatched, honestly reported.
    {
        sicnu::experiment::ReplayCheck check;
        check.dependency = QStringLiteral( "software_revision" );
        const QString pinned = doc.root().value( QStringLiteral( "software" ) ).toObject()
                                   .value( QStringLiteral( "revision" ) ).toString();
        if ( !readinessHooks.currentSoftwareRevision )
        {
            check.status = ReplayStatus::Unknown;
            check.detail = QStringLiteral( "no local software revision hook wired" );
        }
        else
        {
            const QString local = readinessHooks.currentSoftwareRevision();
            if ( local == pinned )
            {
                check.status = ReplayStatus::Ok;
                check.detail = pinned;
            }
            else
            {
                check.status = ReplayStatus::Mismatched;
                check.detail =
                    QStringLiteral( "local revision %1 != pinned %2" ).arg( local, pinned );
            }
        }
        report.checks.append( check );
    }

    // Model pin: checked only when the capsule records one.
    {
        const QJsonObject pin = findInput( doc, QStringLiteral( "model" ) );
        if ( !pin.isEmpty() )
        {
            sicnu::experiment::ReplayCheck check;
            check.dependency = QStringLiteral( "model" );
            if ( !readinessHooks.modelAvailable )
            {
                check.status = ReplayStatus::Unknown;
                check.detail = QStringLiteral( "no model catalog hook wired" );
            }
            else
            {
                const bool available = readinessHooks.modelAvailable(
                    pin.value( QStringLiteral( "id" ) ).toString(),
                    pin.value( QStringLiteral( "digest" ) ).toString() );
                check.status = available ? ReplayStatus::Ok : ReplayStatus::Missing;
                check.detail = available ? QStringLiteral( "resolvable with matching digest" )
                                         : QStringLiteral( "not resolvable or digest changed" );
            }
            report.checks.append( check );
        }
    }

    // Outputs: availability through the consuming machine's resolver.
    {
        const QJsonArray outputs = doc.root().value( QStringLiteral( "outputs" ) ).toArray();
        for ( const auto &item : outputs )
        {
            const QJsonObject output = item.toObject();
            const QString ref = output.value( QStringLiteral( "portable_ref" ) ).toString();
            sicnu::experiment::ReplayCheck check;
            check.dependency = QStringLiteral( "output:%1" ).arg( ref );
            if ( !readinessHooks.outputAvailable )
            {
                check.status = ReplayStatus::Unknown;
                check.detail = QStringLiteral( "no output availability hook wired" );
            }
            else
            {
                // Artifact sizes are qint64 end-to-end (experiment_types.h);
                // toInt() would turn > 2^31 outputs into the default 0 and
                // let a size-checking hook pass or fail on a fiction (the
                // round-1 evaluation.cpp defect class).
                const bool available = readinessHooks.outputAvailable(
                    ref, output.value( QStringLiteral( "digest" ) ).toString(),
                    output.value( QStringLiteral( "size_bytes" ) ).toInteger() );
                check.status = available ? ReplayStatus::Ok : ReplayStatus::Missing;
                check.detail = available ? QStringLiteral( "resolvable with matching content" )
                                         : QStringLiteral( "output not available here" );
            }
            report.checks.append( check );
        }
    }

    rollup( report );
    return report;
}

} // namespace sicnu::experiment::capsule
