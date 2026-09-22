// capsule_readiness.h — replay readiness of a ReproducibilityCapsule
// against THIS machine: "what is missing HERE to replay this experiment?"
//
// Works from the document + injected hooks only (offline; no original
// store). Level rollup mirrors ReplayReadiness exactly:
//   any missing/mismatched REQUIRED pin → Impossible
//   otherwise any unknown dependency    → BestEffort
//   otherwise                           → Exact
// Unwired hooks answer Unknown — never a fake Exact.
#pragma once

#include "capsule_builder.h"
#include "experiment/replay_readiness.h"

namespace sicnu::dataset
{
class DatasetStore;
}

namespace sicnu::experiment::capsule
{

/// Additional availability probes for the consuming machine. Unwired ⇒
/// Unknown, never a fake Ok.
struct CapsuleReadinessHooks
{
    /// Software revision of the CONSUMING build ("" = unwired).
    std::function<QString()> currentSoftwareRevision;
    /// model id/digest still resolvable here?
    std::function<bool( const QString &modelId, const QString &modelDigest )> modelAvailable;
    /// Output still available under this portable ref (the hook resolves
    /// workspace:/external: against local roots), with matching digest/size?
    std::function<bool( const QString &portableRef, const QString &digest, qint64 sizeBytes )>
        outputAvailable;
};

class CapsuleReadiness
{
  public:
    /// Assesses @p doc against the local machine. @p datasetStore may be
    /// null (dataset/split checks become Unknown). @p hooks supplies
    /// capability/plan pins the experiment layer cannot consult itself.
    static ReplayReadinessReport assess( const CapsuleDocument &doc,
                                         const sicnu::dataset::DatasetStore *datasetStore,
                                         const CapsuleHooks &hooks,
                                         const CapsuleReadinessHooks &readinessHooks = {} );
};

} // namespace sicnu::experiment::capsule
