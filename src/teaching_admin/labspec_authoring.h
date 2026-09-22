// labspec_authoring.h — Lab Authoring validation against real operator registry.
// Recipe compile view is read-only; LabSpec is authoring truth.
#pragma once

#include "admin_types.h"

#include <QJsonObject>
#include <QSet>
#include <QString>

namespace sicnu::teaching_admin {

/// Closed set of LabSpec top-level fields we accept when authoring
/// (unknown field = error). Matches sicnu.labspec.v1 + common lab.json keys.
QSet<QString> legalLabspecTopKeys();

/// Validate LabSpec document. @p knownOperators from real registry (or test allow-list).
/// Unknown operator / unknown field / illegal param key → error.
ValidationResult validateLabSpec( const QJsonObject &spec,
                                  const QSet<QString> &knownOperators,
                                  const QJsonObject &operatorParamSchemas = {} );

/// Read-only recipe/pipeline compile projection (not authoring truth).
QJsonObject projectRecipeCompileView( const QJsonObject &labSpec,
                                      const QJsonObject &pipelineDoc = {} );

} // namespace sicnu::teaching_admin
