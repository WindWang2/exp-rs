/***************************************************************************
  geospatial/fabric/mirror.h
  Cloud-Native Data Fabric / Data Cube 10.0 — explicit offline mirror.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  The range cache is NOT an offline mirror (its header says so): entries
  are transient, policy-evicted, and invisible to later processes. The
  mirror is the explicit, durable counterpart: a caller-directed pass that
  MATERIALIZES a chunk plan's source windows into a plain directory (one
  GeoTIFF per chunk, atomically published) plus a manifest JSON whose keys
  are identity tokens.

  Fail-closed (DECISIONS D-1010): an asset without a provable identity
  token is never mirrored — the mirror's whole value is that a later
  process can PROVE a hit is the same bytes. Path keys would be a lie.

  Layout (all paths inside the mirror directory):
    mirror/
      manifest.json          — entries: token → {file, bytes, window, time}
      chunks/<sha16>.tif     — chunk payload (GeoTIFF, atomic publish)

  Concurrency: single-writer per mirror directory (an O_EXCL lock file
  guards the manifest update); readers are lock-free (publish is atomic).
  Multi-process writers to ONE directory are refused via the lock —
  second-instance semantics, not a distributed lock (DECISIONS D-1014).

  Offline replay: reads consult the mirror by (token, chunk key) BEFORE
  the network (VirtualCubeReadOptions.mirrorDirectory). An offline miss is
  the offline gate's typed refusal — the mirror never guesses.
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_FABRIC_MIRROR_H
#define SICNU_GEOSPATIAL_FABRIC_MIRROR_H

#include "geospatial/common.h"
#include "geospatial/fabric/chunk_plan.h"
#include "geospatial/fabric/query_planner.h"
#include "geospatial/fabric/virtual_cube.h"

#include <json/json.h>

#include <cstdint>
#include <string>
#include <vector>

namespace sicnu::geo
{

struct MirrorOptions
{
    /// The mirror directory (created on demand; must be empty-or-valid).
    std::string mirrorDirectory;
    /// Total DECLARED chunk bytes this pass may materialize (the plan's
    /// estimates — real GeoTIFF files run larger; bytesWritten reports the
    /// file truth). 0 = the plan's estimatedBytes; a plan WITHOUT byte
    /// facts (unknown dtypes) yields a zero estimate and the budget does
    /// not engage (nothing to bound with — stated, not hidden).
    std::uint64_t maxBytes = 0;
    /// Chunks materialized per internal window (bounded memory).
    std::size_t chunkWindow = 64;
    /// GeoTIFF creation: block size for mirrored chunks (tiles 256×256).
    int blockSize = 256;
    /// Per-chunk read budget through the source reader (11.0; the previous
    /// hard-coded 256 MiB — same default, now declared).
    std::size_t maxChunkReadBytes = 256ull * 1024 * 1024;
};

struct MirrorChunkOutcome
{
    std::uint64_t index = 0;
    /// "mirrored" | "already-present" | "skipped-unprovable-identity" |
    /// "skipped-budget" | "skipped-cancelled" | "failed"
    std::string status;
    std::string file;                 ///< written chunk file ("" when skipped)
    std::uint64_t bytes = 0;
    std::string token;                ///< identity token used as the key
    std::string chunkKey;             ///< token + geometry hash (see below)
    std::string errorText;
    std::string assetIdHint;
};

struct MirrorReport
{
    std::vector<MirrorChunkOutcome> chunks;
    /// 11.0: outcomes past the retained window are dropped from the vector
    /// and counted (bounded report — D-1008 doctrine, same as prefetch).
    std::uint64_t outcomesDropped = 0;
    std::uint64_t bytesWritten = 0;
    std::uint64_t mirrored = 0, alreadyPresent = 0, skippedUnprovable = 0;
    std::uint64_t skippedBudget = 0, skippedCancel = 0, failed = 0;
    bool budgetStopped = false;   ///< the pass ended on the byte budget
    bool cancelled = false;       ///< the pass ended on cancel

    Json::Value toJson() const;
};

/// The chunk mirror key: sha256 over (identity token, source asset path
/// identity, source window geometry, band selection) — stable across
/// processes and runs; hex, truncated to 32 chars for a filesystem-safe
/// name (full hash in the manifest).
std::string fabricChunkMirrorKey( const std::string &identityToken,
                                  const std::string &assetPath,
                                  const RasterWindow &sourceWindow,
                                  const std::string &bandSelector );

/// Materializes the plan's chunks into the mirror. Requires the plan to be
/// an EO cube plan (GeoError(InvalidArgument) otherwise). Every mirrored
/// chunk is read ONCE through the normal read path (range cache semantics
/// intact) and written through atomic_fs publish; the manifest is updated
/// after each chunk (a crashed pass leaves earlier chunks valid).
MirrorReport mirrorChunks( const FabricPlan &plan, const MirrorOptions &options = {},
                           const CancelToken &cancel = {} );

/// Same walk over a directly-constructed chunk plan + cube.
MirrorReport mirrorChunks( const VirtualCube &cube, const CubeChunkPlan &plan,
                           const MirrorOptions &options = {},
                           const CancelToken &cancel = {} );

/// Mirror lookup: returns the local chunk file path for (token, chunkKey),
/// or "" on a miss. Total function: a corrupt manifest line skips that
/// entry (typed accumulation in `skippedCorrupt`, never a throw).
std::string resolveMirrorHit( const std::string &mirrorDirectory, const std::string &token,
                              const std::string &chunkKey, std::string *skippedCorrupt = nullptr );

// --- 11.0 offline replay (DECISIONS D-1103) --------------------------------
//
// A mirror is only worth its name when a LATER PROCESS can resolve a hit
// with ZERO network probing. The v2 manifest therefore carries an INDEX
// keyed by each asset's credential-free canonical key (object-store
// canonical key, credential-stripped identity URL, or canonical local
// path) → {token, assetId, grid facts, written time}. Materialization
// fills it; the lookups below read it — all pure local work.

/// The index key of an asset (pure string work; never a credential):
/// object-store spellings canonicalize through the profile table, http(s)
/// URLs through the credential-stripped identity URL, local paths through
/// ResourceUri canonicalization.
std::string fabricMirrorIndexKey( const std::string &assetPath );

/// Per-asset facts recorded in the offline index (grid facts make OFFLINE
/// cube builds and window mapping possible without opening the asset).
struct MirrorIndexAssetFacts
{
    bool found = false;
    std::string token;          ///< "" when the asset mirrored unprovable
    std::string assetId;
    bool hasGrid = false;
    int rasterWidth = 0, rasterHeight = 0;
    double resX = 0.0, resY = 0.0;      ///< geotransform-derived scales
    double assetMinX = 0.0, assetMinY = 0.0, assetMaxX = 0.0, assetMaxY = 0.0;
    std::string epsgAuthid;
    std::string writtenUtc;             ///< materialization time (expiry basis)
};

/// Looks an asset up in the mirror's offline index. Total function: found
/// = false covers absent/corrupt entries (corrupt text lands in
/// `skippedCorrupt` when provided) — never a throw.
bool lookupMirrorAsset( const std::string &mirrorDirectory, const std::string &assetPath,
                        MirrorIndexAssetFacts &facts, std::string *skippedCorrupt = nullptr );

/// Resolves a chunk artifact OFFLINE: index key of \a assetPath → token →
/// chunk key for (token, \a sourceWindow, \a bandSelector) → local file.
/// The v2 chunk key uses the index-key path basis; the 10.0 raw-path key
/// is tried as a fallback so v1 manifests keep resolving. \a maxAgeSeconds
/// > 0 expires chunks materialized longer ago (writtenUtc basis; an
/// expired hit reports expired=true with file=""). Never touches network.
struct MirrorArtifactHit
{
    bool hit = false;
    bool expired = false;
    std::string token;
    std::string chunkKey;
    std::string file;                   ///< local chunk path ("" when no hit)
    std::uint64_t bytes = 0;
    std::string skippedCorrupt;
};
MirrorArtifactHit resolveMirrorArtifact( const std::string &mirrorDirectory,
                                         const std::string &assetPath,
                                         const RasterWindow &sourceWindow,
                                         const std::string &bandSelector,
                                         std::uint64_t maxAgeSeconds = 0 );

/// Mirror stats from the manifest (entries, bytes; honest zeros when the
/// mirror directory does not exist — never a guessed inventory).
Json::Value mirrorStatsJson( const std::string &mirrorDirectory );

} // namespace sicnu::geo

#endif // SICNU_GEOSPATIAL_FABRIC_MIRROR_H
