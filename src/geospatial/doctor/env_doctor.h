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
