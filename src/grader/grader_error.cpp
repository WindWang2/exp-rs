// grader_error.cpp — machine-readable code spellings.
#include "grader_error.h"

namespace sicnu::grader {

std::string toCodeString( GraderErrorCode code )
{
    switch ( code ) {
    case GraderErrorCode::None:
        return "grader:e-none";
    case GraderErrorCode::SchemaVersionUnsupported:
        return "grader:e-schema-version";
    case GraderErrorCode::SchemaShapeInvalid:
        return "grader:e-schema-shape";
    case GraderErrorCode::InvalidJson:
        return "grader:e-invalid-json";
    case GraderErrorCode::Internal:
        return "grader:e-internal";
    }
    return "grader:e-internal";
}

} // namespace sicnu::grader
