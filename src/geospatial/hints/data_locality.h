/***************************************************************************
  geospatial/hints/data_locality.h
  Cloud-Native Geospatial Data Fabric 9.0 (M8) — data locality & execution
  hints.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  Pure, additive CONTRACT data: what an execution plane may consider when
  scheduling work over a dataset (local vs remote, estimated bytes, preferred
  chunk/window shape, cacheability, seek cost, COG/multidim readiness).

  This module creates NO scheduler, NO thread, NO queue — it is a function
  from (path, budget) to a JSON-stable hint record, synthesized from the
  inspection/probe surfaces this module already owns. Consumers read the
  facts and decide; nothing here enforces anything, and absent facts are
  honestly absent (a hint is never a guess dressed as a measurement).
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_DATA_LOCALITY_H
#define SICNU_GEOSPATIAL_DATA_LOCALITY_H

#include "geospatial/common.h"

#include <json/json.h>

#include <cstdint>
#include <string>

namespace sicnu::geo
{

struct DataLocalityOptions
{
    /// Remote identity probe budget (bytes; 0 disables the remote probe —
    /// hints then carry no reachability facts instead of stale ones).
    std::uintmax_t remoteProbeBytes = 1024;
    int timeoutSeconds = 10;
    int connectTimeoutSeconds = 5;
    int maxRetries = 1;
};

/// One dataset's locality hints (JSON-stable; absent facts are omitted,
/// never defaulted to plausible-looking zeros).
struct DataLocalityHints
{
    std::string path;
    std::string kind;             ///< "raster" | "vector" | "multidim" | "remote" | "unknown"
    std::string locality;         ///< "local" | "remote" | "unknown"

    bool hasEstimatedBytes = false;
    std::uint64_t estimatedBytes = 0;   ///< size in bytes when knowable

    /// Access-cost facts.
    bool seekCostLow = false;           ///< true for tiled/blocked/COG layouts
    bool cogOptimized = false;          ///< COG driver claims the layout, or a
                                        ///< tiled GTiff (COG-shaped heuristic —
                                        ///< the driver claim and the shape guess
                                        ///< are deliberately not distinguished
                                        ///< at this API; see data_locality.cpp)
    std::string compression;            ///< declared codec ("" undeclared)

    /// Preferred read shape: block size for raster tiles, chunk shape for
    /// multidim variables (slowest→fastest); empty when undeclared.
    std::vector<std::int64_t> preferredChunkShape;

    /// Cacheability: a provable identity token (see identity/asset_identity)
    /// — "" means NOT cacheable (fail-closed, same token contract as the
    /// execution fingerprint).
    std::string identityToken;
    std::string identityStrength;       ///< "content" | "etag" | "metadata" | ""

    /// Remote reachability facts (only when probed).
    bool remoteProbed = false;
    bool remoteAcceptsRanges = false;

    Json::Value toJson() const;
};

/// Synthesizes hints for any resource spelling. Read-only: one local inspect
/// or one bounded remote probe; never reads pixel/feature data beyond the
/// bounded statistics-free inspection contract.
DataLocalityHints dataLocalityHintsFor( const std::string &path,
                                        const DataLocalityOptions &options = {} );

} // namespace sicnu::geo

#endif // SICNU_GEOSPATIAL_DATA_LOCALITY_H
