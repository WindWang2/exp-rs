/***************************************************************************
  geospatial/products/cn_product_metadata.h
  Geospatial I/O Foundation 4.0 — Chinese satellite product metadata
  (GF-1/2/6 PMS/WFV, ZY-3 TLC/NAD/FWD/BWD, HJ-1A/1B CCD).
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  One canonical place that knows how CRESDA-family L1A sidecar XML documents
  describe Chinese satellite products. Everything is read-only and parsed
  offline from the product's own files; nothing is fetched, nothing invented.

  Policy (matches product_adapters.h): everything declared by the product is
  copied verbatim; absence is absence — calibration coefficients, sun
  geometry and orbit/scene ids that the sidecar does not declare are reported
  as explicitly missing, never defaulted.

  Band→role mapping is data-driven from data/products/band_roles/*.json
  (ADR 0146). The loader is fail-closed: a missing/unparseable table is a
  structured error, not a silent "unknown".
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_CN_PRODUCT_METADATA_H
#define SICNU_GEOSPATIAL_CN_PRODUCT_METADATA_H

#include "geospatial/products/product_adapters.h"

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::geo
{

/// One band entry of a CN band-role table (data/products/band_roles/*.json).
struct CnBandSpec
{
    std::string band;          ///< native band id ("B1", "MS1", ...)
    std::string role;          ///< ADR 0065 vocabulary; "unknown" when unmappable
    std::string roleReason;    ///< why role is "unknown" ("" when mapped)
    bool hasWavelength = false;
    double wavelengthNm = 0.0; ///< documented centre wavelength when known
};

/// Band-role table for one satellite sensor layout.
struct CnBandRoleTable
{
    std::string sensorKey;     ///< "gf1_pms", "gf6_wfv", "zy3_nad_ms", ...
    std::string satellite;     ///< "GF1", "ZY3", "HJ1A" ...
    std::string sensorMode;    ///< "PMS", "WFV", "NAD", "CCD", ...
    std::string source;        ///< provenance note carried by the JSON
    std::vector<CnBandSpec> bands;
};

/// Resolves the data/products/band_roles directory the same way the
/// processing layer resolves data/ paths (SICNU_DATA_DIR → walk-up →
/// SICNU_SOURCE_DIR). Empty when not found (callers fail closed).
std::string cnBandRoleTableDir();

/// Loads the band-role table for a sensor key. Throws GeoError(OpenFailed)
/// when the table directory/file is missing and GeoError(InvalidArgument)
/// when the JSON does not declare the requested sensor — both diagnosable,
/// neither guesses.
CnBandRoleTable cnBandRoleTable( const std::string &sensorKey );

/// Result of identifying a path against the supported CN product families.
struct CnProductIdentity
{
    bool recognized = false;      ///< path names a KNOWN CN satellite family
    bool supported = false;       ///< and the sensor is one we adapt
    std::string kindName;         ///< product kind name when supported
    std::string satellite;        ///< "GF1", "GF2", "GF6", "ZY3", "HJ1A", "HJ1B"
    std::string sensorMode;       ///< "PMS1", "WFV2", "TLC", "NAD", "CCD1", ...
    std::string sensorKey;        ///< band-role table key when supported
    std::string reason;           ///< human-readable diagnosis when unsupported
};

/// Cheap filename/shape-based identification of a CN product path. Never
/// throws, never opens files. Returns supported=false with a concrete reason
/// for CN-family paths we deliberately do not adapt (GF-3 SAR, GF-4/5/7,
/// ZY-1/5-1, CBERS, ...), so callers can refuse diagnosably instead of
/// falling back to GenericRaster.
CnProductIdentity cnIdentifyProduct( const std::string &path );

/// Reads product metadata from a GF-1/2/6, ZY-3 or HJ-1 L1A sidecar XML (or
/// the image/directory that carries one beside it). Throws GeoError when the
/// sidecar cannot be located/read — never fabricates a record.
ProductMetadata readCnProductMetadata( const std::string &path,
                                       const CnProductIdentity &identity );

/// Locate the L1A sidecar XML for a CN product path (file, image or product
/// directory). Returns "" when nothing matches (bounded directory listing;
/// multispectral sidecar preferred for PMS directories).
std::string cnLocateSidecarXml( const std::string &path );

/// Locate the measurement TIFF for a CN product path (file, image or product
/// directory). Prefers the TIFF sharing the sidecar's stem (PMS directories
/// carry one MSS and one PAN pair — directory order must never decide which
/// image belongs to which sidecar); falls back to the only/*.tif sibling.
/// Returns "" when absent.
std::string cnLocateImageTiff( const std::string &path,
                               const std::string &sidecarPath = std::string() );

/// Sensor key (band-role table key) for a supported identity. Prefers the
/// declared band count of the parsed metadata (NAD pan vs MS share a sensor
/// id; PMS pan vs MSS share BandID letters); falls back to the
/// identity-derived key. Never returns "" for supported identities.
std::string cnSensorKey( const CnProductIdentity &identity,
                         const ProductMetadata &metadata );

} // namespace sicnu::geo

#endif // SICNU_GEOSPATIAL_CN_PRODUCT_METADATA_H
