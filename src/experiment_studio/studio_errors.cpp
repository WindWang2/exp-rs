#include "experiment_studio/studio_errors.h"

namespace sicnu::experiment_studio
{

Diagnostic studioError( const QString &code, const QString &message )
{
    Diagnostic d;
    d.code = code;
    d.message = message;
    d.severity = sicnu::data::DiagnosticSeverity::Error;
    return d;
}

} // namespace sicnu::experiment_studio
