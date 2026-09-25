// src/verify_adapters/gdal_grid_probe.cpp
#include "verify_adapters/gdal_grid_probe.h"

#include "geospatial/gdal_guard.h"
#include "geospatial/metadata/canonical_metadata.h"
#include "geospatial/raster/raster_reader.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace sicnu::verify_adapters
{

GdalGridProbe::GdalGridProbe( const int samplingAxis ) : mSamplingAxis( samplingAxis )
{
    sicnu::geo::ensureGdalRegistered();
}

namespace
{

/// Up to three distinct window origins per axis {0, middle, end}, deduped
/// for rasters smaller than two windows.
std::vector<int> windowOrigins( const int extent, const int window )
{
    std::vector<int> origins{ 0, ( extent - window ) / 2, extent - window };
    std::sort( origins.begin(), origins.end() );
    origins.erase( std::unique( origins.begin(), origins.end() ), origins.end() );
    return origins;
}

} // namespace

std::optional<sicnu::verify::GridInfo> GdalGridProbe::grid( const std::string &path )
{
    if ( path.empty() || mSamplingAxis <= 0 )
        return std::nullopt;

    sicnu::geo::RasterReader reader;
    try
    {
        reader = sicnu::geo::RasterReader::open( path );
    }
    catch ( ... )
    {
        // Open failure is the probe's declared "cannot answer": missing
        // file, foreign/remote source, VRT with dead sources — the engine
        // renders it Indeterminate, never a guess.
        return std::nullopt;
    }
    if ( !reader.isOpen() )
        return std::nullopt;

    const sicnu::geo::RasterMetadata &metadata = reader.metadata();
    sicnu::verify::GridInfo info;
    info.width = metadata.width;
    info.height = metadata.height;
    info.bandCount = metadata.bandCount;
    // Stable cross-platform identity only: the authority code ("EPSG:4326").
    // WKT text varies across GDAL/PROJ versions and would destabilize every
    // report digest that carried it; an authority-less CRS projects "".
    info.crs = metadata.crs.valid ? metadata.crs.authid : std::string{};

    if ( metadata.width <= 0 || metadata.height <= 0 || metadata.bandCount <= 0 )
    {
        // A degenerate but openable grid: structural facts are real; the
        // fractions have no pixels to speak of and stay at 0.
        return info;
    }

    const sicnu::geo::BandInfo &band = metadata.bands.front();
    const int windowW = std::min( metadata.width, mSamplingAxis );
    const int windowH = std::min( metadata.height, mSamplingAxis );

    std::size_t sampled = 0;
    std::size_t nodata = 0;
    std::size_t nonFinite = 0;
    for ( const int yOff : windowOrigins( metadata.height, windowH ) )
    {
        for ( const int xOff : windowOrigins( metadata.width, windowW ) )
        {
            const sicnu::geo::RasterWindow window{ xOff, yOff, windowW, windowH };
            try
            {
                const std::vector<double> values = reader.readWindow( { 1 }, window );
                for ( const double value : values )
                {
                    ++sampled;
                    if ( !std::isfinite( value ) )
                        ++nonFinite;
                    else if ( sicnu::geo::bandSentinelMatches( band, value ) )
                        ++nodata;
                }
            }
            catch ( ... )
            {
                // Sampling is the probe's contract; a grid whose band-1
                // window cannot be read (complex types, VRT window failure)
                // cannot answer with fractions — and a NaN fraction would
                // poison the report's canonical digest. Refuse loudly.
                return std::nullopt;
            }
        }
    }

    if ( sampled == 0 )
        return std::nullopt;
    info.nodataFraction = static_cast<double>( nodata ) / static_cast<double>( sampled );
    info.finiteFraction =
        static_cast<double>( sampled - nonFinite ) / static_cast<double>( sampled );
    return info;
}

} // namespace sicnu::verify_adapters
