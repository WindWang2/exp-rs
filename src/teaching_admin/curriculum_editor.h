// curriculum_editor.h — Course Builder: schema-driven curriculum edit + validate + export/diff.
// Does NOT duplicate LabSpec into curriculum; lab refs only.
#pragma once

#include "admin_types.h"

#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>

namespace sicnu::teaching_admin {

struct CurriculumPaths
{
    QString labsDir;       ///< data/labs
    QString packsDir;      ///< data/labs/packs
    QString registryPath;  ///< data/labs/lab-registry.json
};

/// Validate a sicnu.curriculum/1 document in-memory (cycle, missing lab,
/// duplicate id, unknown keys, forward refs). Known lab ids come from
/// filesystem probe + registry (injected via @p knownLabIds for tests).
ValidationResult validateCurriculum( const QJsonObject &manifest,
                                     const QSet<QString> &knownLabIds,
                                     const QSet<QString> &knownPackIds = {} );

/// Load lab ids resolvable from labsDir + registry JSON (best-effort).
QSet<QString> discoverKnownLabIds( const CurriculumPaths &paths );

QSet<QString> discoverKnownPackIds( const CurriculumPaths &paths );

/// Canonical export (sorted keys) for round-trip determinism.
QJsonObject exportCanonicalCurriculum( const QJsonObject &manifest );

/// Structural diff of two curriculum documents (module/lab ids + field changes).
QJsonObject diffCurriculum( const QJsonObject &previous, const QJsonObject &current );

/// Thin student course-home preview VM (duplicated idea; does not use #1237 sources).
QJsonObject projectCourseHomePreview( const QJsonObject &manifest );

} // namespace sicnu::teaching_admin
