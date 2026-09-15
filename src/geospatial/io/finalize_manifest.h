/***************************************************************************
  geospatial/io/finalize_manifest.h
  Geospatial I/O, COG & Interchange 11.0 — finalize manifest (provenance
  sidecar) for atomically published datasets.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  Contract:
  * A finalize manifest records WHAT was published and its content digest:
    schema version, producer, driver, shape/dtype/CRS, creation options,
    dataset SHA-256 (streamed, O(1) memory), byte size, UTC stamp.
  * The manifest is a sibling sidecar "<name>.sicnu-manifest.json". It is
    written BEFORE the main file publishes (atomic_fs publishes sidecars
    first, main last), so a published dataset never lacks its manifest; a
    crash mid-transaction leaves staging + manifest as sweepable orphans.
  * verifyDataset() is the independent integrity gate: it recomputes the
    digest from disk and re-checks the declared shape with the canonical
    metadata inspector. A missing manifest is a FAILED verification
    (fail-closed) — callers who deal with legacy files must say so via
    allowMissingManifest and get the absence reported either way.
  * Digests use sicnu::geo::Sha256 (the platform's only hash authority).
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_IO_FINALIZE_MANIFEST_H
#define SICNU_GEOSPATIAL_IO_FINALIZE_MANIFEST_H

#include "geospatial/common.h"

#include <string>
#include <vector>

namespace sicnu::geo::io
{

/// Sidecar suffix appended to the dataset main file ("x.tif" →
/// "x.tif.sicnu-manifest.json").
extern const char *const kFinalizeManifestSuffix;

/// Declared facts captured at finalize time. Shape fields are optional
/// (0/empty = not applicable, e.g. vector datasets).
struct FinalizeManifestFields
{
    std::string producer;                     ///< "RasterWriter", "io:make_cog", ...
    std::string driver;                       ///< GDAL short name ("GTiff", "GPKG", ...)
    int width = 0;
    int height = 0;
    int bandCount = 0;
    std::string dtype;
    std::string crsAuthid;
    std::vector<std::string> creationOptions;
};

/// Streaming SHA-256 of a file's bytes (1 MiB chunks). Throws
/// GeoError(IoError) when the file cannot be read.
std::string datasetSha256Hex( const std::string &path );

/// Path of the manifest sidecar for a dataset main file.
std::string finalizeManifestPath( const std::string &mainPath );

/// Builds the manifest JSON for `mainPath` (digest and size are computed
/// from the file on disk NOW — call while the bytes to be published are in
/// place, i.e. on the staged file before publish; the digest survives the
/// rename unchanged). Throws GeoError(IoError) when the file is unreadable.
Json::Value buildFinalizeManifest( const std::string &mainPath, const FinalizeManifestFields &fields );

/// Writes the manifest next to `mainPath` through writeFileAtomic (staged →
/// fsync → publish). Used for already-published datasets and by callers that
/// manage the staged manifest themselves before a group publish.
void writeFinalizeManifest( const std::string &mainPath, const Json::Value &manifest );

/// Reads + parses the manifest of `mainPath`. GeoError(NotFound) when no
/// sidecar exists, GeoError(InvalidMetadata) when it exists but does not
/// parse or carries a foreign schema version.
Json::Value readFinalizeManifest( const std::string &mainPath );

/// Verification issue codes:
///   main_file_missing, manifest_missing, manifest_invalid,
///   digest_mismatch, shape_mismatch, inspect_unavailable
/// (inspect_unavailable is advisory: shape re-check needs a readable dataset;
/// when inspection itself fails the digest verdict still stands).
struct ManifestIssue
{
    std::string code;
    std::string message;
};

struct ManifestVerifyReport
{
    bool verified = false;        ///< true only when zero fatal issues
    bool digestMatched = false;
    bool manifestPresent = false;
    std::string datasetSha256;    ///< recomputed from disk
    std::string displayPath;      ///< redacted display form of mainPath
    std::vector<ManifestIssue> issues;
    Json::Value toJson() const;
};

/// Independent integrity check: manifest parses, digest recomputed from disk
/// matches, declared shape matches a fresh canonical inspection (when the
/// dataset can be inspected at all). Read-only; throws nothing that the
/// report does not already carry.
ManifestVerifyReport verifyDataset( const std::string &mainPath, bool allowMissingManifest = false );

} // namespace sicnu::geo::io

#endif // SICNU_GEOSPATIAL_IO_FINALIZE_MANIFEST_H
