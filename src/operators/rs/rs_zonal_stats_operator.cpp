/***************************************************************************
 * rs_zonal_stats_operator.cpp — per-zone raster statistics through the
 * shared windowed rasterization seam (Scientific Processing 8.0, package D).
 ***************************************************************************/
#include "rs_zonal_stats_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "operators/rs/rs_raster_vector.h"
#include "geospatial/crs/crs_policy.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QFile>
#include <QTextStream>

#include <gdal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

constexpr int kWindowDim = 256;
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
// Shared median-collection budget (floats across ALL zones/bands). Zones
// whose collection is cut short are flagged — their streaming stats stay
// exact. Deterministic: collection follows feature/window order.
constexpr size_t kMedianValueBudget = 16u * 1024u * 1024u;

struct BandAcc
{
    std::int64_t count = 0;
    std::int64_t nodata = 0;
    double min = std::numeric_limits<double>::infinity();
    double max = -std::numeric_limits<double>::infinity();
    // Welford's online moments — numerically stable single pass (the
    // naive E[X^2]-E[X]^2 form cancels catastrophically on large values).
    double mean = 0.0;
    double m2 = 0.0;
    std::vector<float> values; // median collection (budgeted)
    bool medianTruncated = false;
};

using ZoneKey = std::string;
using ZoneAcc = std::map<int, BandAcc>; // band → accumulator

} // anonymous namespace

Json::Value RsZonalStatsOperator::schema() const {
    using namespace schema;
    Json::Value props( Json::objectValue );
    props["input"] = makeRasterParam( "input", "Value raster (CRS + north-up geotransform define the sampling grid)" );
    props["vector"] = makeVectorParam( "vector", "Zone polygons (OGR dataset)" );
    props["layer"] = makeStringParam( "layer", "Layer name or 0-based index (default first layer)", "" );
    props["zoneField"] = makeStringParam( "zoneField", "Attribute naming zones (default: FID)", "" );
    Json::Value bandsParam = makeStringParam( "bands", "1-based band indices (default [1])", "" );
    bandsParam["type"] = "array";
    bandsParam["items"] = Json::Value( Json::objectValue );
    bandsParam["items"]["type"] = "integer";
    bandsParam["required"] = false;
    props["bands"] = bandsParam;
    props["median"] = makeBooleanParam( "median", "Compute the exact median per zone (budgeted)", true );
    props["output"] = makeOutputParam( "output", "Output CSV path", "csv" );

    Json::Value outputs( Json::objectValue );
    outputs["output"] = makeOutputParam( "output", "CSV statistics", "csv" );

    Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
    root["required"] = makeRequired( { "input", "vector", "output" } );
    return root;
}

Json::Value RsZonalStatsOperator::metadata() const {
    Json::Value meta( Json::objectValue );
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append( "raster-vector" );
    meta["tags"].append( "zonal" );
    meta["tags"].append( "statistics" );
    meta["task"] = "raster-vector";
    meta["gpu"] = false;
    meta["purpose"] = "Summarize raster values inside polygon zones (site "
                      "characterization, accuracy sampling, region reporting).";
    meta["prerequisites"].append( "Value raster carries a CRS and a north-up geotransform." );
    meta["prerequisites"].append( "Zone features carry geometry; a declared zoneField must exist on every feature." );
    meta["limitations"].append( "Overlapping zones: last feature wins (input order), matching rs:rasterize." );
    meta["limitations"].append( "Median is budgeted (shared 16M-value collection); zones beyond it are flagged "
                                "while their other statistics stay exact." );
    meta["limitations"].append( "stddev is the population statistic (divided by N)." );
    return meta;
}

Json::Value RsZonalStatsOperator::executionEstimate() const {
    Json::Value est( Json::objectValue );
    est["tileWidth"] = kWindowDim;
    est["tileHeight"] = kWindowDim;
    est["estimatedRamBytes"] = Json::Value::UInt64(
        kMedianValueBudget * sizeof( float ) + 4ULL * kWindowDim * kWindowDim * sizeof( float ) );
    return est;
}

