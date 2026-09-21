// grader_engine.cpp — Slice A: fail-closed validation only. The matching and
// scoring engines land in slices B/C; a valid (rubric, evidence) pair still
// refuses to grade with a typed error until then.
#include "grader/grader_engine.h"

namespace sicnu::grader {

GradeOutcome grade( const GradingRubric &rubric, const GradeEvidence &evidence )
{
    GradeOutcome out;
    if ( GraderError error; !rubric.validate( error ) ) {
        out.error = error;
        return out;
    }
    if ( GraderError error; !evidence.validate( error ) ) {
        out.error = error;
        return out;
    }
    const std::size_t evidenceCount = evidence.items.size();
    if ( evidenceCount > static_cast<std::size_t>( rubric.budgets.maxEvidenceItems ) ) {
        out.error = makeError( GraderErrorCode::SchemaShapeInvalid,
                               "evidence bundle holds " + std::to_string( evidenceCount ) + " items, over the rubric budget " +
                                   std::to_string( rubric.budgets.maxEvidenceItems ),
                               "items" );
        return out;
    }
    out.error = makeError( GraderErrorCode::Internal, "grading engine not implemented (slices B/C)" );
    return out;
}

} // namespace sicnu::grader
