/***************************************************************************
  geospatial/products/sensor_profile.h
  Sensor/Product Physics Platform 10.0 — sensor profile registry loader.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  data/products/sensor_profiles/*.json is the single sensor-truth authority
  for the CN satellite families (ADR 0147): platform, instrument, sensor
  mode, band layout with physical roles, documented spectral ranges and
  centre wavelengths, nominal GSD, expected product constituents and the
  declared calibration rule. Replaces the band-role-only tables of ADR 0157.

  Policy: the loader is fail-closed — a missing/unparseable file or a schema
  violation is a structured error, never a silent "unknown". Unknown keys
  inside a sensor entry are ignored but reported (forward compatibility is
  explicit, not silent). Nothing is invented: a wavelength/FWHM field is
  present only when a published source documents it.
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_SENSOR_PROFILE_H
#define SICNU_GEOSPATIAL_SENSOR_PROFILE_H

#include "geospatial/common.h"

#include <string>
#include <vector>

namespace sicnu::geo
{

/// One band of a sensor profile (registry v1).
struct SensorBandProfile
{
    std::string band;        ///< native band id ("B1", "MS1", ...)
    std::string role;        ///< ADR 0065 vocabulary; "unknown" must carry roleReason
    std::string roleReason;  ///< why role is "unknown" ("" when mapped)
    bool hasWavelengthNm = false;        ///< documented spectral-range midpoint
    double wavelengthNm = 0.0;
    bool hasCenterWavelengthNm = false;  ///< documented nominal band centre
    double centerWavelengthNm = 0.0;
    bool hasFwhmNm = false;              ///< documented FWHM (never a range width)
    double fwhmNm = 0.0;
    std::string spectralRangeUm;         ///< documented range, verbatim ("0.45-0.52")
    bool hasGsdM = false;                ///< band-level nominal GSD
    double gsdM = 0.0;
    std::string note;
};

/// Full sensor profile (one registry entry).
struct SensorProfileRecord
{
    std::string sensorKey;
    std::string satellite;       ///< "GF1", "ZY3", "HJ2A/HJ2B", ...
    std::string instrument;      ///< "PMS", "WFV", "CCD", "NAD", "HRC"
    std::string sensorMode;      ///< "PMS", "PMS-PAN", "NAD-MS", "FWD", ...
    std::string modality;        ///< optical / sar / thermal / hyperspectral
    bool hasGsdM = false;
    double gsdM = 0.0;
    std::string panVariant;      ///< sensor key of the panchromatic sibling, "" when none
    std::string msVariant;       ///< sensor key of the multispectral sibling, "" when none
    std::vector<std::string> constituents; ///< expected product constituents
    std::string calibrationRule;           ///< declared-coefficient semantics text
    std::vector<std::string> qaVocabulary; ///< documented QA flag names (may be empty)
    int schemaVersion = 0;                 ///< registry file version
    std::string source;                    ///< provenance note carried by the file
    std::string familyFile;                ///< registry file the entry came from
    std::vector<std::string> unknownKeys;  ///< forward-compat report (ignored keys)
    std::vector<SensorBandProfile> bands;
    /// v2 `band_axis` declaration (hyperspectral band axes): the bands array
    /// stays the per-band truth and is written out in full; the axis block
    /// only describes extent/ordering/bad-band flags for consumers. Absent
    /// for v1 entries and for optical layouts.
    bool hasBandAxis = false;
    int bandAxisCount = 0;                 ///< declared axis extent (== bands.size())
    std::string bandAxisOrdering;          ///< declared ordering note, verbatim
    std::vector<int> badBandIndices;       ///< 0-based indices of flagged/dead bands

    /// Case-insensitive band lookup; nullptr when the band is not in the layout.
    const SensorBandProfile *findBand( const std::string &bandId ) const;
};

/// Resolves data/products/sensor_profiles the way the processing layer
/// resolves data/ paths (SICNU_DATA_DIR → walk-up → SICNU_SOURCE_DIR).
/// Empty when not found (callers fail closed).
std::string sensorProfileDir();

/// Lists the sensor keys declared by the registry (all family files, bounded).
/// Files that cannot be parsed are reported in @p warnings and skipped —
/// enumeration is a discovery aid; loading a specific sensor stays
/// fail-closed.
std::vector<std::string> sensorProfileKeys( std::vector<std::string> *warnings = nullptr );

/// Loads the profile for a sensor key. Throws GeoError(OpenFailed) when the
/// registry directory/file is missing and GeoError(InvalidArgument) when the
/// file does not declare the key or violates the declared version's schema.
/// v2 files get the strict per-field rules (types, finite positive physical
/// quantities with declared units, role vocabulary, pan/ms reference
/// integrity, band-id uniqueness); v1 files keep the historical rules.
SensorProfileRecord loadSensorProfile( const std::string &sensorKey );

/// True when the registry declares @p sensorKey (non-throwing discovery).
bool hasSensorProfile( const std::string &sensorKey );

/// One validator finding. @p file/@p sensorKey/@p band locate the entry
/// (band empty for entry/file-level findings); @p message names the violated
/// rule and the offending value. Never a throw — validation reports, the
/// loader refuses.
struct SensorProfileValidationIssue
{
    std::string file;      ///< registry file name ("gaofen.json")
    std::string sensorKey; ///< entry key ("" for file-level findings)
    std::string band;      ///< band id ("" for non-band findings)
    std::string message;
};

/// Validates every registry file in sensorProfileDir() under the rules of
/// its declared version (v1 files get the historical rules; v2 files the
/// strict rules) plus the registry-wide cross-references (pan_variant /
/// ms_variant must resolve to declared keys). Returns all findings; an
/// empty vector means the registry is clean. Unreadable/unparseable files
/// are reported as findings, never thrown — validation is a diagnostic over
/// the whole registry, not a single-entry load.
std::vector<SensorProfileValidationIssue> validateSensorProfiles();

} // namespace sicnu::geo

#endif // SICNU_GEOSPATIAL_SENSOR_PROFILE_H
