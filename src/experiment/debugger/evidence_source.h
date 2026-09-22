// evidence_source.h — read-only providers of recorded run evidence
// (RS14-06, ADR 0174).
//
// The debugger never records, never writes, and never executes: everything
// it knows comes from an IRunEvidenceSource. Production wires
// DirectoryEvidenceSource (ExperimentStore reads + workflow checkpoint /
// provenance files); tests wire InMemoryEvidenceSource. Absent evidence is
// reported typed (`experiment.debugger.evidence_absent`), never faked.
#pragma once

#include "../../data/data_result.h"
#include "../experiment_store.h"
#include "../experiment_types.h"
#include "debugger_types.h"
#include "run_snapshot.h"

#include <QHash>
#include <QJsonObject>
#include <QString>

#include <optional>

namespace sicnu::experiment::debugger
{

/// Per-step evidence for one run, in the source's recorded order. Order is
/// meaningful: producers precede consumers in every mode we read.
struct StepEvidence
{
    StepEvidenceMode mode = StepEvidenceMode::Absent;
    QVector<StepSnapshot> steps;
    /// Artifact records the source carries directly (provenance graphs
    /// contribute root inputs and every produced artifact with fingerprints).
    QVector<ArtifactSnapshot> artifacts;
    /// Plan signature when the source recorded one (provenance run node).
    QString planSignature;
};

/// Normalizers shared by every source (strict; typed failures). These are
/// the ONLY places recorded wire shapes are interpreted — sources never
/// re-implement them.
///   - provenance: strict envelope gate (delegates to ProvenanceGraph), then
///     nodeExec/artifact normalization with topological step order
///     (deterministic id tiebreak) and root-input detection.
///   - bridge: the run.metrics()["workflow"] contract document (steps array
///     of {id, operator, status, output{digest,size,path}} summaries).
sicnu::data::Result<StepEvidence> stepEvidenceFromProvenanceDoc( const QJsonObject &doc );
sicnu::data::Result<StepEvidence> stepEvidenceFromBridgeWorkflowMetrics( const QJsonObject &workflow );

/// The one seam between the debugger and recorded evidence.
class IRunEvidenceSource
{
  public:
    virtual ~IRunEvidenceSource() = default;

    /// Run-level record. Failure carries kCodeUnknownRun when the id is
    /// unknown to this source.
    virtual sicnu::data::Result<ExperimentRun> run( const QString &runId ) = 0;

    /// Per-step evidence. Failure carries kCodeEvidenceAbsent when the
    /// source holds no step-level record for this run (a run may legitimately
    /// have none — single-operator runs recorded without a bridge).
    virtual sicnu::data::Result<StepEvidence> steps( const QString &runId ) = 0;
};

/// Test / embedding fake: explicit maps, no I/O. Raw recorded documents can
/// be inserted alongside ready-made evidence; raw documents go through the
/// SAME production normalizers so tests exercise the real interpretation
/// path.
class InMemoryEvidenceSource : public IRunEvidenceSource
{
  public:
    void insertRun( const ExperimentRun &run );
    void insertRunJson( const QString &runId, const QJsonObject &runJson );
    void insertStepEvidence( const QString &runId, const StepEvidence &evidence );
    /// Stores a raw d17_provenance document; steps() normalizes it through
    /// stepEvidenceFromProvenanceDoc on every call.
    void insertProvenanceDoc( const QString &runId, const QJsonObject &doc );

    sicnu::data::Result<ExperimentRun> run( const QString &runId ) override;
    sicnu::data::Result<StepEvidence> steps( const QString &runId ) override;

  private:
    QHash<QString, QJsonObject> m_runs;          // stored as JSON so round-trips match production
    QHash<QString, StepEvidence> m_steps;
    QHash<QString, QJsonObject> m_provenanceDocs;
};

/// Production source: run records from the ExperimentStore (read APIs only),
/// step evidence from a workflow run directory. Richest available mode wins:
///   1. checkpoint_<runId>.json        → CheckpointSteps (strict loader reuse)
///   2. provenance_<runId>.json        → ProvenanceGraph (strict parser reuse)
///      (attempt-<N>/ subdirectories are considered, highest attempt first)
///   3. run.metrics()["workflow"]      → StepsEvidence (bridge contract)
///   4. none                           → typed kCodeEvidenceAbsent
/// The store is borrowed and used read-only; the directory is scanned
/// bounded (root + attempt subdirectories, no recursion beyond that).
class DirectoryEvidenceSource : public IRunEvidenceSource
{
  public:
    /// @p store may be nullptr: the source then answers run() with
    /// kCodeUnknownRun (evidence-directory-only deployments, tests).
    DirectoryEvidenceSource( ExperimentStore *store, const QString &runDirectory );

    sicnu::data::Result<ExperimentRun> run( const QString &runId ) override;
    sicnu::data::Result<StepEvidence> steps( const QString &runId ) override;

  private:
    ExperimentStore *m_store = nullptr;
    QString m_runDirectory;
};

} // namespace sicnu::experiment::debugger
