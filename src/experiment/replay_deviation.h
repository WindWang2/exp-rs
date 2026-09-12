// replay_deviation.h — Replay deviation reporting (goal M7).
//
// replay_readiness answers "CAN this run be replayed?" BEFORE a replay;
// this module answers "WHAT changed?" AFTER one: it compares the recorded
// original against the recorded replay and reports every deviation in typed
// form. Verdicts are honest:
//   identical   — same result fingerprint (outputs AND metrics equal)
//   equivalent  — identity pins equal, result fingerprints differ (the
//                 permitted rerun case: nondeterministic kernels under a
//                 declared determinism note)
//   deviated    — at least one identity pin differs (the replay is NOT the
//                 same experiment; every differing pin is listed)
//   incomplete  — evidence missing on either side; no verdict is possible
//                 without guessing (which this module refuses to do).
#pragma once

#include "experiment_store.h"
#include "experiment_types.h"

#include <QJsonObject>

namespace sicnu::experiment
{

/// Layout version of deviation reports.
inline constexpr int kReplayDeviationSchemaVersion = 1;

struct ReplayDeviationReport
{
    enum class Verdict
    {
        Identical,
        Equivalent,
        Deviated,
        Incomplete,
    };

    Verdict verdict = Verdict::Incomplete;
    QString originalRunId;
    QString replayRunId;
    /// Structured pin comparison (from RunComparison::compare).
    QJsonObject pinComparison;
    /// Metric deltas for comparable runs (empty for deviated runs — numbers
    /// across different identities are exactly the apple-to-orange compare
    /// the platform refuses to print).
    QJsonObject metricDelta;
    /// Environment field drift: field name → {original, replay}.
    QJsonObject environmentDrift;
    /// Human-readable deviation list (one entry per deviation).
    QStringList deviations;
    /// Typed evidence gaps preventing a stronger verdict.
    QStringList evidenceGaps;

    QJsonObject toJson() const;
};

class ReplayDeviationAnalyzer
{
  public:
    explicit ReplayDeviationAnalyzer( ExperimentStore &store );

    /// Compares two recorded runs. Fails typed when either id is unknown.
    Result<ReplayDeviationReport> analyze( const QString &originalRunId,
                                           const QString &replayRunId ) const;

  private:
    ExperimentStore *m_store = nullptr;
};

} // namespace sicnu::experiment
