/***************************************************************************
  geospatial/fabric/prefetch.cpp — bounded cache prefetch.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#include "geospatial/fabric/prefetch.h"

#include "geospatial/fabric/mirror.h"
#include "geospatial/fabric/object_store.h"
#include "geospatial/identity/asset_identity.h"
#include "geospatial/remote/range_cache.h"
#include "geospatial/raster/raster_reader.h"

#include <algorithm>
#include <map>

namespace sicnu::geo
{

Json::Value PrefetchReport::toJson() const
{
    Json::Value json;
    Json::Value chunksJson( Json::arrayValue );
    for ( const PrefetchChunkOutcome &outcome : chunks )
    {
        Json::Value entry;
        entry["index"] = static_cast<Json::UInt64>( outcome.index );
        entry["status"] = outcome.status;
        entry["bytesPulled"] = static_cast<Json::UInt64>( outcome.bytesPulled );
        if ( !outcome.errorText.empty() )
            entry["error"] = outcome.errorText;
        if ( !outcome.assetIdHint.empty() )
            entry["assetIdHint"] = outcome.assetIdHint;
        chunksJson.append( entry );
    }
    json["chunks"] = chunksJson;
    json["bytesPulled"] = static_cast<Json::UInt64>( bytesPulled );
    json["warmed"] = static_cast<Json::UInt64>( warmed );
    json["cacheHits"] = static_cast<Json::UInt64>( cacheHits );
    json["mirrorHits"] = static_cast<Json::UInt64>( mirrorHits );
    json["skippedBudget"] = static_cast<Json::UInt64>( skippedBudget );
    json["skippedCancel"] = static_cast<Json::UInt64>( skippedCancel );
    json["failed"] = static_cast<Json::UInt64>( failed );
    json["budgetExhausted"] = budgetExhausted;
    json["outcomesDropped"] = static_cast<Json::UInt64>( outcomesDropped );
    return json;
}

namespace
{

/// The shared walk. For each chunk: resolve its hinted asset, map the
/// chunk's world extent onto that asset's pixels, read the window through
/// the range-cache spelling and measure the origin bytes from the cache's
/// telemetry delta. Values are DISCARDED — prefetch warms, it does not use.
PrefetchReport prefetchChunksImpl( const VirtualCube &cube, const CubeChunkPlan &plan,
                                   const PrefetchOptions &options, const CancelToken &cancel )
{
    if ( !RemoteRangeCache::installed() )
        throw GeoError( ErrorCode::InvalidArgument,
                        "prefetch needs the range cache installed (/vsirangecache/)" );
    if ( !cube.grid().valid() )
        throw GeoError( ErrorCode::InvalidArgument, "cube grid is invalid — cannot prefetch" );

    PrefetchReport report;
    report.chunks.reserve( 1024 );
    std::uint64_t pulled = 0;
    std::uint64_t outcomesDropped = 0;
    const auto recordOutcome = [ & ]( PrefetchChunkOutcome outcome ) {
      if ( report.chunks.size() < 1024 )
        report.chunks.push_back( std::move( outcome ) );
      else
        ++outcomesDropped;
    };
    // maxBytes==0 means the DECLARED default bound: the chunk plan's own
    // byte estimate (first-chunk size × chunk count) — never unbounded.
    std::uint64_t budget = options.maxBytes;
    if ( budget == 0 )
    {
        const std::uint64_t total = plan.chunkCountTotal();
        if ( total > 0 )
        {
            const std::vector<CubeChunkRequest> first = plan.materializeChunks( 0, 1 );
            budget = ( first.empty() ? 0 : first.front().estimatedBytes ) * total;
        }
    }

    std::map<std::string, AssetRecord> byId;
    for ( const VirtualCubeAssetIndexEntry &entry : cube.assets() )
        byId[entry.record.id] = entry.record;
    // Identity probes per ASSET (not per chunk) when a mirror was declared.
    std::map<std::string, AssetIdentity> identities;
    ( void )identities.size();

    const std::uint64_t total = plan.chunkCountTotal();
    const std::size_t chunkWindow = options.chunkWindow == 0 ? 64 : options.chunkWindow;
    bool budgetExhausted = false;
    for ( std::uint64_t begin = 0; begin < total; begin += chunkWindow )
    {
        if ( cancel.cancelled() )
            break;
        const std::vector<CubeChunkRequest> requests =
          plan.materializeChunks( begin, std::min<std::size_t>( chunkWindow, 4096 ) );
        for ( const CubeChunkRequest &request : requests )
        {
            if ( cancel.cancelled() )
            {
                // The un-walked remainder is reported as skipped, not silent.
                report.skippedCancel += total - request.index;
                break;
            }

            PrefetchChunkOutcome outcome;
            outcome.index = request.index;
            outcome.assetIdHint = request.assetIdHint;

            const auto assetIt = byId.find( request.assetIdHint );
            if ( assetIt == byId.end() )
            {
                outcome.status = "failed";
                outcome.errorText = "chunk hint '" + request.assetIdHint +
                                    "' is not an indexed asset";
                ++report.failed;
                recordOutcome( std::move( outcome ) );
                continue;
            }
            const AssetRecord &record = assetIt->second;

            // Mirror first: a token-keyed hit means the chunk is already local.
            // (The identity check runs only when a mirror was declared; the
            // whole probe sits inside the per-chunk failure budget — one dead
            // asset cannot void the walk.)
            if ( !options.mirrorDirectory.empty() )
            {
                try
                {
                    AssetIdentity identity;
                    const auto identityIt = identities.find( record.id );
                    if ( identityIt != identities.end() )
                    {
                        identity = identityIt->second;
                    }
                    else
                    {
                        identity = assetIdentityToken( record.path, AssetIdentityOptions{} );
                        identities[record.id] = identity;
                    }
                    if ( identity.provable() )
                    {
                        std::string skippedCorrupt;
                        RasterReader probeReader =
                          RasterReader::open( fabricCachedPath( record.path ) );
                        const VirtualCubeSourceWindow mapped = virtualCubeSourceWindow(
                          probeReader.metadata(), request.minX, request.minY, request.maxX,
                          request.maxY );
                        if ( mapped.ok )
                        {
                            const std::string key = fabricChunkMirrorKey(
                              identity.token, record.path, mapped.window, "band1" );
                            const std::string hit =
                              resolveMirrorHit( options.mirrorDirectory, identity.token, key,
                                                &skippedCorrupt );
                            if ( !hit.empty() )
                            {
                                outcome.status = "mirror-hit";
                                ++report.mirrorHits;
                                recordOutcome( std::move( outcome ) );
                                continue;
                            }
                        }
                    }
                }
                catch ( const GeoError &error )
                {
                    // Mirror probing is best-effort: fall through to the
                    // normal read path (which has its own per-chunk handling).
                }
            }

            if ( budget > 0 && pulled >= budget )
            {
                outcome.status = "skipped-budget";
                ++report.skippedBudget;
                report.budgetExhausted = true;
                recordOutcome( std::move( outcome ) );
                continue;
            }

            try
            {
                RasterReader reader = RasterReader::open( fabricCachedPath( record.path ) );
                const VirtualCubeSourceWindow mapped = virtualCubeSourceWindow(
                  reader.metadata(), request.minX, request.minY, request.maxX, request.maxY );
                if ( !mapped.ok )
                {
                    outcome.status = "failed";
                    outcome.errorText = "chunk extent misses the asset raster";
                    ++report.failed;
                    recordOutcome( std::move( outcome ) );
                    continue;
                }
                const std::uintmax_t telemetryBefore =
                  RemoteRangeCache::telemetryJson()["bytes_fetched"].asUInt64();
                ( void )reader.readWindow( { 1 }, mapped.window,
                                           options.maxChunkBytes ? options.maxChunkBytes
                                                                 : 16ull * 1024 * 1024 );
                const std::uintmax_t telemetryAfter =
                  RemoteRangeCache::telemetryJson()["bytes_fetched"].asUInt64();
                const std::uint64_t chunkBytes =
                  static_cast<std::uint64_t>( telemetryAfter - telemetryBefore );
                pulled += chunkBytes;
                outcome.bytesPulled = chunkBytes;
                // cache-hit vs warmed: origin bytes pulled tells the story.
                if ( chunkBytes == 0 )
                {
                    outcome.status = "cache-hit";
                    ++report.cacheHits;
                }
                else
                {
                    outcome.status = "warmed";
                    ++report.warmed;
                }
                report.bytesPulled += chunkBytes;
                recordOutcome( std::move( outcome ) );
            }
            catch ( const GeoError &error )
            {
                outcome.status = "failed";
                outcome.errorText = error.what();
                ++report.failed;
                recordOutcome( std::move( outcome ) );
            }
        }
    }
    return report;
}

} // namespace

PrefetchReport prefetchChunks( const FabricPlan &plan, const PrefetchOptions &options,
                               const CancelToken &cancel )
{
    if ( !plan.chunkPlan().isEo() )
        throw GeoError( ErrorCode::InvalidArgument,
                        "prefetch walks EO-cube plans — multidim stores are local" );
    const VirtualCube cube =
      VirtualCube::build( plan.selectedAssets(), plan.grid(), OverlapPolicy::FirstWins, {},
                          VirtualCubeBuildOptions {}, cancel );
    return prefetchChunksImpl( cube, plan.chunkPlan(), options, cancel );
}

PrefetchReport prefetchChunks( const VirtualCube &cube, const CubeChunkPlan &plan,
                               const PrefetchOptions &options, const CancelToken &cancel )
{
    return prefetchChunksImpl( cube, plan, options, cancel );
}

} // namespace sicnu::geo
