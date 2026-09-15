// mosaic_plan.cpp — F15 Package A implementation (ADR 0163).
#include "mosaic_plan.h"

#include <ogr_spatialref.h>
#include <ogr_srs_api.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace rs::mosaic {
namespace {

constexpr double kRotationEps = 1e-12;

OGRSpatialReference* importCrs(const std::string& crs)
{
    if ( crs.empty() )
        return nullptr;
    auto *srs = new OGRSpatialReference();
    if ( srs->SetFromUserInput( crs.c_str() ) != OGRERR_NONE )
    {
        srs->Release();
        return nullptr;
    }
    return srs;
}

struct Rect
{
    double minX = 0, minY = 0, maxX = 0, maxY = 0;
};

// Axis-aligned footprint in the scene's own CRS (pixel-corner convention,
// consistent with rs_mosaic_operator).
Rect sceneRect( const SceneEntry& s )
{
    const auto &gt = s.geoTransform;
    Rect r;
    r.minX = std::min( gt[0], gt[0] + s.width * gt[1] );
    r.maxX = std::max( gt[0], gt[0] + s.width * gt[1] );
    r.minY = std::min( gt[3], gt[3] + s.height * gt[5] );
    r.maxY = std::max( gt[3], gt[3] + s.height * gt[5] );
    return r;
}

bool gridRectIntersection( const ScenePlacement &p, int w, int h,
                           const ScenePlacement &q, int qw, int qh,
                           int64_t *outPixels )
{
    const int64_t ix0 = std::max<int64_t>( p.offsetX, q.offsetX );
    const int64_t iy0 = std::max<int64_t>( p.offsetY, q.offsetY );
    const int64_t ix1 = std::min<int64_t>( p.offsetX + w, q.offsetX + qw );
    const int64_t iy1 = std::min<int64_t>( p.offsetY + h, q.offsetY + qh );
    if ( ix0 >= ix1 || iy0 >= iy1 )
    {
        *outPixels = 0;
        return false;
    }
    *outPixels = ( ix1 - ix0 ) * ( iy1 - iy0 );
    return true;
}

} // namespace

bool MosaicPlanner::sameCrs( const std::string &wktA, const std::string &wktB )
{
    if ( wktA == wktB )
        return true;
    OGRSpatialReference a, b;
    if ( a.SetFromUserInput( wktA.c_str() ) != OGRERR_NONE ||
         b.SetFromUserInput( wktB.c_str() ) != OGRERR_NONE )
        return false;
    return a.IsSame( &b ) != 0;
}

std::optional<std::array<double, 4>>
MosaicPlanner::footprintEnvelope( const SceneEntry &scene, const std::string &targetCrs )
{
    OGRSpatialReference *src = importCrs( scene.crsWkt );
    OGRSpatialReference *dst = importCrs( targetCrs );
    if ( !src || !dst )
    {
        if ( src ) src->Release();
        if ( dst ) dst->Release();
        return std::nullopt;
    }
    OGRCoordinateTransformation *ct = OGRCreateCoordinateTransformation( src, dst );
    src->Release();
    dst->Release();
    if ( !ct )
        return std::nullopt;

    const Rect r = sceneRect( scene );
    const double xs[4] = { r.minX, r.maxX, r.minX, r.maxX };
    const double ys[4] = { r.minY, r.minY, r.maxY, r.maxY };
    if ( !ct->Transform( 4, const_cast<double*>( xs ), const_cast<double*>( ys ) ) )
    {
        OGRCoordinateTransformation::DestroyCT( ct );
        return std::nullopt;
    }
    OGRCoordinateTransformation::DestroyCT( ct );

    std::array<double, 4> env {
        std::min( { xs[0], xs[1], xs[2], xs[3] } ),
        std::min( { ys[0], ys[1], ys[2], ys[3] } ),
        std::max( { xs[0], xs[1], xs[2], xs[3] } ),
        std::max( { ys[0], ys[1], ys[2], ys[3] } ),
    };
    for ( double v : env )
    {
        if ( !std::isfinite( v ) )
            return std::nullopt;
    }
    return env;
}

