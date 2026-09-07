// experiment_types.h — Experiment/Run identity + canonical config +
// comparison (goal §22–§27, ADR 0137).
//
// ExperimentRun RECORDS an execution performed through the existing
// TaskCenter/JobEngine/workflow seams — it never executes anything itself
// (no second scheduler). The three hashes have three distinct meanings and
// are tested never to be conflated:
//   run_config_hash      — canonical parameters only (key-order free)
//   execution_fingerprint— + algorithm/workflow identity, dataset version,
//                          split, model digest, seed (what was executed)
//   result_fingerprint   — output digests + metrics (what came out)
#pragma once

#include "../dataset/dataset_types.h"
#include "../data/data_result.h"
#include "experiment_ids.h"

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace sicnu::experiment
{

using sicnu::dataset::Diagnostic;
using sicnu::dataset::DiagnosticSeverity;
using sicnu::dataset::DeterminismGrade;
using sicnu::dataset::Result;
using sicnu::dataset::RunStatus;

/// Serialization schema version of experiment store payloads.
inline constexpr int kExperimentSerializationVersion = 1;

/// Status transitions (truthful states; ADR 0130 no-fake-success rule).
bool isValidRunTransition( RunStatus from, RunStatus to );
bool isTerminalRunStatus( RunStatus status );

/// Canonical parameter identity: SHA-256 over canonicalizeJsonRfc8785 of the
/// parameter object. `{"a":1,"b":2}` and `{"b":2,"a":1}` collide — that is
/// the point (goal §23).
QString runConfigHash( const QJsonObject &parameters );

/// Execution identity of a run: canonical hash over algorithm/workflow
/// identity + version, canonical parameters, dataset version, split
/// manifest, model digest and seed.
struct RunExecutionIdentity
{
    QString algorithmId;       ///< operator id or workflow id
    QString algorithmVersion;
    QJsonObject parameters;    ///< canonicalized before hashing
    QString datasetVersionId;
    QString datasetFingerprint;
    QString splitManifestId;
    QString splitFingerprint;
    QString modelDigest;       ///< model catalog content digest ("" = n/a)
    quint64 seed = 0;

    friend bool operator==( const RunExecutionIdentity &, const RunExecutionIdentity & ) = default;
};

QString runExecutionFingerprint( const RunExecutionIdentity &identity );

/// Result identity: canonical hash over produced artifact digests + metrics
/// document. Two runs with equal result fingerprints produced equivalent
/// outputs (duplicate detection input, goal §36).
QString runResultFingerprint( const QStringList &artifactDigests,
                              const QJsonObject &metrics );

// --- Experiment -------------------------------------------------------------

class Experiment
{
  public:
    Experiment() = default;

    const QString &experimentId() const { return m_experimentId; }
    void setExperimentId( const QString &id ) { m_experimentId = id; }
    const QString &name() const { return m_name; }
    void setName( const QString &name ) { m_name = name; }
    /// The research question this experiment answers (goal §22).
    const QString &objective() const { return m_objective; }
    void setObjective( const QString &text ) { m_objective = text; }
    const QDateTime &createdAtUtc() const { return m_createdAtUtc; }
    void setCreatedAtUtc( const QDateTime &time ) { m_createdAtUtc = time; }
    const QStringList &tags() const { return m_tags; }
    QStringList &tags() { return m_tags; }

    const QStringList &runIds() const { return m_runIds; }
    QStringList &runIds() { return m_runIds; }

    QJsonObject toJson() const;
    static Result<Experiment> fromJson( const QJsonObject &json );

    friend bool operator==( const Experiment &, const Experiment & ) = default;

  private:
    QString m_experimentId;
    QString m_name;
    QString m_objective;
    QDateTime m_createdAtUtc;
    QStringList m_tags;
    QStringList m_runIds;
};

// --- RunEnvironment -----------------------------------------------------------

/// Allowlisted, secret-filtered environment snapshot (goal §29, ADR 0138).
/// There is NO full-environment dump path; anything not on the allowlist is
/// excluded by construction, and a denylist pass still runs over the result
/// (defense-in-depth, tested).
class RunEnvironment
{
  public:
    RunEnvironment() = default;

    static RunEnvironment captureCurrent();
    /// Deterministic reconstruction from an explicit field map (tests).
    static RunEnvironment fromFields( const QJsonObject &fields,
                                      const QHash<QString, QString> &envVariables = {} );

    const QJsonObject &fields() const { return m_fields; }
    const QHash<QString, QString> &envVariables() const { return m_envVariables; }

    /// Secret denylist over names AND values. Returns the filtered copy.
    /// Exposed for tests: the bundle path calls this before serialization.
    static QHash<QString, QString> filterSecrets( const QHash<QString, QString> &variables );

    QJsonObject toJson() const;
    static Result<RunEnvironment> fromJson( const QJsonObject &json );

    bool operator==( const RunEnvironment & ) const = default;

  private:
    QJsonObject m_fields;               // platform, versions, locale, …
    QHash<QString, QString> m_envVariables; // allowlisted SICNU_* etc.
};

// --- ExperimentRun --------------------------------------------------------------

class ExperimentRun
{
  public:
    ExperimentRun() = default;

    const QString &runId() const { return m_runId; }
    void setRunId( const QString &id ) { m_runId = id; }
    const QString &experimentId() const { return m_experimentId; }
    void setExperimentId( const QString &id ) { m_experimentId = id; }
    RunStatus status() const { return m_status; }
    void setStatus( RunStatus status ) { m_status = status; }

    // What was executed (the identity pins).
    const QString &algorithmId() const { return m_algorithmId; }
    void setAlgorithmId( const QString &id ) { m_algorithmId = id; }
    const QString &algorithmVersion() const { return m_algorithmVersion; }
    void setAlgorithmVersion( const QString &version ) { m_algorithmVersion = version; }
    const QJsonObject &parameters() const { return m_parameters; }
    void setParameters( const QJsonObject &parameters ) { m_parameters = parameters; }
    const QString &datasetVersionId() const { return m_datasetVersionId; }
    void setDatasetVersionId( const QString &id ) { m_datasetVersionId = id; }
    const QString &datasetFingerprint() const { return m_datasetFingerprint; }
    void setDatasetFingerprint( const QString &fingerprint ) { m_datasetFingerprint = fingerprint; }
    const QString &splitManifestId() const { return m_splitManifestId; }
    void setSplitManifestId( const QString &id ) { m_splitManifestId = id; }
    const QString &splitFingerprint() const { return m_splitFingerprint; }
    void setSplitFingerprint( const QString &fingerprint ) { m_splitFingerprint = fingerprint; }
    /// Model identity: catalog "id@version" + content digest (ADR 0137).
    const QString &modelId() const { return m_modelId; }
    void setModelId( const QString &id ) { m_modelId = id; }
    const QString &modelDigest() const { return m_modelDigest; }
    void setModelDigest( const QString &digest ) { m_modelDigest = digest; }
    quint64 seed() const { return m_seed; }
    void setSeed( quint64 seed ) { m_seed = seed; }
    DeterminismGrade determinism() const { return m_determinism; }
    void setDeterminism( DeterminismGrade grade ) { m_determinism = grade; }
    /// Required when determinism != Strict (goal §30).
    const QString &determinismNote() const { return m_determinismNote; }
    void setDeterminismNote( const QString &note ) { m_determinismNote = note; }

    // Where it ran.
    const RunEnvironment &environment() const { return m_environment; }
    void setEnvironment( const RunEnvironment &environment ) { m_environment = environment; }
    const QString &softwareRevision() const { return m_softwareRevision; }
    void setSoftwareRevision( const QString &revision ) { m_softwareRevision = revision; }
    /// Platform execution reference (taskId / workflow runId / operator run).
    const QString &executionRef() const { return m_executionRef; }
    void setExecutionRef( const QString &ref ) { m_executionRef = ref; }

    // Timing + lifecycle bookkeeping.
    const QDateTime &createdAtUtc() const { return m_createdAtUtc; }
    void setCreatedAtUtc( const QDateTime &time ) { m_createdAtUtc = time; }
    const QDateTime &startedAtUtc() const { return m_startedAtUtc; }
    void setStartedAtUtc( const QDateTime &time ) { m_startedAtUtc = time; }
    const QDateTime &finishedAtUtc() const { return m_finishedAtUtc; }
    void setFinishedAtUtc( const QDateTime &time ) { m_finishedAtUtc = time; }

    // What came out.
    struct Artifact
    {
        QString path;
        QString role;      ///< "primary", "sidecar", "report"…
        QString digest;    ///< SHA-256 hex ("" = not computed)
        qint64 sizeBytes = -1;
        friend bool operator==( const Artifact &, const Artifact & ) = default;
    };
    QVector<Artifact> &artifacts() { return m_artifacts; }
    const QVector<Artifact> &artifacts() const { return m_artifacts; }
    const QJsonObject &metrics() const { return m_metrics; }
    void setMetrics( const QJsonObject &metrics ) { m_metrics = metrics; }

    // Derived identity helpers.
    RunExecutionIdentity executionIdentity() const;
    QString configHash() const { return runConfigHash( m_parameters ); }
    QString resultFingerprint() const;

    QJsonObject toJson() const;
    static Result<ExperimentRun> fromJson( const QJsonObject &json );

    friend bool operator==( const ExperimentRun &, const ExperimentRun & ) = default;

  private:
    QString m_runId;
    QString m_experimentId;
    RunStatus m_status = RunStatus::Created;
    QString m_algorithmId;
    QString m_algorithmVersion;
    QJsonObject m_parameters;
    QString m_datasetVersionId;
    QString m_datasetFingerprint;
    QString m_splitManifestId;
    QString m_splitFingerprint;
    QString m_modelId;
    QString m_modelDigest;
    quint64 m_seed = 0;
    DeterminismGrade m_determinism = DeterminismGrade::Strict;
    QString m_determinismNote;
    RunEnvironment m_environment;
    QString m_softwareRevision;
    QString m_executionRef;
    QDateTime m_createdAtUtc;
    QDateTime m_startedAtUtc;
    QDateTime m_finishedAtUtc;
    QVector<Artifact> m_artifacts;
    QJsonObject m_metrics;
};

// --- Comparison ------------------------------------------------------------------

/// Comparability verdict over two runs (goal §27): the structural diffs come
/// FIRST; metric diffs only make sense within the verdict's meaning.
struct RunDiffItem
{
    QString dimension;   ///< "dataset", "split", "model", "algorithm", "config", "seed", "environment"
    bool differs = false;
    QString detail;

    QJsonObject toJson() const;
};

struct RunComparison
{
    enum class Verdict
    {
        Comparable,                  ///< every pin identical
        ComparableWithDifferences,   ///< pins differ only in declared dims (e.g. config)
        NotComparable,               ///< dataset/split/model identity differ
    };

    QVector<RunDiffItem> dimensions;
    Verdict verdict = Verdict::Comparable;
    QStringList reasons;

    QJsonObject toJson() const;
    static RunComparison fromJson( const QJsonObject &json );

    /// Structural comparability of @p a vs @p b. Metrics are NOT consulted:
    /// comparability is about identity pins, not numbers.
    static RunComparison compare( const ExperimentRun &a, const ExperimentRun &b );

    /// Metric deltas for comparable runs (reported by name; only numeric
    /// leaf values present in both).
    QJsonObject metricDiff( const ExperimentRun &a, const ExperimentRun &b ) const;
};

} // namespace sicnu::experiment
