// grader_engine.h — top-level facade of the process-aware experiment grader.
//
// grade() consumes a validated rubric + evidence bundle and produces a
// deterministic GradeReport, or a typed refusal (no report). The engine is
// pure: no I/O, no clock, no store access. Collectors that project workflow
// provenance / checkpoints / experiment-store records into GradeEvidence
// documents are the caller's integration job (see docs/integration.md and
// grader_adapters.h for the document shapes).
#pragma once

#include "grader_types.h"

namespace sicnu::grader {

struct GradeOutcome
{
    bool ok = false;
    GraderError error;
    GradeReport report;
};

/// Grade an evidence bundle against a rubric. Both inputs are validated
/// first; an invalid document is a typed hard error (fail closed, no
/// partial grading of garbage).
GradeOutcome grade( const GradingRubric &rubric, const GradeEvidence &evidence );

} // namespace sicnu::grader