std::optional<MosaicPlan>
MosaicPlanner::build( const std::vector<SceneEntry> &scenes, const Options &options,
                      std::string *errorMessage )
{
    auto fail = [errorMessage]( const std::string &msg ) {
        if ( errorMessage )
            *errorMessage = msg;
        return std::nullopt;
    };

    if ( scenes.empty() )
        return fail( "MosaicPlan: no scenes provided" );

    for ( size_t i = 0; i < scenes.size(); ++i )
    {
        const SceneEntry &s = scenes[i];
        if ( s.width <= 0 || s.height <= 0 )
            return fail( "MosaicPlan: scene " + std::to_string( i ) + " (" + s.path +
                         ") has non-positive extent" );
        for ( double v : s.geoTransform )
        {
            if ( !std::isfinite( v ) )
                return fail( "MosaicPlan: scene " + std::to_string( i ) + " (" + s.path +
                             ") has a non-finite geotransform" );
        }
        if ( std::abs( s.geoTransform[1] ) < 1e-15 || std::abs( s.geoTransform[5] ) < 1e-15 )
            return fail( "MosaicPlan: scene " + std::to_string( i ) + " (" + s.path +
                         ") has zero pixel size" );
    }
    if ( options.referenceIndex >= static_cast<int>( scenes.size() ) )
        return fail( "MosaicPlan: reference index out of range" );

    // Reference scene: explicit, else highest priority then lowest input index.
    int refIdx = options.referenceIndex;
    if ( refIdx < 0 )
    {
        refIdx = 0;
        for ( int i = 1; i < static_cast<int>( scenes.size() ); ++i )
        {
            if ( scenes[i].priority > scenes[refIdx].priority )
                refIdx = i;
        }
    }

    MosaicPlan plan;
    plan.scenes.resize( scenes.size() );
    for ( size_t i = 0; i < scenes.size(); ++i )
        plan.scenes[i].scene = scenes[i];

    const SceneEntry &ref = plan.scenes[refIdx].scene;
    plan.crsWkt = ref.crsWkt;
    const double refPxX = ref.geoTransform[1];
    const double refPxY = ref.geoTransform[5];
    const bool northUp = refPxY < 0;

    // Per-scene classification.
    for ( size_t i = 0; i < plan.scenes.size(); ++i )
    {
        ScenePlanEntry &e = plan.scenes[i];
        const SceneEntry &s = e.scene;
        const auto &gt = s.geoTransform;

        const bool rotated = std::abs( gt[2] ) > kRotationEps || std::abs( gt[4] ) > kRotationEps;
        if ( rotated )
            plan.diagnostics.rotated.push_back( static_cast<int>( i ) );

        // Unknown (empty) reference CRS matches only unknown scene CRS:
        // both sides unknown share the "no CRS declared" grid.
        e.crsMatchesPlan = ( i == static_cast<size_t>( refIdx ) ) ||
                           ( plan.crsWkt.empty()
                                 ? s.crsWkt.empty()
                                 : sameCrs( s.crsWkt, plan.crsWkt ) );
        if ( !e.crsMatchesPlan )
            plan.diagnostics.crsMismatch.push_back( static_cast<int>( i ) );

        const bool sizeMatch =
            std::abs( std::abs( gt[1] ) - std::abs( refPxX ) ) <=
                options.pixelSizeTolerance * std::abs( refPxX ) &&
            std::abs( std::abs( gt[5] ) - std::abs( refPxY ) ) <=
                options.pixelSizeTolerance * std::abs( refPxY );
        if ( !sizeMatch )
            plan.diagnostics.pixelSizeMismatch.push_back( static_cast<int>( i ) );

        // Y-axis orientation must agree with the reference grid (mirroring guard).
        const bool yDirMatch = ( gt[5] * refPxY > 0 ) || rotated; // rotated already flagged
        if ( !yDirMatch )
            plan.diagnostics.yDirectionMismatch.push_back( static_cast<int>( i ) );
        e.gridEligible = !rotated && e.crsMatchesPlan && sizeMatch && yDirMatch;
    }

    // Union extent over grid-eligible scenes; mixed-CRS scenes contribute a
    // transformed footprint envelope to the extent diagnostic only.
    double minX = std::numeric_limits<double>::max();
    double minY = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest();
    double maxY = std::numeric_limits<double>::lowest();
    bool anyEligible = false;
    for ( const ScenePlanEntry &e : plan.scenes )
    {
        if ( !e.gridEligible )
            continue;
        const Rect r = sceneRect( e.scene );
        minX = std::min( minX, r.minX );
        minY = std::min( minY, r.minY );
        maxX = std::max( maxX, r.maxX );
        maxY = std::max( maxY, r.maxY );
        anyEligible = true;
    }
    if ( !anyEligible )
        return fail( "MosaicPlan: no grid-eligible scene shares the reference CRS/pixel grid "
                     "(see diagnostics); reproject/resample the inputs onto the reference grid first" );

    const int64_t w64 = static_cast<int64_t>( std::llround( ( maxX - minX ) / std::abs( refPxX ) ) );
    const int64_t h64 = static_cast<int64_t>( std::llround( ( maxY - minY ) / std::abs( refPxY ) ) );
    if ( w64 <= 0 || h64 <= 0 || w64 > std::numeric_limits<int>::max() ||
         h64 > std::numeric_limits<int>::max() )
        return fail( "MosaicPlan: computed grid extent is invalid (" + std::to_string( w64 ) + "x" +
                     std::to_string( h64 ) + ")" );
    plan.width = static_cast<int>( w64 );
    plan.height = static_cast<int>( h64 );

    std::array<double, 6> gt {};
    gt[0] = minX;
    gt[1] = refPxX;
    gt[2] = 0.0;
    gt[3] = northUp ? maxY : minY;
    gt[4] = 0.0;
    gt[5] = refPxY;
    plan.gridTransform = gt;

    // Placements.
    for ( size_t i = 0; i < plan.scenes.size(); ++i )
    {
        ScenePlanEntry &e = plan.scenes[i];
        if ( !e.gridEligible )
            continue;
        const Rect r = sceneRect( e.scene );
        const double subX = ( r.minX - minX ) / std::abs( refPxX );
        const double subY = northUp ? ( maxY - r.maxY ) / std::abs( refPxY )
                                    : ( r.minY - minY ) / std::abs( refPxY );
        const double fracX = std::abs( subX - std::round( subX ) );
        const double fracY = std::abs( subY - std::round( subY ) );
        e.placement.offsetX = static_cast<int64_t>( std::llround( subX ) );
        e.placement.offsetY = static_cast<int64_t>( std::llround( subY ) );
        e.placement.aligned = fracX <= options.subPixelEpsilon && fracY <= options.subPixelEpsilon;
        e.placement.fracOffsetX = fracX;
        e.placement.fracOffsetY = fracY;
        e.placement.valid = true;
        if ( !e.placement.aligned )
            plan.diagnostics.subPixelOffset.push_back( static_cast<int>( i ) );
    }

    // Composite order: all scenes, back-to-front paint order = priority
    // ascending (lowest priority painted first), ties by input index.
    plan.compositeOrder.resize( scenes.size() );
    for ( size_t i = 0; i < scenes.size(); ++i )
        plan.compositeOrder[i] = static_cast<int>( i );
    std::stable_sort( plan.compositeOrder.begin(), plan.compositeOrder.end(),
                      [&scenes]( int a, int b ) {
                          return scenes[a].priority < scenes[b].priority;
                      } );

    // Overlap inventory in composite-order space, grid-eligible pairs only.
    for ( size_t ai = 0; ai < plan.compositeOrder.size(); ++ai )
    {
        for ( size_t bi = ai + 1; bi < plan.compositeOrder.size(); ++bi )
        {
            const int ia = plan.compositeOrder[ai];
            const int ib = plan.compositeOrder[bi];
            const ScenePlanEntry &ea = plan.scenes[ia];
            const ScenePlanEntry &eb = plan.scenes[ib];
            if ( !ea.gridEligible || !eb.gridEligible )
                continue;
            int64_t pixels = 0;
            if ( gridRectIntersection( ea.placement, ea.scene.width, ea.scene.height,
                                       eb.placement, eb.scene.width, eb.scene.height,
                                       &pixels ) && pixels > 0 )
            {
                plan.overlaps.push_back( { ia, ib, pixels } );
            }
        }
    }

    // Actionable warnings, one per condition class, naming scene indices.
    auto nameList = []( const std::vector<int> &idx ) {
        std::string s;
        for ( size_t i = 0; i < idx.size(); ++i )
        {
            if ( i ) s += ", ";
            s += std::to_string( idx[i] );
        }
        return s;
    };
    if ( !plan.diagnostics.crsMismatch.empty() )
        plan.diagnostics.warnings.push_back(
            "CRS mismatch vs plan CRS for input(s) [" + nameList( plan.diagnostics.crsMismatch ) +
            "]; reproject onto the reference grid (gdal reproject) before mosaicking" );
    if ( !plan.diagnostics.rotated.empty() )
        plan.diagnostics.warnings.push_back(
            "rotated/sheared raster(s) [" + nameList( plan.diagnostics.rotated ) +
            "]; orthorectify before mosaicking" );
    if ( !plan.diagnostics.pixelSizeMismatch.empty() )
        plan.diagnostics.warnings.push_back(
            "pixel size mismatch for input(s) [" + nameList( plan.diagnostics.pixelSizeMismatch ) +
            "]; resample to the reference resolution before mosaicking" );
    if ( !plan.diagnostics.yDirectionMismatch.empty() )
        plan.diagnostics.warnings.push_back(
            "Y-axis orientation mismatch (mirrored/south-up raster) for input(s) [" +
            nameList( plan.diagnostics.yDirectionMismatch ) +
            "]; flip/reproject to the reference orientation before mosaicking" );
    if ( !plan.diagnostics.subPixelOffset.empty() )
        plan.diagnostics.warnings.push_back(
            "sub-pixel grid offset for input(s) [" + nameList( plan.diagnostics.subPixelOffset ) +
            "]; offsets are snapped to the nearest pixel (up to half a pixel of misregistration)" );

    if ( errorMessage )
        errorMessage->clear();
    return plan;
}

} // namespace rs::mosaic
