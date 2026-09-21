// capsule_builder.h — projects ONE recorded ExperimentRun into a
// ReproducibilityCapsule document.
//
// Projection-not-computation doctrine: every section is copied from recorded
// store truth (ExperimentStore / DatasetStore) or arrives through an
// explicitly injected CapsulesHooks provider (capability descriptors,
// verifier summaries, lineage slices, workflow definition digests). The
// builder never executes, infers science, or repairs records — an
// unresolvable pin is RECORDED as unresolved, never dropped or fabricated.
#pragma once

#include "capsule_document.h"

#include "dataset/dataset_store.h"
#include "experiment/experiment_store.h"

#include <functional>

namespace sicnu::experiment::capsule
{

/// External providers the experiment module cannot consult itself (the
/// capability catalog and verifier live in upper layers). Unwired hooks
/// yield recorded facts only — never a fabricated digest or a fake verdict.
struct CapsuleHooks
{
    /// algorithm id → capability descriptor record in force for this build
    /// (e.g. the installed capability sidecar content). Empty object ⇒ not
    /// wired / not found.
    std::function<QJsonObject( const QString &algorithmId )> capabilityDescriptor;
    /// algorithm/workflow id → definition digest pinning the executed plan
    /// (workflow definition snapshot digest; "" when not wired).
    std::function<QString( const QString &algorithmId )> planDefinitionDigest;
    /// run id → pre-computed verifier summary (OutputVerification-style
    /// JSON). Empty object ⇒ not wired.
    std::function<QJsonObject( const QString &runId )> verifierSummary;
};

struct CapsuleOptions
{
    /// Fixed creation instant (ISO-8601 UTC) for deterministic rebuilds.
    /// Empty ⇒ wall clock at build time. Two builds of the same store
    /// content with the same createdUtc produce byte-identical capsules.
    QString createdUtc;
    /// Absolute root of the PRODUCING workspace (portability policy).
    /// Recorded artifact paths inside it are rewritten to
    /// "workspace:<relative>" references; anything outside becomes
    /// "external:<file-name>". The capsule never carries absolute paths.
    QString workspaceRoot;
};

class CapsuleBuilder
{
  public:
    /// Both stores must outlive the builder (bare pointers are held).
    CapsuleBuilder( const ExperimentStore &experimentStore,
                    const dataset::DatasetStore &datasetStore );

    /// Projects @p runId into a finalized capsule document. Fails with a
    /// typed diagnostic when the run does not exist (capsule.run-missing).
    /// Warnings (e.g. unresolvable dataset version) ride the Result's
    /// diagnostics while the document still records the fact.
    Result<CapsuleDocument> build( const QString &runId,
                                   const CapsuleOptions &options = {},
                                   const CapsuleHooks &hooks = {} ) const;

  private:
    const ExperimentStore *m_experimentStore = nullptr;
    const dataset::DatasetStore *m_datasetStore = nullptr;
};

} // namespace sicnu::experiment::capsule
