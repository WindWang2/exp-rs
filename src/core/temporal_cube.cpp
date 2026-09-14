/***************************************************************************
  core/temporal_cube.cpp
  Temporal Phenology Timeline Studio (D16) — TemporalCube implementation.
  ---------------------------
  See core/temporal_cube.h for the seam contract (ADR 0161).
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
#include <string>

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
    static const QRegularExpression compact( QStringLiteral( "\\d{8}" ) );
    match = compact.match( text );
    if ( match.hasMatch() )
    {
        const QDate d = QDate::fromString( match.captured( 0 ), QStringLiteral( "yyyyMMdd" ) );
        if ( d.isValid() )
            return d;
    }
    static const QRegularExpression ordinal( QStringLiteral( "\\d{7}" ) );
    match = ordinal.match( text );
    if ( match.hasMatch() )
    {
        const int year = match.captured( 0 ).left( 4 ).toInt();
        const int doy = match.captured( 0 ).mid( 4 ).toInt();
        const QDate candidate = QDate( year, 1, 1 ).addDays( doy - 1 );
        if ( candidate.isValid() && candidate.year() == year )
            return candidate;
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

class TemporalCubeImpl final : public TemporalCube
{
  public:
    std::vector<CalendarPoint> timeline() const override { return mTimeline; }
    int sliceCount() const override { return static_cast<int>( mTimeline.size() ); }
    int width() const override { return mWidth; }
    int height() const override { return mHeight; }

    std::vector<float> readChunk( int, int, int, int, int, int ) override { return {}; }
    std::vector<float> extractPixelSeries( int, int ) override
    {
        return std::vector<float>( mTimeline.size(), std::numeric_limits<float>::quiet_NaN() );
    }

    std::vector<CalendarPoint> mTimeline;
    int mWidth = 0;
    int mHeight = 0;
};

} // namespace

std::shared_ptr<TemporalCube> TemporalCube::open( const std::vector<QString> &scenePaths,
                                                  const TemporalCubeConfig &config,
                                                  QString *why )
{
    const auto refuse = [why]( const QString &reason ) {
        if ( why )
            *why = reason;
        return std::shared_ptr<TemporalCube>();
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

    // Metadata-only pass: shape, instant, band layout — no pixel reads.
    double minDays = std::numeric_limits<double>::infinity();
    double maxDays = -std::numeric_limits<double>::infinity();
    int width = 0;
    int height = 0;
    sicnu::geo::CrsInfo crs;
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
        const sicnu::geo::RasterMetadata &meta = reader.metadata();
        if ( meta.width < 1 || meta.height < 1 || meta.bandCount < 1 )
            return refuse( QStringLiteral( "TemporalCube: scene '%1' has an empty raster grid" )
                               .arg( path ) );

        const QDate instant = sceneInstant( path, meta );
        if ( !instant.isValid() )
            return refuse( QStringLiteral( "TemporalCube: scene '%1' has no resolvable "
                                           "acquisition date (metadata or filename)" )
                               .arg( path ) );

        if ( width == 0 )
        {
            width = meta.width;
            height = meta.height;
            crs = meta.crs;
        }
        else
        {
            if ( meta.width != width || meta.height != height )
                return refuse( QStringLiteral( "TemporalCube: scene '%1' grid %2x%3 disagrees "
                                               "with %4x%5" )
                                   .arg( path )
                                   .arg( meta.width )
                                   .arg( meta.height )
                                   .arg( width )
                                   .arg( height ) );
            if ( !crsAgrees( crs, meta.crs ) )
                return refuse( QStringLiteral( "TemporalCube: scene '%1' CRS disagrees with the "
                                               "scene set" )
                                   .arg( path ) );
        }

        const double days = static_cast<double>( instant.toJulianDay() ) - epochJulian;
        minDays = std::min( minDays, days );
        maxDays = std::max( maxDays, days );
        reader.close();
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

    auto impl = std::make_shared<TemporalCubeImpl>();
    impl->mWidth = width;
    impl->mHeight = height;
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

} // namespace sicnu::temporal
