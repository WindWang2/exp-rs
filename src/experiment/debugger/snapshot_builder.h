// snapshot_builder.h — RunSnapshot assembly from evidence sources
// (RS14-06, ADR 0174).
#pragma once

#include "../../data/data_result.h"
#include "evidence_source.h"
#include "run_snapshot.h"

#include <QString>

namespace sicnu::experiment::debugger
{

/// Builds the normalized snapshot of one recorded run by joining the
/// run-level record with the richest available step evidence. Fails typed
/// (kCodeUnknownRun) when the run is unknown; succeeds with
/// StepEvidenceMode::Absent when the run exists but carries no step
/// evidence — that is a valid, honestly-degraded snapshot.
class RunSnapshotBuilder
{
  public:
    explicit RunSnapshotBuilder( IRunEvidenceSource &source );

    sicnu::data::Result<RunSnapshot> build( const QString &runId ) const;

  private:
    IRunEvidenceSource *m_source = nullptr;
};

} // namespace sicnu::experiment::debugger
