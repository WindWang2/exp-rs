/***************************************************************************
  geospatial/doctor/data_doctor.h
  Geospatial I/O Foundation 4.0 — read-only structured dataset diagnostics.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  Contract:
  * strictly read-only: no metadata domains are written, no statistics are
    forced into PAM, sidecars are only enumerated (never created)
  * every finding is a structured record {check, severity, message, detail};
    severities: ok / info / warning / error
  * a dataset that cannot be opened is still a valid report (readability is
    the first check), so operators and the CLI always return usable output
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_DATA_DOCTOR_H
#define SICNU_GEOSPATIAL_DATA_DOCTOR_H

#include "geospatial/common.h"
#include "geospatial/metadata/canonical_metadata.h"

#include <json/json.h>

#include <string>

namespace sicnu::geo
{

struct DoctorReport
{
    std::string path;
    std::string kind;          ///< "raster" | "vector" | "multidimensional" | "unreadable"
    std::string driver;
    bool readable = false;
    int errorCount = 0;
    int warningCount = 0;
    Json::Value findings;      ///< array of {check, severity, message, detail?}

    Json::Value toJson() const;
};

/// Full doctor pass. options.includeStatistics additionally computes bounded
/// per-band statistics (the only potentially-costly opt-in).
DoctorReport runDoctor( const std::string &path, const InspectOptions &options = {} );

/// Lighter inspect pass: canonical metadata only (no quality findings).
/// This backs `data inspect`; doctor adds the findings on top.
Json::Value runInspect( const std::string &path, const InspectOptions &options = {} );

} // namespace sicnu::geo

#endif // SICNU_GEOSPATIAL_DATA_DOCTOR_H
