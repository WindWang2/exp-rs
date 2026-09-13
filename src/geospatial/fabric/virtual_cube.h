/***************************************************************************
  geospatial/fabric/virtual_cube.h
  Cloud-Native Data Fabric / Data Cube 10.0 — virtual mosaic / time cube.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  A lazy logical raster cube over SEPARATE scene assets (the usual cloud-EO
  shape: one COG per acquisition). The cube owns:

    * a bounded asset index (bbox + instant per asset) built WITHOUT opening
      pixel data;
    * one negotiated output grid (explicit, or deterministically derived
      from a bounded metadata probe — DECISIONS D-1005/D-1007);
    * on-demand window reads: each window is answered by the intersecting
      assets under a deterministic overlap policy, with per-tile provenance
      back to the source asset (asset id + source pixel window);
    * NO eager mosaic: nothing reads pixels until readWindow is called, and
      each read materializes only the requested window.

  Determinism: the same spec + the same assets in the same order answer the
  same window with the same values, byte for byte. Overlap resolution is
  FirstWins over the selection order (DECISIONS D-1006) — the selection
  order itself is the caller's (or the quality policy's) declared intent.

  Bounds & errors: assets that fail to open during a read are recorded in
  the provenance (failed=true + typed error text) and the window is still
  answered from the remaining assets — a plan is honest, an execution can
  degrade (GOAL Autonomy defaults #2). A window answered from NO asset has
  every pixel NoData and says so. Cross-CRS grids are a typed refusal
  (Unsupported), never a silent warp (DECISIONS D-1005).
 ***************************************************************************/

#ifndef SICNU_GEOSPATIAL_FABRIC_VIRTUAL_CUBE_H
#define SICNU_GEOSPATIAL_FABRIC_VIRTUAL_CUBE_H

#include "geospatial/catalog/asset_query.h"
#include "geospatial/common.h"
#include "geospatial/fabric/catalog_service.h"
#include "geospatial/metadata/canonical_metadata.h"
#include "geospatial/raster/raster_reader.h"
#include "geospatial/util/time_normalization.h"

#include <json/json.h>

#include <cstdint>
#include <string>
#include <vector>

namespace sicnu::geo
{

/// The cube's output grid. explicit=true requires a valid CRS + positive
/// scales + a non-empty extent; otherwise one is negotiated (see build).
struct VirtualCubeGrid
{
    bool explicitGrid = false;
    CrsInfo crs;
    double scaleX = 0.0;             ///< positive (pixel size), Y sign follows
    double scaleY = 0.0;             ///< the CRS convention (negated when set)
    double minX = 0.0, minY = 0.0, maxX = 0.0, maxY = 0.0;

    bool valid() const;
    int width() const;               ///< round((maxX-minX)/|scaleX|) — throws when invalid
    int height() const;
    Json::Value toJson() const;
    static VirtualCubeGrid fromJson( const Json::Value &json );
};

enum class OverlapPolicy
{
    FirstWins,   ///< first asset in selection order writes; later assets fill
                 ///< NoData holes only (DECISIONS D-1006)
};

/// Build-time bounds (namespace scope: a nested-type default argument would
/// need the incomplete enclosing class).
struct VirtualCubeBuildOptions
{
    int probeLimit = 256;       ///< hard cap on metadata opens (bounds IO)
};

const char *overlapPolicyName( OverlapPolicy policy );
OverlapPolicy overlapPolicyFromName( const std::string &name ); ///< throws

/// Deterministic quality ordering applied to the caller's asset list before
/// indexing (DECISIONS D-1007). disable both keys to keep the input order.
struct VirtualCubeQuality
{
    bool byCloudCoverAscending = true;  ///< undeclared cloud cover sorts LAST
    bool byNewestFirst = true;          ///< datetimeUtc descending; undated LAST
    /// Ties break by (id ascending, input order) — total, stable order.
};

/// Per-asset read options for window execution.
struct VirtualCubeReadOptions
{
    /// 1-based band index INSIDE each source asset. Every asset must carry
    /// it (validated lazily per open; failing assets are provenance-logged).
    int bandIndex = 1;
    /// When non-empty, resolve each asset's band by canonical band role
    /// instead of a fixed index (falls back to bandIndex when a role match
    /// is absent; the resolution lands in provenance).
    std::string bandRole;
    /// Optional mirror directory: when a chunk mirror hit exists for the
    /// asset's identity token, read the mirrored local file instead of the
    /// remote path (fabric/mirror contract). "" disables mirror preference.
    std::string mirrorDirectory;
    /// Byte budget per source window read (RasterReader::readWindow budget;
    /// a breach is a per-asset failure recorded in provenance, not a global
    /// abort — one fat asset cannot kill a mosaic window).
    std::size_t maxWindowBytes = 256ull * 1024 * 1024;
};

/// One contributing (or failed) asset of a window read.
struct VirtualCubeProvenance
{
    std::string assetId;
    std::string assetPath;        ///< resolved fetchable path (redact for display)
    std::string instantUtc;       ///< the asset's effective instant ("" undated)
    RasterWindow sourceWindow;    ///< source pixel window that fed the output
    double targetMinX = 0.0, targetMinY = 0.0, targetMaxX = 0.0, targetMaxY = 0.0;
    bool contributed = false;     ///< wrote at least one output pixel
    bool failed = false;          ///< open/read failed (errorText carries why)
    std::string errorText;
    std::string mirrorHit;        ///< non-empty when served from this local mirror file

