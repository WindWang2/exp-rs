#pragma once

#include "data/data_result.h"

#include <QString>

namespace sicnu::experiment_studio
{

using sicnu::data::Diagnostic;
using sicnu::data::Result;

/// Typed diagnostic factory (dotted "experiment_studio.*" codes).
Diagnostic studioError( const QString &code, const QString &message );

} // namespace sicnu::experiment_studio
