// replay_readiness.h — replay readiness assessment + historical run lookup
// (goal 7.0 §F).
//
// A reproduction judgment is only as good as its evidence, so every check
// reports its own status and detail; the overall level NEVER overstates:
//   - any missing/mismatched REQUIRED pin  → Impossible
//   - otherwise any unknown dependency     → BestEffort
//   - otherwise                            → Exact
// The model/algorithm/artifact checks use the same ReproductionHooks the
// bundle exporter validates with; unwired hooks are "unknown", never a fake
// ok.
#pragma once

#include "experiment_store.h"
#include "experiment_types.h"
#include "reproduction_bundle.h"

namespace sicnu::dataset
{
class DatasetStore;
}

namespace sicnu::experiment
{

/// Status of one dependency check.
enum class ReplayCheckStatus
{
    Ok,
    Missing,
    Mismatched,
    Unknown,
};

QString replayCheckStatusToString( ReplayCheckStatus status );

/// One dependency check with its evidence.
struct ReplayCheck
{
    QString dependency;
    ReplayCheckStatus status = ReplayCheckStatus::Unknown;
    QString detail;

    QJsonObject toJson() const;
};

/// Full readiness report for one run.
struct ReplayReadinessReport
{
    sicnu::dataset::ReproductionLevel level = sicnu::dataset::ReproductionLevel::Impossible;
    QVector<ReplayCheck> checks;
    QStringList notes;

    /// The dependencies blocking a replay (missing/mismatched) — the
    /// "missing dependency diagnostics" of the goal.
    QStringList missingDependencyDiagnostics() const;

    QJsonObject toJson() const;
};

class ReplayReadiness
{
  public:
    /// Assesses one run WITHOUT a bundle: dataset version + fingerprint,
    /// split manifest + fingerprint, artifacts on disk, model/algorithm via
    /// @p hooks. @p datasetStore may be null (dataset/split become unknown).
    static ReplayReadinessReport assess( const ExperimentRun &run,
                                         const sicnu::dataset::DatasetStore *datasetStore,
                                         const ReproductionHooks &hooks );

    /// Historical comparison: stored runs (excluding @p runId itself) whose
    /// execution fingerprint equals @p fingerprint — "has this exact
    /// execution happened before, and did it produce the same record?" is
    /// answerable by pairing their result fingerprints.
    static QStringList equivalentRuns( const ExperimentStore &store, const QString &fingerprint,
                                       const QString &excludeRunId );
};

} // namespace sicnu::experiment
