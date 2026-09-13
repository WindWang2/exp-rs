/***************************************************************************
  geospatial/fabric/query_planner.h
  Cloud-Native Data Fabric / Data Cube 10.0 — bounded, inspectable, executable
  query plans.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  The planner is the fabric's front door. It turns one declarative intent —
  "this catalog, filtered like this, gridded like that, chunked so, within
  this execution budget" — into a FabricPlan whose stages are inspectable
  JSON and whose cost hints (scenes, chunks, estimated bytes, estimated
  memory, estimated remote calls, identity cacheability) are computed from
  declared facts and bounded probes, never from a full crawl.

  THE MEMORY CONTRACT (DECISIONS D-1009): the catalog_query stage consumes
  the catalog page by page and keeps only the SELECTED assets (at most
  intent.sceneBudget) plus constant-size statistics. Peak memory over the
  catalog stage is O(page + selected), not O(catalog) — asserted at the
  100k-record scale by a test that measures process RSS.

  Execution is bounded and cooperative: every stage boundary and every
  chunk read polls the CancelToken; every byte read passes through the
  execution budget (breach → GeoError(ResourceExhausted)); per-asset
  failures degrade to provenance records (same doctrine as the virtual
  cube) so one dead scene cannot void a plan.

  The planner owns NO scheduler and NO thread: planFabric/executeFabric are
  synchronous functions on the caller's thread (UI callers are already
  off-thread by house contract). It owns NO algorithm: reads go through
  RasterReader / the range cache, identity through asset_identity, queries
  through CatalogService — all existing authorities.
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_FABRIC_QUERY_PLANNER_H
#define SICNU_GEOSPATIAL_FABRIC_QUERY_PLANNER_H

#include "geospatial/common.h"
#include "geospatial/fabric/catalog_service.h"
#include "geospatial/fabric/chunk_plan.h"
#include "geospatial/fabric/virtual_cube.h"

#include <json/json.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace sicnu::geo
{

/// The declarative input. Either a catalog URI (local tree or STAC API
/// root) OR an in-memory record set — at least one must be present.
struct FabricIntent
{
    /// Catalog source ("" when records are given directly).
    std::string catalogUri;
    /// In-memory alternative to catalogUri (planner runs the records
    /// backend — same filters, same selection policy).
    std::vector<AssetRecord> records;

    CatalogQuery query;             ///< filter vocabulary (validated up front)
    /// Maximum selected scenes (the plan's O(selected) size bound).
    int sceneBudget = 64;
    /// Explicit target grid; empty → negotiated from the selected assets
    /// (same rules as VirtualCube::build).
    VirtualCubeGrid grid;
    CubeChunkShape chunkShape;
    CubeSlice slice;
    /// Optional concrete window (grid pixel coordinates). When set, the
    /// plan is executable as ONE window read (executeWindow); when empty,
    /// execution means the chunk walk (executeChunks).
    bool hasWindow = false;
    int windowX = 0, windowY = 0, windowW = 0, windowH = 0;
    /// Total byte budget across ALL origin reads of one execution.
    std::uint64_t executionBudgetBytes = 256ull * 1024 * 1024;

    void validate() const;          ///< throws GeoError(InvalidArgument)
};

/// Cost hints over declared facts + bounded probes. Every counter is
/// honest: absent facts stay 0 and the *_unknown flags say why.
struct FabricPlanCost
{
    std::uint64_t catalogMatches = 0;          ///< matched before selection
    bool catalogMatchesTruncated = false;      ///< cap stopped the count
    std::uint64_t scenes = 0;                  ///< selected assets
    std::uint64_t chunks = 0;                  ///< post-slice logical chunks
    std::uint64_t estimatedBytes = 0;          ///< window or full-chunk bytes
    std::uint64_t estimatedMemoryBytes = 0;    ///< peak in-memory footprint
    std::uint64_t estimatedRemoteCalls = 0;    ///< origin fetches (miss estimate)
    std::uint64_t cacheableAssets = 0;         ///< provable identity tokens
    std::uint64_t unprovableIdentityAssets = 0;///< "" tokens (never cached)
    std::uint64_t undatedScenes = 0;           ///< no resolvable instant
    bool bytesUnknown = false;                 ///< no byte facts on any asset

    Json::Value toJson() const;
};

/// One plan stage (planner bookkeeping — inspectable, JSON-stable names).
struct FabricPlanStage
{
    std::string name;        ///< "catalog_query" | "asset_selection" |
                             ///< "grid_planning" | "chunk_planning" |
                             ///< "identity_cache" 
    std::string status;      ///< "planned" | "skipped" | "estimated"
    std::uint64_t inputs = 0;
    std::uint64_t outputs = 0;
    Json::Value details;     ///< stage-specific bounded facts

    Json::Value toJson() const;
};

struct FabricPlanOptions;

class FabricPlan
{
  public:
    /// Inspectable description (stable keys; display-redacted).
    Json::Value toJson() const;

    const std::vector<FabricPlanStage> &stages() const { return mStages; }
    /// The intent the plan was built from (execution budget + window live
    /// here; execution reads them).
    const FabricIntent &intent() const { return mIntent; }
    const FabricPlanCost &cost() const { return mCost; }
    const std::vector<AssetRecord> &selectedAssets() const { return mSelected; }
    const VirtualCubeGrid &grid() const { return mGrid; }
    /// The chunk plan (present when chunk_planning ran; empty otherwise).
    const CubeChunkPlan &chunkPlan() const { return mChunkPlan; }
    bool isWindowPlan() const { return mWindowPlan; }

  private:
    friend FabricPlan planFabric( const FabricIntent &intent,
                                  const FabricPlanOptions &options,
                                  const CancelToken &cancel );
    FabricPlan() = default;

    std::vector<FabricPlanStage> mStages;
    FabricPlanCost mCost;
    std::vector<AssetRecord> mSelected;
    VirtualCubeGrid mGrid;
    CubeChunkPlan mChunkPlan;
    bool mWindowPlan = false;
    FabricIntent mIntent;        ///< kept for execution (budget + window)
};

/// Planner options (probe bounds; time budgets ride on CatalogService).
struct FabricPlanOptions
{
    CatalogServiceOptions catalog;   ///< walk/network bounds
    int gridProbeLimit = 256;        ///< metadata opens for grid negotiation
    int identityProbeLimit = 64;     ///< identity probes for cacheability
                                     ///< cost facts (assets beyond stay
                                     ///< uncounted — the estimate says so)
};

/// Runs the plan stages through chunk_planning. Throws GeoError for
/// invalid intents, offline remote catalogs (typed refusal), cancel during
/// crawl, and ResourceExhausted when the catalog exceeds declared bounds.
FabricPlan planFabric( const FabricIntent &intent, const FabricPlanOptions &options = {},
                       const CancelToken &cancel = {} );

/// The ONE JSON → intent parser (operator and CLI surfaces share it; no
/// second parser may appear). Accepts the intent vocabulary:
///   catalog, sceneBudget, executionBudgetBytes,
///   bounds[minX,minY,maxX,maxY] | query{...CatalogQuery fields...},
///   grid{crs,scaleX,scaleY,extent[4]}, chunkShape{time,y,x,band},
///   slice{timeStartUtc,timeEndUtc,extent[4],bandRoles[]},
///   window{x,y,w,h}
/// Throws GeoError(InvalidArgument) for structural violations (typed, and
/// the message names the field).
FabricIntent fabricIntentFromJson( const Json::Value &json );

/// Execution report (bounded; per-asset truth from the virtual cube).
struct FabricExecutionReport
{
    std::uint64_t bytesRead = 0;               ///< origin/cache bytes served
    std::uint64_t assetsConsulted = 0;
    std::uint64_t assetsFailed = 0;
    std::uint64_t chunksExecuted = 0;
    bool budgetBreached = false;               ///< execution stopped on budget
    Json::Value toJson() const;
};

/// Executes a WINDOW plan: one bounded window read over the planned cube.
/// Throws GeoError(Cancelled/ResourceExhausted); asset-level failures land
/// in the result's provenance (degrade doctrine).
VirtualCubeWindowResult executeWindow( const FabricPlan &plan,
                                       const VirtualCubeReadOptions &options,
                                       FabricExecutionReport &report,
                                       const CancelToken &cancel = {} );

/// Per-chunk outcome of a chunk-walk execution.
struct FabricChunkOutcome
{
    std::uint64_t index = 0;
    bool ok = false;
    bool skippedBudget = false;    ///< not attempted: budget exhausted
    bool skippedCancel = false;
    std::string errorText;         ///< typed text when !ok
    std::uint64_t bytesRead = 0;
    std::string assetIdHint;
};

/// Executes a CHUNK plan: materializes chunks in bounded windows, reads
/// each through the virtual cube (or the multidim reader), hands every
/// outcome to the sink. Stops (typed GeoError) on cancel; budget breaches
/// flip skippedBudget for the remainder and REPORT (not throw) — a
/// partial execution is a reportable outcome, not a crash.
std::vector<FabricChunkOutcome> executeChunks( const FabricPlan &plan,
                                               const VirtualCubeReadOptions &options,
                                               std::size_t chunkWindow,   ///< ≤ 4096
                                               const std::function<void( const CubeChunkRequest &,
                                                                         const VirtualCubeWindowResult & )> &sink,
                                               FabricExecutionReport &report,
                                               const CancelToken &cancel = {} );

} // namespace sicnu::geo

#endif // SICNU_GEOSPATIAL_FABRIC_QUERY_PLANNER_H