    Json::Value toJson() const;
};

struct VirtualCubeWindowResult
{
    int width = 0, height = 0;
    /// Stored values (band-sequential, single band), NoData-filled where no
    /// asset covered. The NoData VALUE is gridNoData below — never an
    /// implicit zero.
    std::vector<double> values;
    double gridNoData = 0.0;
    bool noDataIsNaN = false;
    /// Every asset consulted for this window (contributing, skipped-empty,
    /// or failed) in selection order.
    std::vector<VirtualCubeProvenance> provenance;

    Json::Value provenanceJson() const;
};

/// One indexed asset (bbox/instant facts + lazily-captured native grid).
struct VirtualCubeAssetIndexEntry
{
    AssetRecord record;
    std::string identityToken;    ///< "" = unprovable (never mirror-cached)
    std::string instantUtc;       ///< effective instant ("" undated)
    // Native grid facts, captured by the bounded build probe (probed=false
    // when the probe budget passed this asset by):
    bool probed = false;
    bool readable = false;        ///< metadata open succeeded
    std::string probeError;       ///< typed error text when !readable
    bool hasGrid = false;         ///< geotransform + size present
    int rasterWidth = 0, rasterHeight = 0;
    double resX = 0.0, resY = 0.0;
    double assetMinX = 0.0, assetMinY = 0.0, assetMaxX = 0.0, assetMaxY = 0.0;
    std::string epsgAuthid;       ///< "EPSG:xxxx" when the asset declares one
};

class VirtualCube
{
  public:
    /// Builds the cube: validates + orders + indexes the spec's assets, and
    /// (when grid.explicitGrid is false) negotiates a grid from a bounded
    /// metadata probe (at most probeLimit opens — the highest-resolution
    /// asset wins; ties break by (authid, scale, id)). Throws:
    ///   GeoError(InvalidArgument)  — empty asset list, invalid explicit grid
    ///   GeoError(Unsupported)      — grid needed but not derivable (every
    ///                                probed asset lacks resolution facts)
    ///   GeoError(Cancelled)        — cancel fired during the probe
    static VirtualCube build( const std::vector<AssetRecord> &assets, const VirtualCubeGrid &grid,
                              OverlapPolicy overlap, const VirtualCubeQuality &quality,
                              const VirtualCubeBuildOptions &options = {},
                              const CancelToken &cancel = {} );

    const VirtualCubeGrid &grid() const { return mGrid; }
    OverlapPolicy overlap() const { return mOverlap; }
    std::size_t assetCount() const { return mAssets.size(); }
    const std::vector<VirtualCubeAssetIndexEntry> &assets() const { return mAssets; }

    /// Bounded cube description (axes, grid, coverage, per-asset summary) —
    /// JSON-stable, provenance-safe (display forms only).
    Json::Value describeJson() const;

    /// Reads one grid-pixel window. Window must be inside the grid extent
    /// (GeoError(InvalidArgument) otherwise — clamp explicitly if wanted).
    /// Each intersecting indexed asset is mapped to source pixels and read;
    /// NoData fills uncovered cells. Cross-CRS assets are per-asset failures
    /// (Unsupported recorded in provenance) unless the grid was derived from
    /// them. Memory is O(window) — never O(cube).
    VirtualCubeWindowResult readWindow( int xOff, int yOff, int width, int height,
                                        const VirtualCubeReadOptions &options = {},
                                        const CancelToken &cancel = {} ) const;

  private:
    VirtualCube() = default;
    VirtualCubeGrid mGrid;
    OverlapPolicy mOverlap = OverlapPolicy::FirstWins;
    std::vector<VirtualCubeAssetIndexEntry> mAssets;   ///< selection order
};

} // namespace sicnu::geo

#endif // SICNU_GEOSPATIAL_FABRIC_VIRTUAL_CUBE_H
