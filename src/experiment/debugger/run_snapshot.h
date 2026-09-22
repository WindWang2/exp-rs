// run_snapshot.h — normalized, comparable snapshot of one recorded run
// (RS14-06, ADR 0174).
//
// A RunSnapshot is a pure value projection of evidence that ALREADY exists:
//   - the ExperimentStore run record (identity pins, metrics), read-only;
//   - per-step execution evidence, in decreasing richness:
//       CheckpointSteps   — workflow checkpoint step plans (resolved params,
//                           lineage fingerprint, output digest, dependencies);
//       ProvenanceGraph   — provenance_<runId>.json nodeExec/artifact graph
//                           (lineage signature + artifact fingerprints, no
//                           per-step parameters);
//       StepsEvidence     — the bridge contract's run.metrics()["workflow"]
//                           ["steps"] summaries (id/operator/status/output);
//       Absent            — no step evidence was recorded.
// The snapshot deliberately does NOT recompute digests: hashes are read as
// recorded, tagged with their mode, and comparisons upstream refuse to mix
// modes. Timing fields, paths and error texts are recorded but excluded from
// the canonical digest — two snapshots of the same execution carry the same
// digest regardless of wall-clock noise.
#pragma once

#include "../experiment_types.h"
#include "../../data/data_result.h"
#include "debugger_types.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace sicnu::experiment::debugger
{

/// Where a snapshot's step records came from (richest available wins).
enum class StepEvidenceMode
{
    Absent,
    CheckpointSteps,
    ProvenanceGraph,
    StepsEvidence,
};

QString stepEvidenceModeName( StepEvidenceMode mode );
/// Strict parse; fails typed on unknown names (no silent fallback).
sicnu::data::Result<StepEvidenceMode> stepEvidenceModeFromName( const QString &name );

/// A recorded digest value plus its provenance mode tag. The digest is
/// kept VERBATIM as recorded (workflow fingerprint prefixes included);
/// classification never rewrites evidence.
struct DigestRecord
{
    QString digest;
    QString mode;
};

/// Classifies a digest value exactly as recorded into its mode
/// ("sha256fl:"/"sha256full:" prefixes, bare 64-hex, or unknown). Pure
/// tagging — no digest is ever computed here.
DigestRecord classifyRecordedDigest( const QString &recorded );

struct StepSnapshot
{
    QString stepId;
    QString operatorId;
    /// Resolved parameters when the source carries them (checkpoint mode).
    /// Empty object otherwise — absence is honest, never padded.
    QJsonObject parameters;
    /// SHA-256 over canonicalizeJsonRfc8785(parameters); empty when the
    /// source recorded no parameters.
    QString paramsHash;
    /// Process-identity digest as recorded (checkpoint `fingerprint` /
    /// provenance `lineageSignature`); empty when the source lacks it.
    QString lineageSignature;
    /// Recorded terminal state string (verbatim; not interpreted here).
    QString status;
    bool cacheHit = false;
    bool cacheHitKnown = false;
    /// Upstream step ids (sorted, deduplicated) when the source declares
    /// them; empty does not distinguish "no parents" from "unknown" —
    /// the mode field answers that upstream.
    QStringList dependencies;
    /// Produced-output digest exactly as recorded (mode tag below).
    QString outputDigest;
    QString digestMode;
    qint64 outputSizeBytes = -1;
    QString errorMessage;

    QJsonObject toJson() const;
    static sicnu::data::Result<StepSnapshot> fromJson( const QJsonObject &json );
    bool operator==( const StepSnapshot & ) const = default;
};

struct ArtifactSnapshot
{
    QString artifactId;
    QString digest;
    QString digestMode;
    qint64 sizeBytes = -1;
    /// True when no step in this run produced it (external input state).
    bool rootInput = false;
    QString producerStepId;

    QJsonObject toJson() const;
    static sicnu::data::Result<ArtifactSnapshot> fromJson( const QJsonObject &json );
    bool operator==( const ArtifactSnapshot & ) const = default;
};

/// Run-level identity pins projected from the ExperimentRun record. Field
/// names follow experiment_types.h; empty means "not recorded".
struct RunPinsSnapshot
{
    QString algorithmId;
    QString algorithmVersion;
    QString datasetVersionId;
    QString datasetFingerprint;
    QString splitManifestId;
    QString splitFingerprint;
    QString modelId;
    QString modelDigest;
    QString softwareRevision;
    QString configHash;
    QString resultFingerprint;
    QString runStatus;
    quint64 seed = 0;
    bool seedKnown = false;

    QJsonObject toJson() const;
    static sicnu::data::Result<RunPinsSnapshot> fromJson( const QJsonObject &json );
    bool operator==( const RunPinsSnapshot & ) const = default;
};

class RunSnapshot
{
  public:
    RunSnapshot() = default;

    const QString &runId() const { return m_runId; }
    void setRunId( const QString &id ) { m_runId = id; }

    StepEvidenceMode stepEvidence() const { return m_stepEvidence; }
    void setStepEvidence( StepEvidenceMode mode ) { m_stepEvidence = mode; }

    const QString &planSignature() const { return m_planSignature; }
    void setPlanSignature( const QString &signature ) { m_planSignature = signature; }

    const QVector<StepSnapshot> &steps() const { return m_steps; }
    void setSteps( QVector<StepSnapshot> steps ) { m_steps = std::move( steps ); }

    const QVector<ArtifactSnapshot> &artifacts() const { return m_artifacts; }
    void setArtifacts( QVector<ArtifactSnapshot> artifacts ) { m_artifacts = std::move( artifacts ); }

    const RunPinsSnapshot &pins() const { return m_pins; }
    void setPins( RunPinsSnapshot pins ) { m_pins = std::move( pins ); }

    /// Verbatim metrics document from the run record (already redacted at
    /// record time); carried for downstream metric comparison only.
    const QJsonObject &metrics() const { return m_metrics; }
    void setMetrics( const QJsonObject &metrics ) { m_metrics = metrics; }

    /// Step lookup by id; nullptr when absent.
    const StepSnapshot *findStep( const QString &stepId ) const;

    QJsonObject toJson() const;
    static sicnu::data::Result<RunSnapshot> fromJson( const QJsonObject &json );

    /// Canonical identity document WITHOUT the run id: pins (minus the
    /// volatile run-status label), plan signature, per-step process identity
    /// and output identity, and root-input identity. Public so consumers
    /// compare identity against EXACTLY the fields the digest covers — one
    /// identity truth, never a parallel re-implementation.
    QJsonObject identityDocument() const;

    /// Canonical identity digest: SHA-256 over canonicalizeJsonRfc8785 of
    /// the identity fields only (pins, plan signature, per-step operator /
    /// paramsHash / lineageSignature / dependencies / output digest + mode,
    /// root-input artifact digests). Excludes timing, paths, error text,
    /// metrics content and non-root artifact paths. Stable across key order
    /// and rebuilds of the same evidence.
    QString snapshotDigest() const;

    bool operator==( const RunSnapshot & ) const = default;

  private:
    QString m_runId;
    StepEvidenceMode m_stepEvidence = StepEvidenceMode::Absent;
    QString m_planSignature;
    QVector<StepSnapshot> m_steps;
    QVector<ArtifactSnapshot> m_artifacts;
    RunPinsSnapshot m_pins;
    QJsonObject m_metrics;
};

} // namespace sicnu::experiment::debugger
