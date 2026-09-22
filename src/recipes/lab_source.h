// src/recipes/lab_source.h
#pragma once

//
// RS14-20 lab input sources.
//
// `LabSource` abstracts "give me compilable lab documents": a directory of
// .lab.json/.labspec.json files, optionally reconciled through
// data/labs/lab-registry.json so that step-less canonical wrappers
// (lab12–14 on master) resolve to their D3 executable source while keeping
// the canonical id + v2 teaching metadata.
//

#include "recipes/lab_document.h"

#include <string>
#include <vector>

namespace sicnu::recipes {

/// One entry of a lab-directory scan.
struct LabSourceEntry
{
  std::string canonicalId;   ///< canonical lab id (registry-aware when possible)
  LabDocument document;      ///< normalized document (registry-merged when needed)
  std::string sourcePath;    ///< file the executable content came from
  bool resolvedViaRegistry = false; ///< true when a step-less wrapper was merged
};

/// Scan `labsDir` for `*.lab.json` (strict D2 set) and `*.labspec.json`
/// (D3 set). When `registryPath` names a readable `sicnu.lab-registry/1`
/// document, canonical entries whose .lab.json lacks steps are merged with
/// their D3 `source`; the D3 file is also skipped as a standalone entry so
/// no lab compiles twice.
///
/// Per-file failures never abort the scan — they land in `errors` with the
/// offending path. Deterministic: entries sorted by canonicalId.
std::vector<LabSourceEntry> loadLabDirectory( const std::string &labsDir,
                                              const std::string &registryPath,
                                              std::vector<LabDocumentError> &errors );

/// Default shipped lab dir resolution, same search policy as the harness
/// catalogs but Qt-free: $SICNU_LAB_DIR → <cwd>/data/labs →
/// SICNU_SOURCE_DIR/data/labs (compile define) → app-relative. Empty string
/// when nothing exists.
std::string defaultLabDirectory();

/// Non-cryptographic 64-bit FNV-1a fingerprint, hex. Used for source-drift
/// provenance (teaching_origin.source_fingerprint) — collision resistance is
/// not the requirement, deterministic change-detection is.
std::string fnv1a64Hex( const std::string &text );

} // namespace sicnu::recipes
