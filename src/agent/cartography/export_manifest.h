// src/agent/cartography/export_manifest.h
#pragma once

//
// Export manifest (Cartography Production 11.0).
//
// A governed export has always been atomic (temp → verify → sha256 →
// rename), but the evidence never outlived the call: the structural digest
// stamped at compose time was returned to the caller and then lost, and an
// atlas/series delivery had no machine-checkable record of WHICH files
// belong together. The manifest is the durable half of the delivery
// contract, written NEXT TO the artifact(s) as `<base>.<format>.manifest.json`:
//
//   { kind: "cartography_export_manifest", manifest_version: 1,
//     layout_name, format, dpi, page_count,
//     provenance:  {template?, template_provenance?, components[]},
//     structural_digest,                       // compose-time geometry pin
//     pages: [{file_name, sha256, bytes, feature_id?, label?, extent?}],
//     manifest_digest,                         // SHA-256 over the payload
//                                              // MINUS the environment block
//     environment: {qt_runtime, qgis_version, os, font_substitutions} }
//
// Determinism contract: `manifest_digest` covers every payload member with
// a stable canonical serialization. The `environment` block is deliberately
// EXCLUDED — it documents the producing host honestly (Qt/QGIS/OS versions,
// observed font substitutions) and must never make two byte-identical
// deliveries from different hosts look different.
//
// Every page entry can be re-verified from disk alone: hash the file, walk
// the pages array, compare. writeExportManifest is atomic (temp + rename)
// like the artifacts it describes; a manifest is only valid once its
// referenced artifacts are final.
//

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::agent::cartography {

inline constexpr int kExportManifestVersion = 1;
/// Production hard bound (DECISIONS D-013): a single manifest describes at
/// most this many pages — the same clamp the series planner and the atlas
/// exporter enforce, so a runaway delivery fails at the producer, not in a
/// multi-gigabyte JSON file.
inline constexpr int kMaxManifestPages = 512;

/// One delivered file. `file_name` is a bare name inside the manifest's
/// directory; `feature_id`/`label`/`extent` are present for atlas pages
/// only (extent in the coverage layer's CRS with its authid).
struct ExportManifestPage
{
    std::string file_name;
    std::string sha256;
    long long bytes = 0;
    std::string feature_id;   ///< atlas: stringified feature id; empty otherwise
    std::string label;        ///< atlas: atlas currentLabel() when non-empty
    Json::Value extent;       ///< atlas: {x_min,y_min,x_max,y_max,crs} | null
};

struct ExportManifest
{
    int manifest_version = kExportManifestVersion;
    std::string layout_name;
    std::string format;       ///< "png" | "pdf" | "svg"
    double dpi = 300.0;
    Json::Value provenance;   ///< compose-provenance block (template/components)
    std::string structural_digest;
    std::vector<ExportManifestPage> pages;
    Json::Value environment;  ///< honest host description; never digested

    /// True when the manifest carries no page entries (a failed delivery
    /// never produces a manifest; an empty one is a caller bug).
    bool empty() const { return pages.empty(); }

    /// Canonical payload JSON (everything except `environment`), members in
    /// the fixed order above. Deterministic across runs and hosts.
    Json::Value payloadToJson() const;

    /// SHA-256 over the canonical payload serialization (hex, lowercase).
    std::string digest() const;

    /// Full document: payload + manifest_digest + environment.
    Json::Value toJson() const;
};

/// Parses and structurally validates a manifest document (kind, version,
/// page shapes, bare file names, hex digests). Returns the problems; empty
/// means the document has manifest shape. Does NOT touch the filesystem.
std::vector<std::string> validateExportManifest( const Json::Value &document );

/// Recomputes `manifest_digest` from the payload members of `document` and
/// compares. False on shape-invalid documents (validate first for detail).
bool verifyExportManifestDigest( const Json::Value &document );

/// Atomic sidecar write: `<directory>/<base_name>.<format>.manifest.json`
/// is produced via temp file + rename inside `directory`. Returns false
/// with *error on any failure; the sidecar path mirrors the artifact base
/// so N artifacts of one delivery live and die together.
bool writeExportManifest( const std::string &directory, const std::string &base_name,
                          const std::string &format, const ExportManifest &manifest,
                          std::string *error = nullptr );

/// Reads and digest-verifies a manifest from
/// `<directory>/<base_name>.<format>.manifest.json`. On success returns the
/// document and sets *digest_ok (false = parsed but payload/digest mismatch
/// — tampered or hand-edited). On read/parse failure returns a null Json
/// value and sets *error.
Json::Value readExportManifest( const std::string &directory, const std::string &base_name,
                                const std::string &format, bool *digest_ok = nullptr,
                                std::string *error = nullptr );

} // namespace sicnu::agent::cartography
