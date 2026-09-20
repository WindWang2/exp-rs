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
#include <cmath>
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

    const std::uint64_t total = plan.chunkCountTotal();
    const std::size_t chunkWindow = options.chunkWindow == 0 ? 64 : options.chunkWindow;
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

            // Budget BEFORE any open: an exhausted walk must not keep
            // paying metadata opens for chunks it will only skip.
            if ( budget > 0 && pulled >= budget )
            {
                outcome.status = "skipped-budget";
                ++report.skippedBudget;
                report.budgetExhausted = true;
                recordOutcome( std::move( outcome ) );
                continue;
            }

            // ONE open per chunk: the same reader answers the mirror lookup
            // and the warm read (the 10.0 walk opened twice per chunk when
            // a mirror was declared — 2M metadata opens per million chunks).
            RasterReader reader;
            const RasterMetadata *metadata = nullptr;
            try
            {
                reader = RasterReader::open( fabricCachedPath( record.path ) );
                metadata = &reader.metadata();
            }
            catch ( const GeoError &error )
            {
                outcome.status = "failed";
                outcome.errorText = error.what();
                ++report.failed;
                recordOutcome( std::move( outcome ) );
                continue;
            }

            // Mirror first: a token-keyed hit means the chunk is already local.
            // (The identity check runs only when a mirror was declared; the
            // probe is best-effort — one dead asset cannot void the walk.)
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
                        identity = fabricAssetIdentity( record.path, AssetIdentityOptions{} );
                        identities[record.id] = identity;
                    }
                    if ( identity.provable() )
                    {
                        const VirtualCubeSourceWindow mapped = virtualCubeSourceWindow(
                          *metadata, request.minX, request.minY, request.maxX, request.maxY );
                        if ( mapped.ok )
                        {
                            // 11.0 key basis + 10.0 legacy key: both spellings resolve.
                            const std::string indexKey = fabricMirrorIndexKey( record.path );
                            std::string skippedCorrupt;
                            std::string key = fabricChunkMirrorKey(
                              identity.token, indexKey, mapped.window, "band1" );
                            std::string hit =
                              resolveMirrorHit( options.mirrorDirectory, identity.token, key,
                                                &skippedCorrupt );
                            if ( hit.empty() )
                            {
                                const std::string legacyKey = fabricChunkMirrorKey(
                                  identity.token, record.path, mapped.window, "band1" );
                                if ( legacyKey != key )
                                  hit = resolveMirrorHit( options.mirrorDirectory, identity.token,
                                                          legacyKey, &skippedCorrupt );
                            }
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
                catch ( const GeoError & )
                {
                    // Mirror probing is best-effort: fall through to the
                    // normal read path (which has its own per-chunk handling).
                }
            }

            try
            {
                const VirtualCubeSourceWindow mapped = virtualCubeSourceWindow(
                  *metadata, request.minX, request.minY, request.maxX, request.maxY );
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

// --- 11.0 (WP F, D-1107): access-pattern-driven prefetch --------------------

Json::Value PrefetchLocalityReport::toJson() const
{
    Json::Value json;
    json["declaredWindows"] = static_cast<Json::UInt64>( declaredWindows );
    json["mergedReads"] = static_cast<Json::UInt64>( mergedReads );
    json["bytesPulled"] = static_cast<Json::UInt64>( bytesPulled );
    json["warmed"] = static_cast<Json::UInt64>( warmed );
    json["cacheHits"] = static_cast<Json::UInt64>( cacheHits );
    json["mirrorHits"] = static_cast<Json::UInt64>( mirrorHits );
    json["skippedBudget"] = static_cast<Json::UInt64>( skippedBudget );
    json["skippedCancel"] = static_cast<Json::UInt64>( skippedCancel );
    json["failed"] = static_cast<Json::UInt64>( failed );
    json["skippedNoOverview"] = static_cast<Json::UInt64>( skippedNoOverview );
    json["budgetExhausted"] = budgetExhausted;
    json["outcomesDropped"] = static_cast<Json::UInt64>( outcomesDropped );
    return json;
}

namespace
{

/// One merged read: an asset's source window that covers (a superset of)
/// the declared pattern's touches.
struct MergedRead
{
    std::size_t assetIndex = 0;
    int xOff = 0, yOff = 0, x1 = 0, y1 = 0;   // source pixel rect, half-open
    int band = 1;      // 12.0: 1-based band the read warms
    int overview = 0;  // 12.0: 1-based overview level (0 = native)
};

/// A touching-or-overlapping merge on the integer grid: two rects merge
/// when their expanded-by-one forms intersect (adjacency counts — sequential
/// device reads like their windows touching).
bool rectsTouchOrOverlap( const MergedRead &a, const MergedRead &b )
{
    return a.xOff <= b.x1 + 1 && b.xOff <= a.x1 + 1 && a.yOff <= b.y1 + 1 &&
           b.yOff <= a.y1 + 1;
}

void mergeRect( MergedRead &a, const MergedRead &b )
{
    a.xOff = std::min( a.xOff, b.xOff );
    a.yOff = std::min( a.yOff, b.yOff );
    a.x1 = std::max( a.x1, b.x1 );
    a.y1 = std::max( a.y1, b.y1 );
}

} // namespace

PrefetchLocalityReport prefetchAccessPattern( const VirtualCube &cube,
                                              const std::vector<AccessWindow> &pattern,
                                              const PrefetchOptions &options,
                                              const CancelToken &cancel )
{
    if ( !RemoteRangeCache::installed() )
        throw GeoError( ErrorCode::InvalidArgument,
                        "prefetch needs the range cache installed (/vsirangecache/)" );
    if ( !cube.grid().valid() )
        throw GeoError( ErrorCode::InvalidArgument, "cube grid is invalid — cannot prefetch" );

    PrefetchLocalityReport report;
    report.declaredWindows = pattern.size();

    // Map every declared window onto the intersecting assets (recorded
    // index facts only — no opens). Windows OUTSIDE the cube grid are
    // declared input, not errors: the mapping simply finds no asset.
    std::vector<MergedRead> reads;
    for ( std::size_t assetIndex = 0; assetIndex < cube.assets().size(); ++assetIndex )
    {
        const VirtualCubeAssetIndexEntry &entry = cube.assets()[assetIndex];
        if ( !entry.hasGrid )
            continue;   // no recorded grid facts — nothing to map without an open
        for ( const AccessWindow &window : pattern )
        {
            if ( window.w <= 0 || window.h <= 0 )
                continue;
            const double minX = cube.grid().minX + window.x * cube.grid().scaleX;
            const double maxX = cube.grid().minX + ( window.x + window.w ) * cube.grid().scaleX;
            const double maxY = cube.grid().maxY - window.y * cube.grid().scaleY;
            const double minY = cube.grid().maxY - ( window.y + window.h ) * cube.grid().scaleY;
            if ( !entry.record.hasBbox || entry.record.maxX <= minX ||
                 entry.record.minX >= maxX || entry.record.maxY <= minY ||
                 entry.record.minY >= maxY )
                continue;   // declared bbox never intersects: not consulted
            RasterMetadata facts;
            facts.width = entry.rasterWidth;
            facts.height = entry.rasterHeight;
            facts.hasGeotransform = true;
            facts.geotransform = { entry.assetMinX, entry.resX, 0.0,
                                   entry.assetMaxY, 0.0, entry.resY };
            const VirtualCubeSourceWindow mapped =
              virtualCubeSourceWindow( facts, minX, minY, maxX, maxY );
            if ( !mapped.ok )
                continue;
            MergedRead read;
            read.assetIndex = assetIndex;
            read.xOff = mapped.window.xOff;
            read.yOff = mapped.window.yOff;
            read.x1 = mapped.window.xOff + mapped.window.width - 1;
            read.y1 = mapped.window.yOff + mapped.window.height - 1;
            read.band = window.band > 0 ? window.band : 1;
            read.overview = window.overview > 0 ? window.overview : 0;
            reads.push_back( read );
        }
    }

    // Locality order: asset (selection order), then ascending y/x. Merge
    // touching/overlapping rects of the SAME asset (single pass over the
    // sorted touches; n = pattern size, bounded by the caller's queue).
    std::stable_sort( reads.begin(), reads.end(),
                      [ & ]( const MergedRead &a, const MergedRead &b ) {
                          if ( a.assetIndex != b.assetIndex )
                              return a.assetIndex < b.assetIndex;
                          // 12.0 progressive refinement: coarser overview
                          // levels warm BEFORE finer ones — a zoom-in
                          // trajectory always has its current zoom warm.
                          if ( a.overview != b.overview )
                              return a.overview > b.overview;
                          if ( a.yOff != b.yOff )
                              return a.yOff < b.yOff;
                          return a.xOff < b.xOff;
                      } );
    std::vector<MergedRead> merged;
    for ( const MergedRead &read : reads )
    {
        bool absorbed = false;
        for ( MergedRead &open : merged )
        {
            if ( open.assetIndex == read.assetIndex && open.band == read.band &&
                 open.overview == read.overview && rectsTouchOrOverlap( open, read ) )
            {
                mergeRect( open, read );
                absorbed = true;
                break;
            }
        }
        if ( !absorbed )
            merged.push_back( read );
    }
    report.mergedReads = merged.size();

    // Walk the merged reads: mirror skip, budget, warm read through the
    // cache spelling. One open per read; identity probes once per asset.
    std::uint64_t pulled = 0;
    const std::uint64_t budget = options.maxBytes;
    std::map<std::size_t, std::string> tokenByAsset;
    for ( std::size_t r = 0; r < merged.size(); ++r )
    {
        if ( cancel.cancelled() )
        {
            report.skippedCancel += merged.size() - r;
            break;
        }
        const MergedRead &read = merged[r];
        const VirtualCubeAssetIndexEntry &entry = cube.assets()[read.assetIndex];

        const int width = read.x1 - read.xOff + 1;
        const int height = read.y1 - read.yOff + 1;
        const RasterWindow window { read.xOff, read.yOff, width, height };

        // Budget BEFORE the open, against the read's declared estimate
        // (grid cells × 8 bytes — the same unit executeWindow uses). A
        // read that cannot fit the remaining budget is skipped honestly;
        // one fat read never blows the budget after the fact.
        // 12.0: an overview read pulls THAT LEVEL's bytes — the estimate is
        // decimated by the level factor (a coarse zoom is cheap; that is
        // the point of warming it first).
        const int estimateShift = 2 * std::min( read.overview, 30 );
        const std::uint64_t estimate =
          ( static_cast<std::uint64_t>( width ) * height * 8 ) >> estimateShift;
        if ( budget > 0 && pulled + estimate > budget )
        {
            ++report.skippedBudget;
            report.budgetExhausted = true;
            continue;
        }

        // Mirror coordination: a hit means the bytes are already local.
        // 12.0: skipped for overview-declared reads — the mirror holds
        // NATIVE band chunks only, so a same-window native hit would
        // misreport as "already local" and silently skip the overview warm.
        if ( !options.mirrorDirectory.empty() && read.overview == 0 )
        {
            try
            {
                std::string token;
                const auto tokenIt = tokenByAsset.find( read.assetIndex );
                if ( tokenIt != tokenByAsset.end() )
                    token = tokenIt->second;
                else
                {
                    // Offline index FIRST (zero network — the mirror knows
                    // the token it materialized under); probe only on an
                    // index miss.
                    MirrorIndexAssetFacts indexFacts;
                    if ( lookupMirrorAsset( options.mirrorDirectory, entry.record.path,
                                            indexFacts ) )
                        token = indexFacts.token;
                    if ( token.empty() )
                        token = fabricAssetIdentity( entry.record.path,
                                                     AssetIdentityOptions{} ).token;
                    tokenByAsset[read.assetIndex] = token;
                }
                if ( !token.empty() )
                {
                    const std::string bandSelector = "band" + std::to_string( read.band );
                    const std::string indexKey = fabricMirrorIndexKey( entry.record.path );
                    std::string skippedCorrupt;
                    std::string key =
                      fabricChunkMirrorKey( token, indexKey, window, bandSelector );
                    std::string hit =
                      resolveMirrorHit( options.mirrorDirectory, token, key, &skippedCorrupt );
                    if ( hit.empty() )
                    {
                        const std::string legacyKey =
                          fabricChunkMirrorKey( token, entry.record.path, window, bandSelector );
                        if ( legacyKey != key )
                            hit = resolveMirrorHit( options.mirrorDirectory, token, legacyKey,
                                                    &skippedCorrupt );
                    }
                    if ( !hit.empty() )
                    {
                        ++report.mirrorHits;
                        continue;
                    }
                }
            }
            catch ( const GeoError & )
            {
                // best-effort: fall through to the warm read
            }
        }

        try
        {
            RasterReader reader = RasterReader::open( fabricCachedPath( entry.record.path ) );
            const std::uintmax_t telemetryBefore =
              RemoteRangeCache::telemetryJson()["bytes_fetched"].asUInt64();
            if ( read.overview > 0 )
            {
                // 12.0 overview-aware warm: pull THAT level's blocks through
                // the cache. A level the source does not have is an honest
                // skip (never a silent native re-read — that would pull the
                // full-res bytes the caller was trying to avoid).
                const std::vector<int> dims = reader.overviewDimensions( read.band );
                // The dimensions vector can be SHORTER than 2×overviewCount
                // (null overview handles are skipped upstream) — bounds-check
                // the level before indexing it.
                if ( static_cast<std::size_t>( 2 * read.overview ) > dims.size() )
                {
                    ++report.skippedNoOverview;
                    continue;
                }
                const int ow = dims[2 * ( read.overview - 1 )];
                const int oh = dims[2 * ( read.overview - 1 ) + 1];
                // The level's scale against the full raster (the recorded
                // index-entry grid is the full-res extent).
                const int dstWidth = std::max( 1, static_cast<int>( std::lround(
                                                   window.width * ( ow / double( entry.rasterWidth ) ) )) );
                const int dstHeight = std::max( 1, static_cast<int>( std::lround(
                                                    window.height * ( oh / double( entry.rasterHeight ) ) )) );
                ( void )reader.readWindowResampled( { read.band }, window, dstWidth, dstHeight,
                                                    read.overview, OverviewPolicy::Exact,
                                                    "nearest" );
            }
            else
            {
                ( void )reader.readWindow( { read.band }, window,
                                           options.maxChunkBytes ? options.maxChunkBytes
                                                                 : 16ull * 1024 * 1024 );
            }
            const std::uintmax_t telemetryAfter =
              RemoteRangeCache::telemetryJson()["bytes_fetched"].asUInt64();
            const std::uint64_t chunkBytes =
              static_cast<std::uint64_t>( telemetryAfter - telemetryBefore );
            pulled += chunkBytes;
            report.bytesPulled += chunkBytes;
            if ( chunkBytes == 0 )
                ++report.cacheHits;
            else
                ++report.warmed;
        }
        catch ( const GeoError & )
        {
            ++report.failed;   // a failed warm is honest, never walk-aborting
        }
    }
    return report;
}

} // namespace sicnu::geo
