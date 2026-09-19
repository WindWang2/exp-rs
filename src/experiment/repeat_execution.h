// repeat_execution.h — repeat-execution classification (12.0, ADR 0137).
//
// "Did THIS execution already happen?" is a store-scale question: an agent
// re-submitting a job, a workflow retry, or a batch re-ingest must be able
// to ask whether the identity pins (algorithm + version + canonical params
// + dataset version + split + model digest + seed + software revision) match
// a recorded run, and whether the recorded outcome makes the repeat a
// DUPLICATE (same results), a RERUN (identity equal, results differ under a
// declared determinism note), or a DEVIATION (same executionRef, different
// pins — the silently-changed-experiment case).
//
// The environment is deliberately NOT part of the identity hash (ADR 0137):
// identical pins on a different machine are the same experiment. Environment
// drift between identity twins is therefore REPORTED, not punished.
#pragma once

#include "experiment_store.h"
#include "experiment_types.h"

#include <QJsonObject>

namespace sicnu::experiment
{

/// Schema version of classifier verdict payloads.
inline constexpr int kRepeatExecutionSchemaVersion = 1;

class RepeatExecutionClassifier
{
  public:
    enum class Classification
    {
        New, ///< no recorded run shares the identity pins
        /// Identity AND result fingerprint equal a recorded run: duplicate
        /// execution (the repeat produced the same outputs).
        SameExecution,
        /// Identity equal to recorded run(s) but no result evidence was
        /// supplied: a duplicate CANDIDATE — duplicate-vs-rerun cannot be
        /// decided without guessing, which this module refuses to do.
        SameIdentity,
        /// Identity equal, results differ: a rerun. Honest only under a
        /// declared nondeterminism grade on the matched run.
        EquivalentRerun,
        /// The executionRef matches recorded run(s) whose identity pins
        /// DIFFER — the same platform execution re-run with changed inputs.
        Deviated,
    };

    struct Verdict
    {
        Classification classification = Classification::New;
        /// Recorded runs the verdict was decided against (may be fewer than
        /// the store matches — bounded lookup).
        QStringList matchedRunIds;
        /// Pin-level comparison against the first matched run (Deviation
        /// evidence; empty otherwise). From RunComparison::compare.
        QJsonObject pinComparison;
        /// Environment drift against the first matched run: field name →
        /// {original, repeat}. Identity twins with drifted environments are
        /// still the same experiment (ADR 0137) — this is evidence, not a
        /// verdict downgrade.
        QJsonObject environmentDrift;
        QStringList reasons;

        QJsonObject toJson() const;
    };

    explicit RepeatExecutionClassifier( ExperimentStore &store );

    /// Classifies one execution against the store. @p resultFingerprint may
    /// be empty (execution still running / no outputs yet); @p executionRef
    /// enables the Deviated detection for platform executions that already
    /// recorded runs under other pins; @p repeatEnvironment (when supplied)
    /// is compared field-by-field against the matched run's environment —
    /// drift is reported as evidence and never changes the verdict (ADR
    /// 0137: environment is not an identity pin).
    Result<Verdict> classify(
        const RunExecutionIdentity &identity,
        const QString &resultFingerprint = QString(),
        const QString &executionRef = QString(),
        const std::optional<RunEnvironment> &repeatEnvironment = std::nullopt ) const;

  private:
    ExperimentStore *m_store = nullptr;
};

} // namespace sicnu::experiment
