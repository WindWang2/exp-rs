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

/// Validate LabSpec document. @p knownOperators from real registry (or test allow-list);
/// an empty set is fail-closed (every operator reference flagged). Unknown operator /
/// unknown field / illegal param key → error. Unsupported `schema` strings and
/// out-of-range `spec_version` values are typed errors — never silently accepted.
/// When @p repoRoot is non-empty, repo-relative references (grading_ref.intent_ref,
/// data.spec_ref) must resolve to existing files or a typed `dangling_ref` error is
/// raised; with an empty root existence is not checked (pure structural lint).
ValidationResult validateLabSpec( const QJsonObject &spec,
                                  const QSet<QString> &knownOperators,
                                  const QJsonObject &operatorParamSchemas = {},
                                  const QString &repoRoot = {} );

/// Read-only recipe/pipeline compile projection (not authoring truth).
QJsonObject projectRecipeCompileView( const QJsonObject &labSpec,
                                      const QJsonObject &pipelineDoc = {} );

} // namespace sicnu::teaching_admin
