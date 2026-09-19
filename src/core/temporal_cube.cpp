/***************************************************************************
  core/temporal_cube.cpp
  Temporal Phenology Timeline Studio (D16) — TemporalCube implementation.
  ---------------------------
  See core/temporal_cube.h for the seam contract (ADR 0161).

  Out-of-core engine: pixels live on disk; composed 256×256 spatial tiles
  (one float plane per calendar slice, time-major) are held in an LRU pool
  capped at 10 tiles (and by the configured byte budget). Composing a tile
  reads, per calendar node, only the window-intersecting scenes — memory is
  O(tile × T), never O(raster × T).
 ***************************************************************************/

#include "core/temporal_cube.h"

#include "geospatial/common.h"
#include "geospatial/metadata/canonical_metadata.h"
#include "geospatial/raster/raster_reader.h"

#include <QDate>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <limits>
#include <list>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace sicnu::temporal
{

namespace
{

/// Parses the first valid date from @a text. Accepted shapes, in priority
/// order: YYYY-MM-DD, YYYYMMDD, YYYYDDD (ordinal). Anything else → invalid.
QDate parseIsoDate( const QString &text )
{
    static const QRegularExpression iso( QStringLiteral( "\\d{4}-\\d{2}-\\d{2}" ) );
    auto match = iso.match( text );
    if ( match.hasMatch() )
    {
        const QDate d = QDate::fromString( match.captured( 0 ), QStringLiteral( "yyyy-MM-dd" ) );
        if ( d.isValid() )
            return d;
    }
    // Try every 8-digit run (not only the first) so a leading non-date digit
    // blob does not hide a later yyyyMMdd token (#1097).
    static const QRegularExpression compact( QStringLiteral( "\\d{8}" ) );
    auto it = compact.globalMatch( text );
    while ( it.hasNext() )
    {
        match = it.next();
        const QDate d = QDate::fromString( match.captured( 0 ), QStringLiteral( "yyyyMMdd" ) );
        if ( d.isValid() )
            return d;
    }
    // #1097: ordinal YYYYDDD must be a standalone 7-digit token — never a
    // prefix/infix of a longer digit run (e.g. t12345678_....tif).
    static const QRegularExpression ordinal( QStringLiteral( "(?<!\\d)\\d{7}(?!\\d)" ) );
    match = ordinal.match( text );
    if ( match.hasMatch() )
    {
        const int year = match.captured( 0 ).left( 4 ).toInt();
        const int doy = match.captured( 0 ).mid( 4 ).toInt();
        if ( doy >= 1 && doy <= 366 )
        {
            const QDate candidate = QDate( year, 1, 1 ).addDays( doy - 1 );
            if ( candidate.isValid() && candidate.year() == year )
                return candidate;
        }
    }
    return QDate();
}

/// Acquisition instant of one scene: canonical metadata first, filename as
/// the documented fallback. Invalid date when neither source resolves.
QDate sceneInstant( const QString &path, const sicnu::geo::RasterMetadata &metadata )
{
    if ( !metadata.acquisitionTime.empty() )
    {
        const QDate fromMetadata =
            parseIsoDate( QString::fromStdString( metadata.acquisitionTime ) );
        if ( fromMetadata.isValid() )
            return fromMetadata;
    }
    return parseIsoDate( path );
}

/// CRS agreement: declared CRS must agree; undeclared CRS (invalid on either
/// side) cannot disagree, so it composites under the pixel-aligned contract.
bool crsAgrees( const sicnu::geo::CrsInfo &a, const sicnu::geo::CrsInfo &b )
{
    if ( !a.valid || !b.valid )
        return true;
    if ( !a.authid.empty() && !b.authid.empty() )
        return a.authid == b.authid;
    if ( !a.wkt.empty() && !b.wkt.empty() )
        return a.wkt == b.wkt;
    return true;
}

constexpr int kTileSize = 256;
/// ADR 0161: at most ten composed tiles stay resident.
constexpr std::size_t kMaxResidentTiles = 10;
/// No-fake-interpolation red line: a hole wider than this is NaN (D-160-8).
constexpr double kMaxHoleSpanDays = 45.0;

struct TileKey
{
    int x = 0;
    int y = 0;
    bool operator==( const TileKey &other ) const { return x == other.x && y == other.y; }
};

struct TileKeyHash
{
    std::size_t operator()( const TileKey &key ) const
    {
        return static_cast<std::size_t>( key.x ) * 73856093u ^
               static_cast<std::size_t>( key.y ) * 19349663u;
    }
};

class TemporalCubeImpl final : public TemporalCube
{
  public:
    std::vector<CalendarPoint> timeline() const override { return mTimeline; }
    int sliceCount() const override { return static_cast<int>( mTimeline.size() ); }
    int width() const override { return mWidth; }
    int height() const override { return mHeight; }

    std::vector<float> readChunk( int x, int y, int width, int height,
                                  int tStart, int tCount ) override
    {
        if ( !validSpatialWindow( x, y, width, height ) || tCount < 1 ||
             tStart < 0 || tStart + tCount > sliceCount() )
            return {};

        std::vector<float> out( static_cast<std::size_t>( tCount ) * width * height,
                                std::numeric_limits<float>::quiet_NaN() );
        const int tx0 = x / kTileSize;
        const int tx1 = ( x + width - 1 ) / kTileSize;
        const int ty0 = y / kTileSize;
        const int ty1 = ( y + height - 1 ) / kTileSize;
        for ( int ty = ty0; ty <= ty1; ++ty )
        {
            for ( int tx = tx0; tx <= tx1; ++tx )
            {
                const Tile &tile = ensureTile( tx, ty );
                // Intersect the request with this tile.
                const int rx0 = std::max( x, tx * kTileSize );
                const int rx1 = std::min( x + width, ( tx + 1 ) * kTileSize );
                const int ry0 = std::max( y, ty * kTileSize );
                const int ry1 = std::min( y + height, ( ty + 1 ) * kTileSize );
                for ( int k = 0; k < tCount; ++k )
                {
                    for ( int row = ry0; row < ry1; ++row )
                    {
                        const float *src = tileValuePtr( tile, rx0 - tx * kTileSize,
                                                         row - ty * kTileSize, tStart + k );
                        float *dst = out.data() +
                                     ( static_cast<std::size_t>( k ) * height + ( row - y ) ) *
                                         width +
                                     ( rx0 - x );
                        std::copy( src, src + ( rx1 - rx0 ), dst );
                    }
                }
            }
        }
        return out;
    }

    std::vector<float> extractPixelSeries( int x, int y ) override
    {
        if ( x < 0 || y < 0 || x >= mWidth || y >= mHeight )
            return {};
        const Tile &tile = ensureTile( x / kTileSize, y / kTileSize );
        std::vector<float> series( mTimeline.size() );
        for ( std::size_t k = 0; k < mTimeline.size(); ++k )
            series[k] = *tileValuePtr( tile, x % kTileSize, y % kTileSize, k );
        return series;
    }

    static std::shared_ptr<TemporalCubeImpl> open( const std::vector<QString> &scenePaths,
                                                   const TemporalCubeConfig &config,
                                                   QString *why );

    std::vector<CalendarPoint> mTimeline;
    int mWidth = 0;
    int mHeight = 0;
    double mMaxWindowDays = 32.0;
    CompositingStrategy mStrategy = CompositingStrategy::BestPixel;

    struct SceneSource
    {
        sicnu::geo::RasterReader reader;
        double tDays = 0.0;
        bool hasCloud = false;
        bool hasNoData = false;
        double noDataValue = 0.0;
    };
    std::vector<SceneSource> mScenes;

  private:
    struct Tile
    {
        std::vector<float> data; ///< [slice][row][col], sliceCount × tileH × tileW
        int width = 0;
        int height = 0;
    };

    using LruIterator = std::list<std::pair<TileKey, Tile>>::iterator;

    const Tile &ensureTile( int tx, int ty ) const
    {
        const TileKey key{ tx, ty };
        auto found = mIndex.find( key );
        if ( found != mIndex.end() )
        {
            mLru.splice( mLru.begin(), mLru, found->second ); // move to MRU
            return found->second->second;
        }

        Tile tile = composeTile( tx, ty );
        while ( !mLru.empty() && mLru.size() >= residentTileLimit() )
            evictLru();
        mLru.emplace_front( key, std::move( tile ) );
        mIndex[key] = mLru.begin();
        return mLru.front().second;
    }

    void evictLru() const
    {
        mIndex.erase( mLru.back().first );
        mLru.pop_back();
    }

    std::size_t residentTileLimit() const
    {
        const std::size_t tileBytes = static_cast<std::size_t>( kTileSize ) * kTileSize *
                                      mTimeline.size() * sizeof( float );
        const std::size_t byBudget = std::max<std::size_t>(
            1, mMemoryBudget / std::max<std::size_t>( tileBytes, 1 ) );
        return std::min( kMaxResidentTiles, byBudget );
    }

    const float *tileValuePtr( const Tile &tile, int col, int row, int slice ) const
    {
        const std::size_t plane = static_cast<std::size_t>( tile.width ) * tile.height;
        return tile.data.data() + plane * slice +
               static_cast<std::size_t>( row ) * tile.width + col;
    }

    /// Composes one spatial tile across the whole calendar (node by node,
    /// keeping only the running composite + per-scene window reads live).
    Tile composeTile( int tx, int ty ) const
    {
        Tile tile;
        tile.width = std::min( kTileSize, mWidth - tx * kTileSize );
        tile.height = std::min( kTileSize, mHeight - ty * kTileSize );
        tile.data.assign( static_cast<std::size_t>( tile.width ) * tile.height *
                              mTimeline.size(),
                          std::numeric_limits<float>::quiet_NaN() );
        if ( mScenes.empty() )
            return tile;

        const sicnu::geo::RasterWindow window{ tx * kTileSize, ty * kTileSize,
                                               tile.width, tile.height };
        const std::size_t pixels = static_cast<std::size_t>( tile.width ) * tile.height;
        const float nan = std::numeric_limits<float>::quiet_NaN();

        for ( std::size_t k = 0; k < mTimeline.size(); ++k )
        {
            const double t_k = mTimeline[k].tDays;
            float *outPlane = tile.data.data() + pixels * k;

            // No-fake-interpolation: hole span bracketing this node.
            double prev = -std::numeric_limits<double>::infinity();
            double next = std::numeric_limits<double>::infinity();
            for ( const SceneSource &scene : mScenes )
            {
                if ( scene.tDays <= t_k )
                    prev = std::max( prev, scene.tDays );
                if ( scene.tDays >= t_k )
                    next = std::min( next, scene.tDays );
            }
            const double holeSpan = ( prev > -std::numeric_limits<double>::infinity() &&
                                      next < std::numeric_limits<double>::infinity() )
                                        ? next - prev
                                        : std::numeric_limits<double>::infinity();
            if ( holeSpan > kMaxHoleSpanDays )
                continue; // plane stays NaN

            const double sigma = std::max( mMaxWindowDays, 0.5 ) / 2.0;
            if ( mStrategy == CompositingStrategy::BestPixel )
            {
                std::vector<float> bestQ( pixels, 0.0f );
                std::vector<float> bestV( pixels, nan );
                for ( const SceneSource &scene : mScenes )
                {
                    const double dt = scene.tDays - t_k;
                    if ( std::abs( dt ) > mMaxWindowDays )
                        continue;
                    const double gauss =
                        std::exp( -( dt * dt ) / ( 2.0 * sigma * sigma ) );
                    readScenePlane( scene, window, [&]( std::size_t i, double value, double cloud ) {
                        const double q = ( 1.0 - cloud ) * gauss;
                        // Fully-clouded observations carry Q = 0: they are
                        // NOT candidates (D-160-8) — same gate as WeightedMean.
                        if ( q > bestQ[i] && q > 0.0 )
                        {
                            bestQ[i] = static_cast<float>( q );
                            bestV[i] = static_cast<float>( value );
                        }
                    } );
                }
                for ( std::size_t i = 0; i < pixels; ++i )
                    if ( bestQ[i] > 0.0f )
                        outPlane[i] = bestV[i];
            }
            else
            {
                std::vector<float> sumQ( pixels, 0.0f );
                std::vector<float> sumQV( pixels, 0.0f );
                for ( const SceneSource &scene : mScenes )
                {
                    const double dt = scene.tDays - t_k;
                    if ( std::abs( dt ) > mMaxWindowDays )
                        continue;
                    const double gauss =
                        std::exp( -( dt * dt ) / ( 2.0 * sigma * sigma ) );
                    readScenePlane( scene, window, [&]( std::size_t i, double value, double cloud ) {
                        const double q = ( 1.0 - cloud ) * gauss;
                        if ( q > 0.0 )
                        {
                            sumQ[i] += static_cast<float>( q );
                            sumQV[i] += static_cast<float>( q * value );
                        }
                    } );
                }
                for ( std::size_t i = 0; i < pixels; ++i )
                    if ( sumQ[i] > 0.0f )
                        outPlane[i] = static_cast<float>( sumQV[i] ) / sumQ[i];
            }
        }
        return tile;
    }

    /// Streams one scene's window (band 1 measurement, band 2 cloud when
    /// present) through @a visit for every valid pixel. Values are stored
    /// values (no implicit scale/offset — RasterReader contract).
    template <typename Visit>
    void readScenePlane( const SceneSource &scene, const sicnu::geo::RasterWindow &window,
                         Visit &&visit ) const
    {
        std::vector<int> bands{ 1 };
        if ( scene.hasCloud )
            bands.push_back( 2 );
        std::vector<double> values;
        try
        {
            values = scene.reader.readWindow( bands, window );
        }
        catch ( const std::exception & )
        {
            return; // typed read failure: this scene contributes no candidates
        }
        const std::size_t pixels = static_cast<std::size_t>( window.width ) * window.height;
        const double *cloud = scene.hasCloud ? values.data() + pixels : nullptr;
        for ( std::size_t i = 0; i < pixels; ++i )
        {
            const double v = values[i];
            if ( !std::isfinite( v ) )
                continue;
            if ( scene.hasNoData && v == scene.noDataValue )
                continue;
            double c = 0.0;
            if ( cloud )
                c = std::min( 1.0, std::max( 0.0, cloud[i] ) );
            visit( i, v, c );
        }
    }

    mutable std::list<std::pair<TileKey, Tile>> mLru; // front = MRU
    mutable std::unordered_map<TileKey, LruIterator, TileKeyHash> mIndex;
    std::size_t mMemoryBudget = 1024ull * 1024 * 1024;

    bool validSpatialWindow( int x, int y, int width, int height ) const
    {
        return x >= 0 && y >= 0 && width >= 1 && height >= 1 && x + width <= mWidth &&
               y + height <= mHeight;
    }
};

std::shared_ptr<TemporalCubeImpl> TemporalCubeImpl::open( const std::vector<QString> &scenePaths,
                                                          const TemporalCubeConfig &config,
                                                          QString *why )
{
    const auto refuse = [why]( const QString &reason ) {
        if ( why )
            *why = reason;
        return std::shared_ptr<TemporalCubeImpl>();
    };

    if ( scenePaths.empty() )
        return refuse( QStringLiteral( "TemporalCube: empty scene list" ) );

    const QDate epoch = QDate::fromString( config.epochIsoDate, QStringLiteral( "yyyy-MM-dd" ) );
    if ( !epoch.isValid() )
        return refuse( QStringLiteral( "TemporalCube: invalid epoch '%1'" ).arg( config.epochIsoDate ) );
    if ( config.cadenceDays < 1 )
        return refuse( QStringLiteral( "TemporalCube: cadenceDays must be >= 1" ) );
    if ( config.maxWindowDays < 0.0 )
        return refuse( QStringLiteral( "TemporalCube: maxWindowDays must be >= 0" ) );

    const double cadence = static_cast<double>( config.cadenceDays );
    const double epochJulian = static_cast<double>( epoch.toJulianDay() );

    auto impl = std::make_shared<TemporalCubeImpl>();
    impl->mMaxWindowDays = config.maxWindowDays;
    impl->mStrategy = config.strategy;
    impl->mMemoryBudget = config.maxMemoryBudgetBytes;

    // Metadata-only pass: shape, instant, band layout — no pixel reads.
    double minDays = std::numeric_limits<double>::infinity();
    double maxDays = -std::numeric_limits<double>::infinity();
    for ( const QString &path : scenePaths )
    {
        sicnu::geo::RasterReader reader;
        try
        {
            reader = sicnu::geo::RasterReader::open( path.toStdString() );
        }
        catch ( const std::exception &e )
        {
            return refuse( QStringLiteral( "TemporalCube: cannot open scene '%1': %2" )
                               .arg( path, QString::fromUtf8( e.what() ) ) );
        }
        // Canonical metadata (bands, instant) comes from one inspect pass;
        // the reader handle below is the pixel channel.
        const sicnu::geo::RasterMetadata meta =
            sicnu::geo::inspectRaster( path.toStdString() );
        if ( meta.width < 1 || meta.height < 1 || meta.bandCount < 1 )
            return refuse( QStringLiteral( "TemporalCube: scene '%1' has an empty raster grid" )
                               .arg( path ) );
        if ( !meta.bands.empty() && meta.bands.front().dtype.size() > 0 &&
             meta.bands.front().dtype.front() == 'C' )
            return refuse( QStringLiteral( "TemporalCube: scene '%1' carries complex pixels, "
                                           "which have no radiometric compositing semantics" )
                               .arg( path ) );

        const QDate instant = sceneInstant( path, meta );
        if ( !instant.isValid() )
            return refuse( QStringLiteral( "TemporalCube: scene '%1' has no resolvable "
                                           "acquisition date (metadata or filename)" )
                               .arg( path ) );

        if ( impl->mWidth == 0 )
        {
            impl->mWidth = meta.width;
            impl->mHeight = meta.height;
        }
        else if ( meta.width != impl->mWidth || meta.height != impl->mHeight )
        {
            return refuse( QStringLiteral( "TemporalCube: scene '%1' grid %2x%3 disagrees "
                                           "with %4x%5" )
                               .arg( path )
                               .arg( meta.width )
                               .arg( meta.height )
                               .arg( impl->mWidth )
                               .arg( impl->mHeight ) );
        }
        if ( !crsAgrees( impl->mScenes.empty() ? meta.crs : impl->mScenes.front().reader.metadata().crs,
                         meta.crs ) )
            return refuse( QStringLiteral( "TemporalCube: scene '%1' CRS disagrees with the "
                                           "scene set" )
                               .arg( path ) );

        SceneSource source;
        source.reader = std::move( reader );
        source.tDays = static_cast<double>( instant.toJulianDay() ) - epochJulian;
        source.hasCloud = meta.bandCount >= 2;
        if ( meta.bands.empty() )
            return refuse( QStringLiteral( "TemporalCube: scene '%1' has no band metadata" )
                               .arg( path ) );
        source.hasNoData = meta.bands.front().hasNoData;
        source.noDataValue = meta.bands.front().noDataValue;
        const double sceneDays = source.tDays;
        impl->mScenes.push_back( std::move( source ) );

        minDays = std::min( minDays, sceneDays );
        maxDays = std::max( maxDays, sceneDays );
    }

    // Regular calendar: k = ceil((tMin − t0)/Δt) .. floor((tMax − t0)/Δt).
    const long kMin = static_cast<long>( std::ceil( minDays / cadence ) );
    const long kMax = static_cast<long>( std::floor( maxDays / cadence ) );
    if ( kMax < kMin )
        return refuse( QStringLiteral( "TemporalCube: scene span [%1, %2] contains no regular "
                                       "%3-day node" )
                           .arg( minDays )
                           .arg( maxDays )
                           .arg( config.cadenceDays ) );

    impl->mTimeline.reserve( static_cast<std::size_t>( kMax - kMin + 1 ) );
    for ( long k = kMin; k <= kMax; ++k )
    {
        CalendarPoint point;
        point.tDays = static_cast<double>( k ) * cadence;
        point.isoDate = epoch.addDays( static_cast<int>( k ) * config.cadenceDays )
                            .toString( QStringLiteral( "yyyy-MM-dd" ) );
        impl->mTimeline.push_back( point );
    }
    return impl;
}

} // namespace

std::shared_ptr<TemporalCube> TemporalCube::open( const std::vector<QString> &scenePaths,
                                                  const TemporalCubeConfig &config,
                                                  QString *why )
{
    return TemporalCubeImpl::open( scenePaths, config, why );
}

} // namespace sicnu::temporal
