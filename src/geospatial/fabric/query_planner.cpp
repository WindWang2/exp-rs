/***************************************************************************
  geospatial/fabric/query_planner.cpp — bounded, inspectable, executable
  query plans.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#include "geospatial/fabric/query_planner.h"

#include "geospatial/fabric/object_store.h"
#include "geospatial/multidim/multidim_view.h"
#include "geospatial/identity/asset_identity.h"
#include "geospatial/raster/raster_reader.h"
#include "geospatial/util/time_normalization.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>

namespace sicnu::geo
{

namespace
{

/// D-1007 rank for streaming top-K selection. Comparing the tuple
/// (cloudCover declared, cloudCover, instant desc, id, input order) needs a
/// strict weak order over declared facts only.
struct SelectionRank
{
    bool hasCloud = false;
    double cloud = 0.0;
    std::int64_t instantNanos = 0;
    bool hasInstant = false;
    std::string id;
    std::uint64_t inputIndex = 0;
};

bool rankBetter( const SelectionRank &a, const SelectionRank &b )
{
    // Fewer clouds first; undeclared last.
    if ( a.hasCloud != b.hasCloud )
        return a.hasCloud;
    if ( a.hasCloud && a.cloud != b.cloud )
        return a.cloud < b.cloud;
    // Newest first; undated last.
    if ( a.hasInstant != b.hasInstant )
        return a.hasInstant;
    if ( a.hasInstant && a.instantNanos != b.instantNanos )
        return a.instantNanos > b.instantNanos;
    if ( a.id != b.id )
        return a.id < b.id;
    return a.inputIndex < b.inputIndex;   // total (input order breaks ties)
}

SelectionRank rankOf( const AssetRecord &record, std::uint64_t inputIndex )
{
    SelectionRank rank;
    rank.hasCloud = record.hasCloudCover;
    rank.cloud = record.cloudCover;
    rank.id = record.id;
    rank.inputIndex = inputIndex;
    if ( !record.datetimeUtc.empty() )
    {
        const InstantParse parse = parseIso8601Instant( record.datetimeUtc );
        if ( parse.ok )
        {
            rank.hasInstant = true;
            rank.instantNanos = parse.epochNanos;
        }
    }
    return rank;
}

/// Bounded top-K under the selection order: O(matches) streaming, O(K)
/// memory (D-1009).
class TopKSelector
{
  public:
    explicit TopKSelector( std::size_t k ) : mK( k ) {}

    void offer( AssetRecord record, std::uint64_t inputIndex )
    {
        SelectionRank rank = rankOf( record, inputIndex );
        if ( mSelected.size() < mK )
        {
            mSelected.push_back( { std::move( record ), std::move( rank ) } );
            std::push_heap( mSelected.begin(), mSelected.end(), &worseFirst );
            return;
        }
        if ( mK > 0 && rankBetter( rank, mSelected.front().rank ) )
        {
            std::pop_heap( mSelected.begin(), mSelected.end(), &worseFirst );
            mSelected.back() = { std::move( record ), std::move( rank ) };
            std::push_heap( mSelected.begin(), mSelected.end(), &worseFirst );
        }
    }

    /// Selected assets in BEST-first order (D-1006 selection order).
    std::vector<AssetRecord> take()
    {
        std::sort( mSelected.begin(), mSelected.end(),
                   [] ( const Entry &a, const Entry &b ) { return rankBetter( a.rank, b.rank ); } );
        std::vector<AssetRecord> result;
        result.reserve( mSelected.size() );
        for ( Entry &entry : mSelected )
            result.push_back( std::move( entry.record ) );
        return result;
    }

  private:
    struct Entry
    {
        AssetRecord record;
        SelectionRank rank;
    };
    // Heap predicate: "better" sorts as LESS, so the heap TOP is the
    // WORST kept element — a full selector replaces exactly that element.
    static bool worseFirst( const Entry &a, const Entry &b )
    {
        return rankBetter( a.rank, b.rank );
    }

    std::size_t mK;
    std::vector<Entry> mSelected;
};

} // namespace

void FabricIntent::validate() const
{
    // 11.0: three source shapes — catalog, in-memory records, or a multidim
    // store. A multidim source is exclusive with the other two (the store
    // replaces the catalog stage; its variable names the cube).
    if ( !multidimPath.empty() )
    {
        if ( !catalogUri.empty() || !records.empty() )
            throw GeoError( ErrorCode::InvalidArgument,
                            "fabric intent takes one source — multidimPath excludes "
                            "catalogUri/records" );
        if ( multidimVariable.empty() )
            throw GeoError( ErrorCode::InvalidArgument,
                            "multidim source needs a variable name" );
    }
    if ( multidimPath.empty() && catalogUri.empty() && records.empty() )
        throw GeoError( ErrorCode::InvalidArgument,
                        "fabric intent needs a catalogUri, in-memory records, or a "
                        "multidim store" );
    if ( !catalogUri.empty() && !records.empty() )
        throw GeoError( ErrorCode::InvalidArgument,
                        "fabric intent takes one source — catalogUri or records, not both" );
    if ( !multidimPath.empty() )
    {
        query.validate();   // filter vocabulary stays shape-valid even if unused
        if ( sceneBudget < 1 )
            throw GeoError( ErrorCode::InvalidArgument, "sceneBudget must be >= 1" );
        if ( executionBudgetBytes == 0 )
            throw GeoError( ErrorCode::InvalidArgument, "executionBudgetBytes must be positive" );
        return;   // grid negotiation/window semantics do not apply to a store
    }
    query.validate();
    if ( sceneBudget < 1 )
        throw GeoError( ErrorCode::InvalidArgument, "sceneBudget must be >= 1" );
    if ( executionBudgetBytes == 0 )
        throw GeoError( ErrorCode::InvalidArgument, "executionBudgetBytes must be positive" );
    if ( hasWindow && ( windowW <= 0 || windowH <= 0 ) )
        throw GeoError( ErrorCode::InvalidArgument, "window width/height must be positive" );
}

Json::Value FabricPlanCost::toJson() const
{
    Json::Value json;
    json["catalogMatches"] = static_cast<Json::UInt64>( catalogMatches );
    json["catalogMatchesTruncated"] = catalogMatchesTruncated;
    json["scenes"] = static_cast<Json::UInt64>( scenes );
    json["chunks"] = static_cast<Json::UInt64>( chunks );
    json["estimatedBytes"] = static_cast<Json::UInt64>( estimatedBytes );
    json["estimatedMemoryBytes"] = static_cast<Json::UInt64>( estimatedMemoryBytes );
    json["estimatedRemoteCalls"] = static_cast<Json::UInt64>( estimatedRemoteCalls );
    json["cacheableAssets"] = static_cast<Json::UInt64>( cacheableAssets );
    json["unprovableIdentityAssets"] = static_cast<Json::UInt64>( unprovableIdentityAssets );
    json["undatedScenes"] = static_cast<Json::UInt64>( undatedScenes );
    json["bytesUnknown"] = bytesUnknown;
    return json;
}

Json::Value FabricPlanStage::toJson() const
{
    Json::Value json;
    json["name"] = name;
    json["status"] = status;
    json["inputs"] = static_cast<Json::UInt64>( inputs );
    json["outputs"] = static_cast<Json::UInt64>( outputs );
    if ( !details.isNull() )
        json["details"] = details;
    return json;
}

Json::Value FabricPlan::toJson() const
{
    Json::Value json;
    Json::Value stages( Json::arrayValue );
    for ( const FabricPlanStage &stage : mStages )
        stages.append( stage.toJson() );
    json["stages"] = stages;
    json["cost"] = mCost.toJson();
    json["grid"] = mGrid.toJson();
    json["chunkPlan"] = mChunkPlan.toJson();
    json["windowPlan"] = mWindowPlan;
    Json::Value assets( Json::arrayValue );
    for ( const AssetRecord &record : mSelected )
    {
        if ( static_cast<int>( assets.size() ) >= 64 )   // bounded preview
        {
            json["selectedAssetsTruncated"] = true;
            break;
        }
        Json::Value asset;
        asset["id"] = record.id;
        asset["displayPath"] = ResourceUri::parse( record.path ).display();
        asset["instantUtc"] = record.datetimeUtc;
        assets.append( asset );
    }
    json["selectedAssets"] = assets;
    json["executionBudgetBytes"] = static_cast<Json::UInt64>( mIntent.executionBudgetBytes );
    return json;
}

FabricPlan planFabric( FabricIntent intent, const FabricPlanOptions &options,
                       const CancelToken &cancel )
{
    intent.validate();

    // --- 11.0: multidim source — the store IS the source. No catalog walk,
    // no asset selection, no grid negotiation: the descriptor's own dims
    // and the declared slice define the chunk space (WP D/E).
    if ( !intent.multidimPath.empty() )
    {
        FabricPlan plan;
        plan.mIntent = std::move( intent );

        const MultidimView view = MultidimView::open( plan.mIntent.multidimPath );
        const MultidimCubeDescriptor descriptor =
          describeCube( view, plan.mIntent.multidimVariable );

        FabricPlanStage openStage;
        openStage.name = "store_open";
        openStage.status = "planned";
        openStage.inputs = 1;
        openStage.outputs = 1;
        Json::Value openDetails;
        openDetails["path"] = ResourceUri::parse( descriptor.path ).display();
        openDetails["variable"] = descriptor.variable;
        openDetails["driver"] = descriptor.driver;
        openDetails["dimensions"] = static_cast<Json::UInt64>( descriptor.dimensionNames.size() );
        openStage.details = openDetails;
        plan.mStages.push_back( std::move( openStage ) );

        plan.mChunkPlan =
          CubeChunkPlan::forMultidimDescriptor( descriptor, plan.mIntent.chunkShape,
                                                plan.mIntent.slice );
        FabricPlanStage chunkStage;
        chunkStage.name = "chunk_planning";
        chunkStage.status = "planned";
        chunkStage.inputs = 1;
        chunkStage.outputs = 1;
        Json::Value chunkDetails;
        chunkDetails["chunkCountTotal"] =
          static_cast<Json::UInt64>( plan.mChunkPlan.chunkCountTotal() );
        chunkDetails["timeSliced"] = plan.mChunkPlan.timeSliced();
        chunkDetails["spatialSliced"] = plan.mChunkPlan.spatialSliced();
        chunkStage.details = chunkDetails;
        plan.mStages.push_back( std::move( chunkStage ) );

        plan.mCost.chunks = plan.mChunkPlan.chunkCountTotal();
        if ( plan.mChunkPlan.chunkCountTotal() > 0 )
        {
            const std::vector<CubeChunkRequest> first = plan.mChunkPlan.materializeChunks( 0, 1 );
            if ( !first.empty() )
                plan.mCost.estimatedBytes =
                  first.front().estimatedBytes * plan.mChunkPlan.chunkCountTotal();
        }
        plan.mCost.bytesUnknown = plan.mCost.estimatedBytes == 0;
        return plan;
    }

    FabricPlan plan;
    // The records path consumes the caller's vector (single owner; the plan
    // keeps only the selected scenes afterwards).
    std::vector<AssetRecord> ownedRecords = std::move( intent.records );
    intent.records.clear();
    plan.mIntent = std::move( intent );

    // --- stage: catalog_query (streaming, O(page + K) memory) -------------
    FabricPlanStage catalogStage;
    catalogStage.name = "catalog_query";
    TopKSelector selector( static_cast<std::size_t>( plan.mIntent.sceneBudget ) );
    std::uint64_t matches = 0;
    bool truncated = false;

    // The walk: count stop at query.maxItems, a no-progress break (a
    // backend that returns pages at a stuck offset must not spin forever),
    // and a page guard (bounds beats cleverness — same doctrine as
    // searchAll). THE one walk implementation for both sources.
    const auto walkPages = [ & ]( const CatalogService &service, const CatalogQuery &query ) {
        CatalogContinuation continuation;
        std::uint64_t inputIndex = 0;
        std::size_t previousOffset = 0;
        int guard = 0;
        while ( true )
        {
            if ( cancel.cancelled() )
                throw GeoError( ErrorCode::Cancelled, "catalog walk cancelled" );
            if ( ++guard > 100000 )
                throw GeoError( ErrorCode::ResourceExhausted,
                                "catalog pagination failed to terminate" );
            const CatalogPage page = service.searchPage( query, continuation, cancel );
            for ( const AssetRecord &record : page.records )
            {
                if ( plan.mIntent.query.maxItems > 0 &&
                     matches >= static_cast<std::uint64_t>( plan.mIntent.query.maxItems ) )
                {
                    truncated = true;
                    break;
                }
                ++matches;
                selector.offer( record, inputIndex++ );
            }
            if ( truncated )
                return;
            if ( !page.next.hasMore )
                return;
            if ( page.records.empty() && page.next.localOffset == previousOffset &&
                 page.next.remoteHref.empty() )
                return;   // no progress and no continuation: stop honestly
            previousOffset = page.next.localOffset;
            continuation = page.next;
        }
    };

    if ( !ownedRecords.empty() )
    {
        const CatalogService service = catalogServiceOverRecords( std::move( ownedRecords ) );
        CatalogQuery countQuery = plan.mIntent.query;
        countQuery.limit = 0;
        // Records are already in memory; stream through the SAME engine.
        walkPages( service, countQuery );
    }
    else
    {
        const CatalogService service = openCatalogService( plan.mIntent.catalogUri, options.catalog );
        walkPages( service, plan.mIntent.query );
    }
    plan.mSelected = selector.take();
    truncated = matches > plan.mSelected.size() && static_cast<int>( plan.mSelected.size() ) >=
                                                     plan.mIntent.sceneBudget;
    catalogStage.inputs = matches;
    catalogStage.outputs = plan.mSelected.size();
    catalogStage.status = "planned";
    plan.mStages.push_back( std::move( catalogStage ) );

    if ( plan.mSelected.empty() )
    {
        FabricPlanStage selection;
        selection.name = "asset_selection";
        selection.status = "skipped";
        plan.mStages.push_back( selection );
        FabricPlanStage gridStage;
        gridStage.name = "grid_planning";
        gridStage.status = "skipped";
        plan.mStages.push_back( gridStage );
        FabricPlanStage chunkStage;
        chunkStage.name = "chunk_planning";
        chunkStage.status = "skipped";
        plan.mStages.push_back( chunkStage );
        plan.mCost.catalogMatches = matches;
        plan.mCost.catalogMatchesTruncated = truncated;
        return plan;   // an empty plan is a valid plan (nothing matches)
    }

    // --- stage: asset_selection (top-K ordering already applied) ----------
    FabricPlanStage selectionStage;
    selectionStage.name = "asset_selection";
    selectionStage.status = "planned";
    selectionStage.inputs = matches;
    selectionStage.outputs = plan.mSelected.size();
    Json::Value selectionDetails;
    selectionDetails["policy"] = "cloud_asc__newest_first__id__input";
    selectionDetails["sceneBudget"] = plan.mIntent.sceneBudget;
    selectionStage.details = selectionDetails;
    plan.mStages.push_back( std::move( selectionStage ) );

    // --- stage: grid_planning (bounded metadata probe when derived) -------
    if ( !plan.mIntent.grid.explicitGrid )
    {
        VirtualCubeBuildOptions buildOptions;
        buildOptions.probeLimit = options.gridProbeLimit;
        // The probe re-runs at execution from the plan JSON; here it runs
        // once so the plan CARRIES the negotiated grid (inspectable).
        const VirtualCube negotiated = VirtualCube::build(
          plan.mSelected, plan.mIntent.grid, OverlapPolicy::FirstWins, VirtualCubeQuality {},
          buildOptions, cancel );
        plan.mGrid = negotiated.grid();
        // The negotiated grid is a DECISION of the plan: every later rebuild
        // (single-asset chunk execution, mirror, prefetch) must reuse it
        // exactly — re-deriving per rebuild would let a single-asset cube
        // negotiate a DIFFERENT grid and silently read the wrong area.
        plan.mGrid.explicitGrid = true;
    }
    else
    {
        plan.mGrid = plan.mIntent.grid;
        plan.mGrid.scaleX = plan.mIntent.grid.scaleX < 0 ? -plan.mIntent.grid.scaleX : plan.mIntent.grid.scaleX;
        plan.mGrid.scaleY = plan.mIntent.grid.scaleY < 0 ? -plan.mIntent.grid.scaleY : plan.mIntent.grid.scaleY;
    }
    FabricPlanStage gridStage;
    gridStage.name = "grid_planning";
    gridStage.status = "planned";
    gridStage.outputs = 1;
    Json::Value gridDetails;
    gridDetails["derived"] = !plan.mIntent.grid.explicitGrid;
    gridStage.details = gridDetails;
    plan.mStages.push_back( std::move( gridStage ) );

    // --- stage: chunk_planning --------------------------------------------
    const VirtualCube cubeForChunks =
      VirtualCube::build( plan.mSelected, plan.mGrid, OverlapPolicy::FirstWins,
                          VirtualCubeQuality {}, VirtualCubeBuildOptions {}, cancel );
    plan.mChunkPlan =
      CubeChunkPlan::forVirtualCube( cubeForChunks, plan.mIntent.chunkShape, plan.mIntent.slice );
    FabricPlanStage chunkStage;
    chunkStage.name = "chunk_planning";
    chunkStage.status = "planned";
    chunkStage.outputs = plan.mChunkPlan.chunkCountTotal();
    plan.mStages.push_back( std::move( chunkStage ) );

    // --- stage: identity_cache (bounded probes; fail-closed) ---------------
    FabricPlanStage identityStage;
    identityStage.name = "identity_cache";
    identityStage.status = "estimated";
    identityStage.inputs = plan.mSelected.size();
    int probed = 0;
    for ( const AssetRecord &record : plan.mSelected )
    {
        if ( probed >= options.identityProbeLimit )
            break;   // honest estimate bound: assets beyond stay uncounted
        ++probed;
        // The probe is a COST ESTIMATE: a failed probe (offline, unsupported
        // spelling, dead origin) means UNPROVABLE — fail-closed, never a
        // plan failure.
        try
        {
            const AssetIdentity identity =
              fabricAssetIdentity( record.path, AssetIdentityOptions{} );
            if ( identity.provable() )
                ++plan.mCost.cacheableAssets;
            else
                ++plan.mCost.unprovableIdentityAssets;
        }
        catch ( const GeoError & )
        {
            ++plan.mCost.unprovableIdentityAssets;
        }
    }
    identityStage.outputs = probed;
    plan.mStages.push_back( std::move( identityStage ) );

    // --- cost hints --------------------------------------------------------
    plan.mCost.catalogMatches = matches;
    plan.mCost.catalogMatchesTruncated = truncated;
    plan.mCost.scenes = plan.mSelected.size();
    plan.mCost.chunks = plan.mChunkPlan.chunkCountTotal();
    if ( plan.mIntent.hasWindow )
        plan.mWindowPlan = true;

    // Estimated bytes: window plans read exactly their window; chunk plans
    // report FIRST-CHUNK-SIZE × chunk count — a deliberate UPPER bound
    // (tail chunks are strictly smaller; exact sums need per-dim tail math
    // that buys nothing for a planning estimate).
    const Json::Value chunkJson = plan.mChunkPlan.toJson();
    std::uint64_t bytesPerChunk = 0;
    if ( !plan.mChunkPlan.dims().empty() && plan.mChunkPlan.chunkCountTotal() > 0 )
    {
        const std::vector<CubeChunkRequest> first = plan.mChunkPlan.materializeChunks( 0, 1 );
        if ( !first.empty() )
            bytesPerChunk = first.front().estimatedBytes;
    }
    if ( bytesPerChunk == 0 )
        plan.mCost.bytesUnknown = true;
    plan.mCost.estimatedBytes =
      plan.mIntent.hasWindow
        ? static_cast<std::uint64_t>( plan.mIntent.windowW ) * plan.mIntent.windowH * 8
        : bytesPerChunk * plan.mCost.chunks;
    if ( plan.mCost.bytesUnknown && plan.mIntent.hasWindow )
    {
        plan.mCost.estimatedBytes = static_cast<std::uint64_t>( plan.mIntent.windowW ) *
                                    plan.mIntent.windowH * 8;   // doubles in memory
        plan.mCost.bytesUnknown = false;
    }
    plan.mCost.estimatedMemoryBytes = plan.mCost.estimatedBytes;
    plan.mCost.estimatedRemoteCalls =
      plan.mIntent.hasWindow ? 1 : plan.mCost.chunks;   // pessimistic miss estimate
    plan.mCost.undatedScenes = static_cast<std::uint64_t>( std::count_if(
      plan.mSelected.begin(), plan.mSelected.end(),
      [] ( const AssetRecord &record ) { return record.datetimeUtc.empty(); } ) );

    if ( cancel.cancelled() )
        throw GeoError( ErrorCode::Cancelled, "plan cancelled" );
    return plan;
}

// --- JSON intent parsing (the single shared parser) ---------------------------

FabricIntent fabricIntentFromJson( const Json::Value &json )
{
    try
    {
    FabricIntent intent;
    intent.catalogUri = json.get( "catalog", "" ).asString();
    intent.sceneBudget = json.get( "sceneBudget", 64 ).asInt();
    intent.executionBudgetBytes = static_cast<std::uint64_t>( json.get( "executionBudgetBytes", 268435456.0 ).asUInt64() );

    const Json::Value &bounds = json["bounds"];
    if ( bounds.isArray() && ( bounds.size() == 4 || bounds.size() == 6 ) )
        for ( const Json::Value &value : bounds )
            intent.query.bbox.push_back( value.asDouble() );
    // Nested query{} is authoritative; TOP-LEVEL shorthand fields (the
    // flat operator/CLI shape) fill anything the nested object left unset.
    const Json::Value &queryJson = json["query"];
    const Json::Value &effectiveQuery = queryJson.isObject() ? queryJson : json;
    {
        const bool nestedBbox = queryJson.isObject() && queryJson["bbox"].isArray();
        if ( !nestedBbox && !intent.query.bbox.empty() )
        {
            // top-level bounds already applied — keep them unless the query
            // object declares its own
        }
        if ( effectiveQuery["bbox"].isArray() && !nestedBbox )
        {
            if ( !intent.query.bbox.empty() )
                intent.query.bbox.clear();
            for ( const Json::Value &value : effectiveQuery["bbox"] )
                intent.query.bbox.push_back( value.asDouble() );
        }
        else
        {
            for ( const Json::Value &value : queryJson["bbox"] )
                intent.query.bbox.push_back( value.asDouble() );
        }
        const auto pick = [ & ]( const char *field ) -> Json::Value {
            if ( queryJson.isObject() && queryJson.isMember( field ) )
                return queryJson[field];
            return json[field];
        };
        if ( !pick( "temporalStartUtc" ).isNull() )
            intent.query.temporalStartUtc = pick( "temporalStartUtc" ).asString();
        if ( !pick( "temporalEndUtc" ).isNull() )
            intent.query.temporalEndUtc = pick( "temporalEndUtc" ).asString();
        if ( queryJson["collections"].isArray() )
            for ( const Json::Value &value : queryJson["collections"] )
                intent.query.collections.push_back( value.asString() );
        else
            for ( const Json::Value &value : json["collections"] )
                intent.query.collections.push_back( value.asString() );
        if ( queryJson["ids"].isArray() )
            for ( const Json::Value &value : queryJson["ids"] )
                intent.query.ids.push_back( value.asString() );
        else
            for ( const Json::Value &value : json["ids"] )
                intent.query.ids.push_back( value.asString() );
        if ( !pick( "cloudCoverMax" ).isNull() )
        {
            intent.query.hasCloudCoverMax = true;
            intent.query.cloudCoverMax = pick( "cloudCoverMax" ).asDouble();
        }
        if ( !pick( "platform" ).isNull() )
            intent.query.platformEquals = pick( "platform" ).asString();
        if ( queryJson["sensors"].isArray() )
            for ( const Json::Value &value : queryJson["sensors"] )
                intent.query.sensorInstruments.push_back( value.asString() );
        else
            for ( const Json::Value &value : json["sensors"] )
                intent.query.sensorInstruments.push_back( value.asString() );
        if ( !pick( "assetRole" ).isNull() )
            intent.query.assetRole = pick( "assetRole" ).asString();
        if ( !pick( "mediaType" ).isNull() )
            intent.query.mediaTypeSubstring = pick( "mediaType" ).asString();
        if ( !pick( "limit" ).isNull() )
            intent.query.limit = pick( "limit" ).asInt();
        if ( !pick( "maxItems" ).isNull() )
            intent.query.maxItems = pick( "maxItems" ).asInt();
    }

    const Json::Value &gridJson = json["grid"];
    if ( gridJson.isObject() && gridJson.isMember( "crs" ) )
    {
        intent.grid.explicitGrid = true;
        intent.grid.crs.valid = true;
        intent.grid.crs.authid = gridJson["crs"].asString();
        intent.grid.scaleX = gridJson.get( "scaleX", 0.0 ).asDouble();
        intent.grid.scaleY = gridJson.get( "scaleY", 0.0 ).asDouble();
        const Json::Value &extent = gridJson["extent"];
        if ( extent.isArray() && extent.size() == 4 )
        {
            intent.grid.minX = extent[0].asDouble();
            intent.grid.minY = extent[1].asDouble();
            intent.grid.maxX = extent[2].asDouble();
            intent.grid.maxY = extent[3].asDouble();
        }
    }

    const Json::Value &shapeJson = json["chunkShape"];
    if ( shapeJson.isObject() )
    {
        intent.chunkShape.time = shapeJson.get( "time", 1 ).asInt64();
        intent.chunkShape.y = shapeJson.get( "y", 256 ).asInt64();
        intent.chunkShape.x = shapeJson.get( "x", 256 ).asInt64();
        intent.chunkShape.band = shapeJson.get( "band", 1 ).asInt64();
        // 11.0: per-name chunk sizes for extra named dimensions (multidim).
        const Json::Value &perDim = shapeJson["perDimension"];
        if ( perDim.isObject() )
            for ( const std::string &name : perDim.getMemberNames() )
                intent.chunkShape.perDimension[name] = perDim[name].asInt64();
    }

    const Json::Value &sliceJson = json["slice"];
    if ( sliceJson.isObject() )
    {
        intent.slice.timeStartUtc = sliceJson.get( "timeStartUtc", "" ).asString();
        intent.slice.timeEndUtc = sliceJson.get( "timeEndUtc", "" ).asString();
        const Json::Value &extent = sliceJson["extent"];
        if ( extent.isArray() && extent.size() == 4 )
        {
            intent.slice.hasSpatialSlice = true;
            intent.slice.minX = extent[0].asDouble();
            intent.slice.minY = extent[1].asDouble();
            intent.slice.maxX = extent[2].asDouble();
            intent.slice.maxY = extent[3].asDouble();
        }
        for ( const Json::Value &role : sliceJson["bandRoles"] )
            intent.slice.bandRoles.push_back( role.asString() );
        // 11.0: explicit named-dimension ranges — "dimensionRanges":
        // {"band": [begin,end], "level": [begin,end]} (half-open).
        const Json::Value &ranges = sliceJson["dimensionRanges"];
        if ( ranges.isObject() )
            for ( const std::string &name : ranges.getMemberNames() )
            {
                const Json::Value &pair = ranges[name];
                if ( !pair.isArray() || pair.size() != 2 )
                    throw GeoError( ErrorCode::InvalidArgument,
                                    "slice dimensionRanges[" + name + "] must be [begin,end]" );
                intent.slice.dimensionRanges[name] =
                  { pair[0].asInt64(), pair[1].asInt64() };
            }
    }

    // 11.0: multidim source vocabulary — "multidim": {"path": …, "variable": …}.
    const Json::Value &multidimJson = json["multidim"];
    if ( multidimJson.isObject() )
    {
        intent.multidimPath = multidimJson.get( "path", "" ).asString();
        intent.multidimVariable = multidimJson.get( "variable", "" ).asString();
        if ( intent.multidimPath.empty() || intent.multidimVariable.empty() )
            throw GeoError( ErrorCode::InvalidArgument,
                            "multidim source needs both path and variable" );
    }

    const Json::Value &windowJson = json["window"];
    if ( windowJson.isObject() )
    {
        intent.hasWindow = true;
        intent.windowX = windowJson.get( "x", 0 ).asInt();
        intent.windowY = windowJson.get( "y", 0 ).asInt();
        intent.windowW = windowJson.get( "w", 0 ).asInt();
        intent.windowH = windowJson.get( "h", 0 ).asInt();
    }
    intent.validate();
    return intent;
    }
    catch ( const Json::Exception &error )
    {
        // Wrong-typed JSON (a string where a number belongs…) is a caller
        // contract violation — typed, never a std exception through the
        // operator boundary.
        throw GeoError( ErrorCode::InvalidArgument,
                        std::string( "fabric intent JSON field type mismatch: " ) + error.what() );
    }
}

// --- execution ----------------------------------------------------------------

namespace
{

/// One-asset cube for time-correct chunk execution (the chunk's hinted
/// asset is the whole world of that step).
VirtualCube cubeOverSingleAsset( const AssetRecord &record, const VirtualCubeGrid &grid )
{
    return VirtualCube::build( { record }, grid, OverlapPolicy::FirstWins, {},
                               VirtualCubeBuildOptions {}, CancelToken{} );
}

/// 11.0 (WP D/E): multidim chunk execution. Every chunk's non-spatial
/// extents are enumerated per source index (chunks may span several along
/// a dim with chunk size > 1); each combination materializes exactly one
/// 2-D window through readSliceWindow — the SAME single read path as every
/// other multidim read (no duplicated slicing machinery). Sliced dims map
/// their post-slice offsets back to source axis indices through the plan's
/// multidimSelection(); unsliced dims use the offset directly.
std::vector<FabricChunkOutcome> executeMultidimChunks( const FabricPlan &plan,
                                                       const VirtualCubeReadOptions &options,
                                                       std::size_t chunkWindow,
                                                       const std::function<void( const CubeChunkRequest &,
                                                                                 const VirtualCubeWindowResult & )> &sink,
                                                       FabricExecutionReport &report,
                                                       const CancelToken &cancel )
{
    const CubeChunkPlan &chunkPlan = plan.chunkPlan();
    const MultidimCubeDescriptor descriptor = chunkPlan.multidimDescriptor();
    const auto &selection = chunkPlan.multidimSelection();
    const auto &dims = chunkPlan.dims();
    if ( dims.size() < 3 )
        throw GeoError( ErrorCode::InvalidArgument,
                        "multidim chunk plan without y/x tail cannot execute" );

    MultidimView view = MultidimView::open( descriptor.path );
    const std::size_t maxCells = 64ull * 1024 * 1024;

    const auto sourceIndexOf = [ &selection ]( const std::string &name, std::int64_t offset ) {
        const auto it = selection.find( name );
        if ( it == selection.end() )
            return offset;
        if ( offset < 0 || offset >= static_cast<std::int64_t>( it->second.size() ) )
            throw GeoError( ErrorCode::InvalidArgument,
                            "chunk offset beyond the sliced axis: " + name );
        return it->second[static_cast<std::size_t>( offset )];
    };

    std::vector<FabricChunkOutcome> outcomes;
    const std::uint64_t total = chunkPlan.chunkCountTotal();
    outcomes.reserve( std::min<std::uint64_t>( total, 1024 ) );
    std::uint64_t outcomesDropped = 0;
    std::uint64_t bytesSpent = 0;
    std::uint64_t settled = 0;
    bool budgetBreached = false;
    bool cancelled = false;
    const auto recordOutcome = [ & ]( FabricChunkOutcome outcome ) {
      if ( outcomes.size() < 1024 )
        outcomes.push_back( std::move( outcome ) );
      else
        ++outcomesDropped;
      ++settled;
    };

    for ( std::uint64_t begin = 0; begin < total && !cancelled; begin += chunkWindow )
    {
        if ( cancel.cancelled() )
            throw GeoError( ErrorCode::Cancelled, "chunk execution cancelled" );
        const std::vector<CubeChunkRequest> requests =
          chunkPlan.materializeChunks( begin, chunkWindow );
        for ( const CubeChunkRequest &request : requests )
        {
            if ( cancel.cancelled() )
            {
                cancelled = true;
                break;
            }
            FabricChunkOutcome outcome;
            outcome.index = request.index;

            // Layout: [...middles, (time), y, x] — the time dim exists only
            // when the descriptor has a temporal axis (review R13 P1: a
            // [band,y,x] store plans without a phantom time dim).
            const bool hasTimeDim =
              dims.size() >= 3 && dims[dims.size() - 3].name == "time";
            const std::size_t spatialCount = hasTimeDim ? 3 : 2;
            const std::size_t leadingDims = dims.size() - spatialCount;
            std::vector<std::size_t> step( leadingDims + ( hasTimeDim ? 1 : 0 ), 0 );
            std::vector<std::size_t> span( leadingDims + ( hasTimeDim ? 1 : 0 ), 0 );
            for ( std::size_t d = 0; d < leadingDims; ++d )
                span[d] = static_cast<std::size_t>( std::max<std::int64_t>( request.dimSizes[d], 1 ) );
            if ( hasTimeDim )
              span[leadingDims] =
                static_cast<std::size_t>( std::max<std::int64_t>( request.dimSizes[leadingDims], 1 ) );
            bool overflowed = false;
            while ( !overflowed )
            {
                // Resolve this combination's source indices.
                std::vector<std::pair<std::string, std::int64_t>> dimSlices;
                dimSlices.reserve( leadingDims + 1 );
                for ( std::size_t d = 0; d < leadingDims; ++d )
                    dimSlices.push_back(
                      { dims[d].name,
                        sourceIndexOf( dims[d].name, request.dimOffsets[d] +
                                                       static_cast<std::int64_t>( step[d] ) ) } );
                if ( hasTimeDim )
                    dimSlices.push_back(
                      { "time",
                        sourceIndexOf( "time", request.dimOffsets[leadingDims] +
                                                 static_cast<std::int64_t>( step[leadingDims] ) ) } );

                const std::int64_t ySource =
                  sourceIndexOf( "y", request.dimOffsets[leadingDims + ( hasTimeDim ? 1 : 0 )] );
                const std::int64_t xSource =
                  sourceIndexOf( "x", request.dimOffsets[leadingDims + ( hasTimeDim ? 2 : 1 )] );
                const std::size_t rows =
                  static_cast<std::size_t>( std::max<std::int64_t>( request.dimSizes[leadingDims + ( hasTimeDim ? 1 : 0 )], 1 ) );
                const std::size_t cols =
                  static_cast<std::size_t>( std::max<std::int64_t>( request.dimSizes[leadingDims + 2], 1 ) );

                const std::uint64_t windowBytes = static_cast<std::uint64_t>( rows ) * cols * 8;
                if ( bytesSpent + windowBytes > plan.intent().executionBudgetBytes )
                {
                    outcome.skippedBudget = true;
                    budgetBreached = true;
                    break;
                }

                const MultidimGrid grid = view.readSliceWindow(
                  descriptor.variable, dimSlices,
                  ySource, xSource, rows, cols, maxCells );

                VirtualCubeWindowResult window;
                window.width = static_cast<int>( grid.cols );
                window.height = static_cast<int>( grid.rows );
                window.values = std::move( grid.values );
                window.gridNoData = grid.hasNoData && !grid.noDataIsNaN
                                      ? grid.noDataValue : -9999.0;
                window.noDataIsNaN = grid.noDataIsNaN;

                bytesSpent += windowBytes;
                report.bytesRead += windowBytes;
                report.chunksExecuted += 1;
                report.assetsConsulted += 1;
                outcome.ok = true;
                outcome.bytesRead = windowBytes;
                if ( sink )
                    sink( request, window );

                // Odometer advance (last leading dim fastest).
                bool carried = false;
                for ( std::size_t d = leadingDims + 1; d-- > 0; )
                {
                    if ( ++step[d] < span[d] )
                    {
                        carried = true;
                        break;
                    }
                    step[d] = 0;
                }
                if ( !carried )
                    overflowed = true;
            }
            // Review R13: a chunk whose combinations were cut short by the
            // budget is a SKIPPED chunk — never half-ok.
            if ( outcome.skippedBudget )
                outcome.ok = false;
            recordOutcome( std::move( outcome ) );
        }
    }
    report.budgetBreached = budgetBreached;
    report.outcomesDropped = outcomesDropped;
    if ( cancelled )
        report.cancelledRemaining = total > settled ? total - settled : 0;
    return outcomes;
}

/// Chunk grid extent → grid pixel rect (clamped; ok=false when empty).
bool chunkPixelRect( const CubeChunkRequest &request, const VirtualCubeGrid &grid, int &x, int &y,
                     int &w, int &h )
{
    if ( !request.hasExtent )
        return false;
    const double px0 = ( request.minX - grid.minX ) / grid.scaleX;
    const double px1 = ( request.maxX - grid.minX ) / grid.scaleX;
    const double py0 = ( grid.maxY - request.maxY ) / grid.scaleY;
    const double py1 = ( grid.maxY - request.minY ) / grid.scaleY;
    const int gx = static_cast<int>( grid.width() );
    const int gy = static_cast<int>( grid.height() );
    x = static_cast<int>( std::max( 0.0, std::floor( px0 ) ) );
    y = static_cast<int>( std::max( 0.0, std::floor( py0 ) ) );
    w = static_cast<int>( std::clamp( std::ceil( px1 ) - x, 0.0, double( gx - x ) ) );
    h = static_cast<int>( std::clamp( std::ceil( py1 ) - y, 0.0, double( gy - y ) ) );
    return w > 0 && h > 0;
}

} // namespace

VirtualCubeWindowResult executeWindow( const FabricPlan &plan, const VirtualCubeReadOptions &options,
                                       FabricExecutionReport &report, const CancelToken &cancel )
{
    if ( !plan.isWindowPlan() )
        throw GeoError( ErrorCode::InvalidArgument,
                        "executeWindow needs a window plan (intent.hasWindow)" );
    if ( plan.selectedAssets().empty() )
        throw GeoError( ErrorCode::InvalidArgument, "plan selected no assets" );

    const FabricIntent &intent = plan.intent();
    const std::uint64_t windowBytes =
      static_cast<std::uint64_t>( intent.windowW ) * intent.windowH * 8;
    if ( windowBytes > intent.executionBudgetBytes )
        throw GeoError( ErrorCode::ResourceExhausted,
                        "window needs " + std::to_string( windowBytes ) +
                          " bytes beyond the execution budget" );

    const VirtualCube cube =
      VirtualCube::build( plan.selectedAssets(), plan.grid(), OverlapPolicy::FirstWins, {},
                          VirtualCubeBuildOptions {}, cancel );
    VirtualCubeWindowResult result =
      cube.readWindow( intent.windowX, intent.windowY, intent.windowW, intent.windowH, options,
                       cancel );
    report.assetsConsulted = result.provenance.size();
    for ( const VirtualCubeProvenance &entry : result.provenance )
        report.assetsFailed += entry.failed ? 1 : 0;
    report.bytesRead = windowBytes;
    report.chunksExecuted = 1;
    return result;
}

std::vector<FabricChunkOutcome> executeChunks( const FabricPlan &plan,
                                               const VirtualCubeReadOptions &options,
                                               std::size_t chunkWindow,
                                               const std::function<void( const CubeChunkRequest &,
                                                                         const VirtualCubeWindowResult & )> &sink,
                                               FabricExecutionReport &report,
                                               const CancelToken &cancel )
{
    const FabricIntent &intent = plan.intent();

    // 11.0: multidim plans execute through the multidim reader (WP D/E).
    if ( !plan.chunkPlan().isEo() && !plan.chunkPlan().multidimDescriptor().path.empty() )
        return executeMultidimChunks( plan, options, chunkWindow, sink, report, cancel );

    if ( plan.selectedAssets().empty() )
        throw GeoError( ErrorCode::InvalidArgument, "plan selected no assets" );
    if ( chunkWindow == 0 || chunkWindow > 4096 )
        throw GeoError( ErrorCode::InvalidArgument, "chunkWindow must be within [1, 4096]" );

    // Assets by id — the chunk hint resolves its time step's world.
    std::map<std::string, AssetRecord> byId;
    for ( const AssetRecord &record : plan.selectedAssets() )
        byId[record.id] = record;

    std::vector<FabricChunkOutcome> outcomes;
    const std::uint64_t total = plan.chunkPlan().chunkCountTotal();
    outcomes.reserve( std::min<std::uint64_t>( total, 1024 ) );
    std::uint64_t outcomesDropped = 0;   // beyond the retained window: counters only
    std::uint64_t bytesSpent = 0;
    std::uint64_t settled = 0;           // chunks with a recorded outcome
    bool budgetBreached = false;
    bool cancelled = false;
    const auto recordOutcome = [ & ]( FabricChunkOutcome outcome ) {
      if ( outcomes.size() < 1024 )
        outcomes.push_back( std::move( outcome ) );
      else
        ++outcomesDropped;   // D-1008: never materialize the whole enumeration
      ++settled;
    };
    for ( std::uint64_t begin = 0; begin < total && !cancelled; begin += chunkWindow )
    {
        if ( cancel.cancelled() )
            throw GeoError( ErrorCode::Cancelled, "chunk execution cancelled" );
        const std::vector<CubeChunkRequest> requests =
          plan.chunkPlan().materializeChunks( begin, chunkWindow );
        for ( const CubeChunkRequest &request : requests )
        {
            if ( cancel.cancelled() )
            {
                cancelled = true;
                break;
            }

            FabricChunkOutcome outcome;
            outcome.index = request.index;
            outcome.assetIdHint = request.assetIdHint;

            const auto assetIt = byId.find( request.assetIdHint );
            if ( assetIt == byId.end() )
            {
                outcome.ok = false;
                outcome.errorText = "chunk hint '" + request.assetIdHint + "' is not a selected asset";
                recordOutcome( std::move( outcome ) );
                continue;
            }

            int x = 0, y = 0, w = 0, h = 0;
            if ( !chunkPixelRect( request, plan.grid(), x, y, w, h ) )
            {
                outcome.ok = false;
                outcome.errorText = "chunk extent maps to an empty pixel rect";
                recordOutcome( std::move( outcome ) );
                continue;
            }
            const std::uint64_t windowBytes = static_cast<std::uint64_t>( w ) * h * 8;
            if ( bytesSpent + windowBytes > plan.intent().executionBudgetBytes )
            {
                outcome.skippedBudget = true;
                recordOutcome( std::move( outcome ) );
                budgetBreached = true;
                continue;   // keep counting the remainder as skipped
            }

            // Time-correct execution: one asset IS one time step, so each
            // chunk reads a single-asset cube (no probe: explicit grid).
            const VirtualCube cube = cubeOverSingleAsset( assetIt->second, plan.grid() );
            const VirtualCubeWindowResult window = cube.readWindow( x, y, w, h, options, cancel );
            bytesSpent += windowBytes;
            report.bytesRead += windowBytes;
            report.chunksExecuted += 1;
            report.assetsConsulted += window.provenance.size();
            for ( const VirtualCubeProvenance &entry : window.provenance )
                report.assetsFailed += entry.failed ? 1 : 0;
            outcome.ok = true;
            outcome.bytesRead = windowBytes;
            recordOutcome( std::move( outcome ) );
            if ( sink )
                sink( request, window );
        }
    }
    report.budgetBreached = budgetBreached;
    report.outcomesDropped = outcomesDropped;
    if ( cancelled )
        report.cancelledRemaining = total > settled ? total - settled : 0;
    return outcomes;
}

Json::Value FabricExecutionReport::toJson() const
{
    Json::Value json;
    json["bytesRead"] = static_cast<Json::UInt64>( bytesRead );
    json["assetsConsulted"] = static_cast<Json::UInt64>( assetsConsulted );
    json["assetsFailed"] = static_cast<Json::UInt64>( assetsFailed );
    json["chunksExecuted"] = static_cast<Json::UInt64>( chunksExecuted );
    json["budgetBreached"] = budgetBreached;
    return json;
}

} // namespace sicnu::geo
