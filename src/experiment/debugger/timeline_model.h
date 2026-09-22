// timeline_model.h — Qt-free dual-run timeline diff model (RS14-06, ADR 0174).
// Slice F.
//
// The model a GUI renders (and a CLI prints): one ordered entry list covering
// BOTH runs, each entry in one closed status, with the first significant
// divergence flagged. Building the model is pure projection — the GUI never
// re-derives divergence semantics, it renders this model.
//
// Teaching/agent consistency contract: the entry flagged isFirstDivergence
// carries exactly the step ids of report.firstDivergence — the human view and
// the machine view can never disagree about WHERE things went wrong.
#pragma once

#include "first_divergence.h"
#include "run_snapshot.h"
#include "step_aligner.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace sicnu::experiment::debugger
{

struct TimelineEntry
{
    QString referenceStepId;
    QString studentStepId;
    QString operatorId;
    /// matched_identical | matched_divergent | matched_incomparable |
    /// missing_in_student | extra_in_student
    QString status;
    /// divergence kind name when matched_divergent ("" otherwise)
    QString divergenceKind;
    CausalConfidence confidence = CausalConfidence::None;
    QString equivalenceRuleId;
    bool isFirstDivergence = false;

    QJsonObject toJson() const;
    bool operator==( const TimelineEntry & ) const = default;
};

class TimelineDiffModel
{
  public:
    /// Builds the ordered dual-run timeline: matched pairs in reference
    /// topological order, then reference-only steps, then student-only
    /// steps (each flagged with its side's position, never interleaved
    /// arbitrarily).
    static QVector<TimelineEntry> build( const RunSnapshot &reference,
                                         const RunSnapshot &student,
                                         const AlignmentResult &alignment,
                                         const FirstDivergenceReport &report );
};

} // namespace sicnu::experiment::debugger
