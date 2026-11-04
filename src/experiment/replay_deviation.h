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
#include <QStringList>
#include <QVector>

namespace sicnu::experiment
{

/// Layout version of deviation reports. v2 adds deviation_details[] (typed
/// entries with domain attribution + remediation); every v1 field keeps its
/// name and semantics, and the verdict logic is untouched.
inline constexpr int kReplayDeviationSchemaVersion = 2;

/// Which side of the experiment a deviation belongs to (R25-B): the data
/// pins, the model pins, the experiment's own configuration, or the
/// environment record. One vocabulary for consumers to route remediation.
enum class ReplayDeviationDomain
{
    Data,
    Model,
    Experiment,
    Environment
};

/// One typed deviation (v2). Carries everything the flat v1 string did,
/// plus the domain attribution and an actionable remediation.
struct ReplayDeviationEntry
{
    QString dimension;      ///< pin/diagnostic dimension ("model", "dataset", ...)
    ReplayDeviationDomain domain = ReplayDeviationDomain::Experiment;
    bool identityPin = false; ///< true when this dimension decides the verdict
    QString detail;         ///< what differs (same text as the v1 list)
    QString remediation;    ///< actionable next step; never empty

    QJsonObject toJson() const;
};

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
    /// Typed deviation entries (schema v2): one per string above, with
    /// domain attribution, identity-pin flag and remediation.
    QVector<ReplayDeviationEntry> deviationDetails;
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
