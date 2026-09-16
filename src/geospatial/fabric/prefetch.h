/***************************************************************************
  geospatial/fabric/prefetch.h
  Cloud-Native Data Fabric / Data Cube 10.0 — bounded cache prefetch.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  Walks a chunk plan and warms the range cache: every chunk's source window
  is read through the /vsirangecache/ spelling so the cache pulls (and
  keeps) the blocks a later real read will need. Prefetch owns NO cache and
  NO fetcher — it is a driver over the existing RemoteRangeCache + Raster-
  Reader stack.

  Bounds: maxBytes caps the bytes PREFETCHED (per-run, measured as the
  cache's bytesFetched delta, not a guess); cancel is polled between
  chunks. A slow or range-ignoring origin is the cache's problem (typed
  errors / fallback semantics unchanged); prefetch reports per-chunk
  outcomes and never masks them.
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_FABRIC_PREFETCH_H
#define SICNU_GEOSPATIAL_FABRIC_PREFETCH_H

#include "geospatial/common.h"
#include "geospatial/fabric/query_planner.h"

#include <json/json.h>

#include <cstdint>
#include <string>
#include <vector>

namespace sicnu::geo
{

struct PrefetchOptions
{
    /// Total bytes this run may pull from origins (measured via the cache
    /// telemetry delta — a process-global counter, so concurrent in-process
    /// cache users inflate it; run exclusively for exact budgets).
    /// 0 = the chunk plan's own byte estimate (bounded by default); a plan
    /// without byte facts yields a zero estimate and the budget does not
    /// engage (nothing to bound with — stated, not hidden).
    std::uint64_t maxBytes = 0;
    /// Per-chunk read budget (window bytes).
    std::size_t maxChunkBytes = 16ull * 1024 * 1024;
    /// Chunks materialized per internal window (bounded memory).
    std::size_t chunkWindow = 64;
    /// Mirror directory (fabric/mirror): when set, chunks that hit the
    /// mirror are skipped (already local) — a mirror pass doubles as a
    /// prefetch pass for everything else.
    std::string mirrorDirectory;
};

struct PrefetchChunkOutcome
{
    std::uint64_t index = 0;
    /// "warmed" | "cache-hit" | "mirror-hit" | "skipped-budget" |
    /// "skipped-cancelled" | "failed"
    std::string status;
    std::uint64_t bytesPulled = 0;   ///< origin bytes this chunk fetched
    std::string errorText;           ///< when failed
    std::string assetIdHint;
};

struct PrefetchReport
{
    std::vector<PrefetchChunkOutcome> chunks;    ///< bounded by chunk plan
    std::uint64_t bytesPulled = 0;               ///< total origin bytes
    std::uint64_t warmed = 0, cacheHits = 0, mirrorHits = 0;
    std::uint64_t skippedBudget = 0, skippedCancel = 0, failed = 0;
    bool budgetExhausted = false;
    std::uint64_t outcomesDropped = 0;   ///< per-chunk outcomes past the
                                         ///< retained window (counters only)

    Json::Value toJson() const;
};

/// Warms the range cache for the plan's chunks. Requires the range cache
/// to be installed (GeoError(InvalidArgument) otherwise — prefetching
/// without a cache is a no-op masquerading as work). Requires the plan to
/// be an EO cube plan (multidim stores are local; prefetch is about remote
/// windows — GeoError(InvalidArgument) for non-EO plans).
PrefetchReport prefetchChunks( const FabricPlan &plan, const PrefetchOptions &options = {},
                               const CancelToken &cancel = {} );

/// Same walk over a directly-constructed chunk plan + cube (the operator
/// surface uses this form; the planner form above forwards here).
PrefetchReport prefetchChunks( const VirtualCube &cube, const CubeChunkPlan &plan,
                               const PrefetchOptions &options = {},
                               const CancelToken &cancel = {} );

// --- 11.0 (WP F, DECISIONS D-1107): access-pattern-driven prefetch ----------

/// One grid window of a declared access pattern (grid pixel coordinates —
/// the same space readWindow addresses).
struct AccessWindow
{
    int x = 0, y = 0, w = 0, h = 0;
};

/// Access-pattern-driven prefetch: the caller declares the window SEQUENCE
/// it is about to read (a viewer trajectory, a tile queue, a model's scan
/// order); the planner maps each window onto the intersecting assets'
/// source pixels, MERGES overlapping/adjacent reads per asset, orders the
/// merged reads by locality (asset selection order, then ascending y/x —
/// sequential bytes on object storage), skips mirror hits, and warms the
/// cache under budget/cancel. Report semantics as prefetchChunks, with
/// `mergedReads` naming how many reads the merge produced.
struct PrefetchLocalityReport
{
    std::uint64_t mergedReads = 0;      ///< reads after per-asset merging
    std::uint64_t declaredWindows = 0;  ///< access pattern size (input)
    std::uint64_t bytesPulled = 0;
    std::uint64_t warmed = 0, cacheHits = 0, mirrorHits = 0;
    std::uint64_t skippedBudget = 0, skippedCancel = 0, failed = 0;
    bool budgetExhausted = false;
    std::uint64_t outcomesDropped = 0;

    Json::Value toJson() const;
};

/// Requires the range cache installed and a valid cube grid (typed
/// refusals as prefetchChunks). Memory is O(merged reads) with the
/// outcome window capped at 1024 (D-1008 doctrine).
PrefetchLocalityReport prefetchAccessPattern( const VirtualCube &cube,
                                              const std::vector<AccessWindow> &pattern,
                                              const PrefetchOptions &options = {},
                                              const CancelToken &cancel = {} );

} // namespace sicnu::geo

#endif // SICNU_GEOSPATIAL_FABRIC_PREFETCH_H
