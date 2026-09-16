/***************************************************************************
  geospatial/doctor/env_doctor.h
  Deployment 11.0 (F19) — local runtime environment self-check ("env-doctor").
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  Contract:
  * Qt-free (GDAL + jsoncpp + std only), mirroring doctor/data_doctor.h:
    every finding is a structured record {check, severity, message, detail,
    diagnostic}; severities: ok / info / warning / error.
  * strictly read-only probes plus bounded scratch writes the host already
    owns (a unique file under the system temp directory, deleted on every
    path); nothing outside temp is created, nothing is enabled or disabled —
    in particular the offline gate and GDAL network deny are REPORTED, never
    toggled.
  * a broken environment is still a valid report: every probe failure is a
    finding naming the exact missing item (driver / database / path / library)
    so first-run diagnosis points at the fix, not at a loader dialog.
  * findings map to curated prose ids in data/help/diagnostics.json
    (family "env"); the id is carried verbatim in the `diagnostic` field and
    must exist there — never invented ad hoc at the call site.
  * report envelope: {"schema":"exp.env.report.v1", host, checks[], counts{}};
    worst() is the CLI exit-contract input (error → broken, warning →
    degraded, else healthy).
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_ENV_DOCTOR_H
#define SICNU_GEOSPATIAL_ENV_DOCTOR_H

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::geo::envcheck
{

struct EnvCheckOptions
{
    /// Directory of the running executable (QCoreApplication::applicationDirPath
    /// on the CLI side; tests pass a fixture tree). Empty = skip exe-relative
    /// probes. Used only to locate runtime data markers, never written.
    std::string applicationDir;
    /// Current working directory actually used for cwd-relative probes
    /// (std::filesystem::current_path when left empty).
    std::string currentDir;
    /// Drivers the deployment contract requires. Empty = the built-in lab set
    /// (GTiff, GPKG, GeoJSON, ESRI Shapefile, MEM, VRT); tests and future
    /// bundle profiles may declare a different closure.
    std::vector< std::string > requiredDrivers;
    /// Directories to scan for proj.db. Empty = the built-in env/GDAL-relative
    /// /system scan. Injected candidates are listed in the finding's `probed`
    /// detail either way.
    std::vector< std::string > projDataCandidates;
};

struct EnvFinding
{
    std::string check;        ///< stable probe id (e.g. "gdal.drivers.required")
    std::string severity;     ///< "ok" | "info" | "warning" | "error"
    std::string message;      ///< single line, honest
    Json::Value detail;       ///< optional structured detail (paths, versions)
    std::string diagnostic;   ///< curated id in data/help/diagnostics.json, "" = none

    Json::Value toJson() const;
};

struct EnvDoctorReport
{
    std::string platform;     ///< "linux" | "windows" | "macos" | "unknown"
    Json::Value checks;       ///< array of EnvFinding
    int okCount = 0;
    int infoCount = 0;
    int warningCount = 0;
    int errorCount = 0;

    /// Single append point for severity bookkeeping — used by the geospatial
    /// probes and the Qt/CLI layer alike so the counters cannot drift.
    void append( const char *severity, const char *checkId, const std::string &message,
                 const Json::Value &detail = Json::Value(), const char *diagnostic = "" );

    Json::Value toJson() const;
    /// "error" | "warning" | "info" | "ok" by worst finding present.
    const char *worst() const;
};

/// Full environment pass. Idempotent, single-threaded by convention (like the
/// CLI startup path that calls it). Never throws on probe failure — a failed
/// probe IS a finding.
EnvDoctorReport runEnvironmentDoctor( const EnvCheckOptions &options = {} );

} // namespace sicnu::geo::envcheck

#endif // SICNU_GEOSPATIAL_ENV_DOCTOR_H