Json::Value RsZonalStatsOperator::run( const Json::Value &params, RSOperatorContext &context )
{
    if ( !params.isObject() )
        throw RSOperatorError( ErrorCode::InvalidParameter, "Operator parameters must be a JSON object" );

    const std::string inputPath = requireString( params, "input" );
    const std::string vectorPath = requireString( params, "vector" );
    const std::string outputPath = requireString( params, "output" );
    const std::string layer = getString( params, "layer", "" );
    const std::string zoneField = getString( params, "zoneField", "" );
    const bool wantMedian = getBool( params, "median", true );

    // Bands: JSON array of 1-based ints (default [1]).
    std::vector<int> bands;
    if ( params.isMember( "bands" ) && params["bands"].isArray() && !params["bands"].empty() )
    {
        for ( const auto &b : params["bands"] )
        {
            if ( !b.isNumeric() )
                throw RSOperatorError( ErrorCode::InvalidParameter, "bands entries must be integers" );
            bands.push_back( b.asInt() );
        }
    }
    else
    {
        bands.push_back( 1 );
    }

    ensureGdalInit();

    GdalDatasetWrapper ds;
    if ( !ds.open( QString::fromStdString( inputPath ) ) )
        throw RSOperatorError( ErrorCode::GdalError, "Failed to open value raster: " + inputPath );
    const int width = ds.width();
    const int height = ds.height();
    if ( width <= 0 || height <= 0 )
        throw RSOperatorError( ErrorCode::InvalidInputData, "Value raster is empty: " + inputPath );
    if ( ds.projection().isEmpty() || !ds.hasGeoTransform() )
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "Value raster must carry a CRS and geotransform: " + inputPath );
    const std::array<double, 6> gt = ds.geoTransform();
    if ( gt[2] != 0.0 || gt[4] != 0.0 )
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "Value raster geotransform is rotated; the raster-vector family "
                               "requires north-up grids" );
    for ( const int band : bands )
        if ( band < 1 || band > ds.bandCount() )
            throw RSOperatorError( ErrorCode::InvalidParameter,
                                   "band must be in [1, " + std::to_string( ds.bandCount() )
                                       + "], got " + std::to_string( band ) );

    // Per-band declared sentinels (compared in float space, #444).
    std::vector<bool> hasSentinel( bands.size(), false );
    std::vector<float> sentinels( bands.size(), 0.0f );
    for ( size_t i = 0; i < bands.size(); ++i )
    {
        bool has = false;
        const double nodata = ds.bandNoDataValue( bands[i], &has );
        if ( has && std::isfinite( nodata ) )
        {
            hasSentinel[i] = true;
            sentinels[i] = static_cast<float>( nodata );
        }
    }

    const sicnu::geo::Crs crs = [&ds, &inputPath]() {
        try
        {
            return sicnu::geo::Crs::fromWkt( ds.projection().toStdString() );
        }
        catch ( const sicnu::geo::GeoError &e )
        {
            throw RSOperatorError( ErrorCode::InvalidInputData,
                                   "Value raster CRS is unusable: " + std::string( e.what() )
                                       + " (" + inputPath + ")" );
        }
    }();

    RasterGrid grid;
    grid.width = width;
    grid.height = height;
    grid.gt = gt;

    FeatureLoadSpec spec;
    spec.vectorPath = QString::fromStdString( vectorPath );
    spec.layerSelector = QString::fromStdString( layer );
    spec.zoneField = QString::fromStdString( zoneField );

    context.reportProgress( 0.05, "Loading zone features" );
    FeatureCache cache = loadFeatureCache( spec, grid, crs );
    if ( cache.features.empty() )
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "The zone vector carries no features: " + vectorPath );

    // Accumulators in zone-key order (deterministic CSV ordering). Every
    // grid-overlapping zone is seeded up front so a zone covering zero
    // pixel centers still reports a count-0 row — silence would hide
    // geometry/CRS mistakes (zones fully outside the grid stay in
    // outsideGridFeatures instead).
    std::map<ZoneKey, ZoneAcc> zones;
    for ( const CachedFeature &f : cache.features )
    {
        if ( f.geometry != nullptr && f.intersects )
            zones[f.zoneKey];
    }

    size_t medianBudgetLeft = kMedianValueBudget;
    std::uint64_t windowsTouched = 0;
    std::vector<float> mask, valueWindow;
    const int windowsX = ( width + kWindowDim - 1 ) / kWindowDim;
    const int windowsY = ( height + kWindowDim - 1 ) / kWindowDim;
    const int totalWindows = windowsX * windowsY;
    int windowIndex = 0;
    for ( int wy = 0; wy < windowsY; ++wy )
    {
        for ( int wx = 0; wx < windowsX; ++wx )
        {
            context.throwIfCancelled();
            ++windowIndex;
            const int xOff = wx * kWindowDim;
            const int yOff = wy * kWindowDim;
            const int winW = std::min( kWindowDim, width - xOff );
            const int winH = std::min( kWindowDim, height - yOff );
            const size_t winN = static_cast<size_t>( winW ) * winH;

            // Zones intersecting this window (slot = feature index + 1).
            std::vector<const CachedFeature *> slotFeatures;
            std::vector<OGRGeometryH> geoms;
            std::vector<double> slotValues;
            for ( const CachedFeature &f : cache.features )
            {
                if ( f.geometry == nullptr || !f.intersects )
                    continue;
                const bool overlap = f.bounds[0] < xOff + winW && f.bounds[2] > xOff
                                     && f.bounds[1] < yOff + winH && f.bounds[3] > yOff;
                if ( !overlap )
                    continue;
                slotFeatures.push_back( &f );
                geoms.push_back( f.geometry );
                slotValues.push_back( static_cast<double>( slotFeatures.size() ) );
            }
            if ( geoms.empty() )
            {
                context.reportProgress( 0.9 * windowIndex / totalWindows, "Zonal statistics" );
                continue;
            }
            ++windowsTouched;

            mask.resize( winN );
            std::fill( mask.begin(), mask.end(), kNaN );
            GDALDatasetH mem = createMemWindow( winW, winH );
            if ( mem == nullptr )
                throw RSOperatorError( ErrorCode::GdalError, "Failed to create the zone mask window" );
            const auto geo = grid.windowGeoBounds( xOff, yOff, winW, winH );
            const bool okRaster = rasterizeWindow( mem, 1, geoms, slotValues, geo[0], geo[3],
                                                   gt[1], gt[5], false );
            const bool okRead = okRaster && readMemWindow( mem, 1, winW, winH, mask.data() );
            GDALClose( mem );
            if ( !okRead )
                throw RSOperatorError( ErrorCode::GdalError,
                                       "Zone mask rasterization failed on window ("
                                           + std::to_string( xOff ) + ", "
                                           + std::to_string( yOff ) + ")" );

            // Per-band value windows + accumulation.
            valueWindow.resize( winN );
            for ( size_t bi = 0; bi < bands.size(); ++bi )
            {
                if ( !ds.readBandWindow( bands[bi], xOff, yOff, winW, winH, valueWindow.data() ) )
                    throw RSOperatorError( ErrorCode::GdalError,
                                           "Failed to read value band "
                                               + std::to_string( bands[bi] ) );
                const float sentinel = sentinels[bi];
                const bool hasS = hasSentinel[bi];
                for ( size_t i = 0; i < winN; ++i )
                {
                    // NaN-safe slot test: the mask holds burn values 1..n
                    // or NaN (unburned); a float→int cast of NaN would be
                    // undefined behavior, so filter on the float first.
                    const float maskValue = mask[i];
                    if ( !( maskValue > 0.0f ) )
                        continue;
                    const int slot = static_cast<int>( maskValue );
                    if ( slot > static_cast<int>( slotFeatures.size() ) )
                        continue;
                    BandAcc &acc = zones[slotFeatures[static_cast<size_t>( slot - 1 )]->zoneKey]
                                   [bands[bi]];
                    const float v = valueWindow[i];
                    if ( !std::isfinite( v ) || ( hasS && v == sentinel ) )
                    {
                        ++acc.nodata;
                        continue;
                    }
                    ++acc.count;
                    acc.min = std::min( acc.min, static_cast<double>( v ) );
                    acc.max = std::max( acc.max, static_cast<double>( v ) );
                    const double delta = v - acc.mean;
                    acc.mean += delta / acc.count;
                    acc.m2 += delta * ( v - acc.mean );
                    if ( wantMedian )
                    {
                        if ( medianBudgetLeft > 0 )
                        {
                            acc.values.push_back( v );
                            --medianBudgetLeft;
                        }
                        else
                        {
                            acc.medianTruncated = true;
                        }
                    }
                }
            }
            context.reportProgress( 0.9 * windowIndex / totalWindows, "Zonal statistics" );
        }
    }

    context.reportProgress( 0.95, "Writing CSV" );
    QFile out( QString::fromStdString( outputPath ) );
    if ( !out.open( QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text ) )
        throw RSOperatorError( ErrorCode::FileNotWritable, "Cannot write: " + outputPath );
    QTextStream ts( &out );
    ts << "zone_key,band,valid_pixels,nodata_pixels,min,max,mean,stddev,median,median_truncated\n";
    std::uint64_t rows = 0;
    std::uint64_t truncatedZones = 0;
    for ( auto &[zoneKey, perBand] : zones )
    {
        for ( const int band : bands )
        {
            static const BandAcc kEmpty;
            const BandAcc &acc = perBand.count( band ) ? perBand.at( band ) : kEmpty;
            const double mean = acc.count > 0 ? acc.mean : kNaN;
            const double stddev = acc.count > 0 ? std::sqrt( acc.m2 / acc.count ) : kNaN;
            double median = kNaN;
            if ( acc.count > 0 && !acc.medianTruncated && !acc.values.empty() )
            {
                // Exact median over the collected values (odd/even).
                std::vector<float> sorted = acc.values;
                const size_t n = sorted.size();
                std::nth_element( sorted.begin(), sorted.begin() + n / 2, sorted.end() );
                const double mid = sorted[n / 2];
                if ( n % 2 == 1 )
                    median = mid;
                else
                {
                    const double lower = *std::max_element( sorted.begin(),
                                                            sorted.begin() + n / 2 );
                    median = 0.5 * ( lower + mid );
                }
            }
            // RFC-4180: quote the key when it carries a comma, quote or
            // newline so machine-readable rows cannot be silently corrupted.
            QString keyText = QString::fromStdString( zoneKey );
            if ( keyText.contains( QChar( ',' ) ) || keyText.contains( QChar( '"' ) )
                 || keyText.contains( QChar( '\n' ) ) )
            {
                keyText.replace( QChar( '"' ), QStringLiteral( "\"\"" ) );
                ts << '"' << keyText << '"';
            }
            else
            {
                ts << keyText;
            }
            ts << "," << band << "," << acc.count << "," << acc.nodata << ",";
            if ( acc.count > 0 )
                ts << QString::fromStdString( formatDouble( acc.min ) ) << ","
                   << QString::fromStdString( formatDouble( acc.max ) ) << ","
                   << QString::fromStdString( formatDouble( mean ) ) << ","
                   << QString::fromStdString( formatDouble( stddev ) ) << ",";
            else
                ts << "nan,nan,nan,nan,";
            ts << QString::fromStdString( formatDouble( median ) ) << ","
               << ( acc.medianTruncated ? 1 : 0 ) << "\n";
            ++rows;
            if ( acc.medianTruncated )
                ++truncatedZones;
        }
    }
    out.close();

    Json::Value result( Json::objectValue );
    result["output"] = outputPath;
    result["zones"] = Json::Value::UInt64( zones.size() );
    result["rows"] = Json::Value::UInt64( rows );
    result["bands"] = static_cast<Json::Value::Int64>( bands.size() );
    result["features"] = Json::Value::UInt64( cache.features.size() );
    result["geometrylessFeatures"] = Json::Value::UInt64( cache.geometryless );
    result["outsideGridFeatures"] = Json::Value::UInt64( cache.outsideGrid );
    result["medianTruncatedZones"] = Json::Value::UInt64( truncatedZones );
    result["windowsTouched"] = Json::Value::UInt64( windowsTouched );
    context.reportProgress( 1.0, "Zonal statistics complete" );
    return result;
}

} // namespace sicnu::operators::rs
