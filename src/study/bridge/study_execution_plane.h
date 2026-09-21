// study_execution_plane.h — production execution adapter for study runs (RS14-07).
//
// A thin bridge leaf in the spirit of sicnu_experiment_bridge: converts study
// points into ExecutionRequest submissions on the SINGLE execution spine
// (ExecutionPlane → TaskCenter admission → JobEngine → operator) and converts
// the committed task result back into the port's outcome type. Deliberately
// NOT part of sicnu_study (the science core must not link the execution
// stack); consumers are opt-in surfaces (tests, future CLI/GUI wiring).
//
// Commit policy (explicit, documented): the operator's temporary output is
// moved ATOMICALLY (same-directory staged rename) to the runner-assigned
// stable "output" path. NO catalog asset is registered — a 100-point sweep
// must not flood the catalog; the experiment run record (parameters, seed,
// artifacts, executionRef) plus the study report are the provenance surface.
//
// Ordering assumption: the study submission is the ONLY surface that builds
// the committed payload for its task ids (the runner awaits each submission
// it owns; ExecutionPlane's commit is once-per-task, first builder wins).
// A foreign surface that snipes a study task's payload build would commit
// with the default catalog committer instead — the runner's output-path
// guard (study.run_output_mismatch) turns that into a typed failure, not a
// silently poisoned artifact.
#pragma once

#include "study/study_execution.h"

#include <QString>

#include <atomic>

namespace sicnu::study
{

class ExecutionPlaneStudyBackend : public IStudyExecutionBackend
{
  public:
    ExecutionPlaneStudyBackend() = default;

    Result<std::unique_ptr<StudySubmission>> submit( const QString &algorithmId,
                                                    const QJsonObject &pointParameters,
                                                    const QString &correlationId,
                                                    std::chrono::milliseconds timeout ) override;
};

} // namespace sicnu::study
