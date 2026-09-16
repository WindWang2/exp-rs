/***************************************************************************
 * src/cli/lab_self_check.h — `lab --self-check`: offline classroom
 * environment diagnostics (teaching-lab-platform-11, package F).
 *
 * A teacher at 23:00 runs ONE command and gets a typed, deterministic JSON
 * diagnostic answering:
 *   * is the offline gate engaged (and what would it refuse)?
 *   * does GDAL/PROJ resolve (EPSG:4326 sanity — the projection DB is the
 *     most common broken-install failure)?
 *   * does every lab data pack verify (fixtures checksum-clean, generated
 *     inputs present or honestly missing)?
 *   * does every grading rules file parse through the REAL grading seam
 *     (OutputVerifier::gradeArtifact — never a second parser)?
 *
 * Determinism: the document carries NO wall clock; identical machine state
 * gives byte-identical output. Exit contract: Ok = every check passed;
 * GenericError = at least one check degraded/failed (details in the JSON).
 ***************************************************************************/
#pragma once

#include <json/json.h>

#include <QString>

namespace sicnu::cli {

struct LabSelfCheckOptions
{
    /// Verification root for data packs. Empty = the compile-time source dir,
    /// overridable per call (bundle deployments pass the bundle root).
    QString packRoot;
    /// Verify only this lab's pack (empty = every pack in <root>/data/labs/packs).
    QString labId;
    /// Grading rules directory. Empty = the rules resolution default
    /// (SICNU_LAB_RULES_DIR > source-tree data/labs/grading).
    QString rulesDir;
    /// Verify packs for labs WITH executable rules only (skips generated-tmp
    /// packs that cannot exist in a fresh deployment).
    bool packsOnly = false;
};

/// Runs every check. Returns the typed document:
///   {schema: "sicnu.lab.self-check/1", overall: ok|degraded|failed,
///    checks: [{check, status, detail?}, ...]}
/// (no timestamps — determinism contract).
Json::Value runLabSelfCheck( const LabSelfCheckOptions &options, bool *ok );

} // namespace sicnu::cli
