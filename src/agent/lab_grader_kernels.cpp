// src/agent/lab_grader_kernels.cpp — grader 2.0 assertion kernels.
//
// Determinism notes: zone codes, bands and bounds are evaluated in declared/
// ascending order; observed JSON is built from accumulators only. Every
// windowed read obeys the caller's byte budget through planTiles().
#include "lab_grader_kernels.h"

#include "geospatial/raster/raster_reader.h"
#include "mapspec/mapspec.h"
#include "cartography/quality.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace sicnu::agent {

namespace {

using sicnu::geo::BandInfo;
using sicnu::geo::RasterMetadata;
using sicnu::geo::RasterReader;
using sicnu::geo::RasterWindow;
using sicnu::geo::TilePlan;
using sicnu::geo::TileSlice;

constexpr const char *kRasterKinds[] = {
    "zone_stats", "band_layout", "spatial_agreement", "series_separation",
    "spectral_signature", "provenance",
};
constexpr const char *kFileKinds[] = { "file_check" };

bool inList( const QString &kind, const char *const (&list)[], int count )
{
    for ( int i = 0; i < count; ++i )
        if ( kind == QLatin1String( list[i] ) )
            return true;
    return false;
}

bool isNonFinite( double v ) { return std::isnan( v ) || std::isinf( v ); }

bool bandIsNoData( const BandInfo &band, double v )
{
    if ( band.hasNoData )
    {
        if ( band.noDataIsNaN )
            return std::isnan( v );
        return v == band.noDataValue;
    }
    return false;
}

/// Resolves @p ref relative to @p rulesDir, honouring an absolute path.
QString resolveAgainstRules( const QString &rulesDir, const QString &ref )
{
    if ( QFileInfo( ref ).isAbsolute() )
        return ref;
    return QDir( rulesDir ).filePath( ref );
}

/// Opens a truth/reference raster (usage error when missing/unopenable).
std::unique_ptr<RasterReader> openSideRaster( const QString &path, const QString &what,
                                              const QString &assertionId, QString *usageError )
{
    if ( !QFile::exists( path ) )
    {
        *usageError = QStringLiteral( "Assertion \"%1\": %2 raster does not exist: %3" )
                        .arg( assertionId, what, path );
        return nullptr;
    }
    try
    {
        auto reader = std::make_unique<RasterReader>( RasterReader::open( path.toUtf8().constData() ) );
        if ( reader->metadata().bandCount < 1 )
        {
            *usageError = QStringLiteral( "Assertion \"%1\": %2 raster has no bands" )
                            .arg( assertionId, what );
            return nullptr;
        }
        return reader;
    }
    catch ( const sicnu::geo::GeoError &e )
    {
        *usageError = QStringLiteral( "Assertion \"%1\": cannot open %2 raster: %3" )
                        .arg( assertionId, what, QString::fromUtf8( e.what() ) );
        return nullptr;
    }
}

/// Largest square-friendly tile that fits the budget for @p bandCount bands.
bool planTiles( int width, int height, int bandCount, std::size_t maxBytes, int *tileW,
                int *tileH, QString *error )
{
    const std::size_t bytesPerPixel =
      static_cast<std::size_t>( bandCount ) * sizeof( double );
    if ( bytesPerPixel == 0 || bytesPerPixel > maxBytes )
    {
        *error = QStringLiteral( "byte budget (%1) is below a single pixel (%2 bytes)" )
                   .arg( maxBytes )
                   .arg( bytesPerPixel );
        return false;
    }
    std::size_t perRow = bytesPerPixel * static_cast<std::size_t>( width );
    if ( perRow <= maxBytes )
    {
        *tileW = width;
        *tileH = static_cast<int>( maxBytes / perRow );
    }
    else
    {
        *tileW = static_cast<int>( maxBytes / bytesPerPixel );
        *tileH = 1;
    }
    *tileH = std::max( 1, *tileH );
    *tileW = std::min( *tileW, width );
    return true;
}

/// Paired window walk: artifact bands + (optionally) two side rasters of the
/// SAME grid. sink receives the artifact buffer plus the side buffers.
template <typename Sink>
bool walkWithSides( const RasterReader &reader, const std::vector<int> &bands,
                    const RasterReader *sideA, const std::vector<int> &sideABands,
                    const RasterReader *sideB, const std::vector<int> &sideBBands,
                    std::size_t maxBytes, Sink &&sink, QString *error )
{
    const RasterMetadata &meta = reader.metadata();
    int tileW = 0, tileH = 0;
    if ( !planTiles( meta.width, meta.height, static_cast<int>( bands.size() ), maxBytes,
                     &tileW, &tileH, error ) )
        return false;
    RasterWindow full;
    full.width = meta.width;
    full.height = meta.height;
    try
    {
        const TilePlan plan = sicnu::geo::planTileWalk( meta, full, tileW, tileH );
        for ( int ty = 0; ty < plan.tilesY; ++ty )
        {
            for ( int tx = 0; tx < plan.tilesX; ++tx )
            {
                const TileSlice slice = plan.slice( tx, ty );
                RasterWindow window;
                window.xOff = slice.xOff;
                window.yOff = slice.yOff;
                window.width = slice.width;
                window.height = slice.height;
                const std::vector<double> artifact =
                  reader.readWindow( bands, window, maxBytes );
                std::vector<double> bufA, bufB;
                if ( sideA )
                    bufA = sideA->readWindow( sideABands, window, maxBytes );
                if ( sideB )
                    bufB = sideB->readWindow( sideBBands, window, maxBytes );
                sink( slice, artifact, bufA, bufB );
            }
        }
    }
    catch ( const sicnu::geo::GeoError &e )
    {
        *error = QString::fromUtf8( e.what() );
        return false;
    }
    return true;
}

// --- Welford accumulator ----------------------------------------------------

struct ZoneAcc
{
    qint64 n = 0;
    double mean = 0.0;
    double m2 = 0.0;
    void add( double v )
    {
        ++n;
        const double d = v - mean;
        mean += d / static_cast<double>( n );
        m2 += d * ( v - mean );
    }
    double variance() const { return n > 1 ? m2 / static_cast<double>( n ) : 0.0; }
};

// --- param helpers ----------------------------------------------------------

bool paramZoneList( const Json::Value &params, std::set<int> *out, QString *error,
                    const QString &assertionId, const char *key )
{
    if ( !params.isMember( key ) )
        return true;
    for ( const Json::Value &item : params[key] )
    {
        if ( !item.isIntegral() )
        {
            *error = QStringLiteral( "Assertion \"%1\": %2 entries must be integers" )
                       .arg( assertionId, key );
            return false;
        }
        out->insert( item.asInt() );
    }
    return true;
}

Json::Value zoneStatJson( const ZoneAcc &acc )
{
    Json::Value json( Json::objectValue );
    json["n"] = static_cast<Json::Int64>( acc.n );
    if ( acc.n > 0 )
        json["mean"] = acc.mean;
    return json;
}

} // namespace

bool isLabRasterKernelKind( const QString &kind )
{
    // Count derived from the array: appending a kind without bumping a
    // hand-written number here silently leaves it unscheduled (the failure
    // mode this constant used to hide).
    return inList( kind, kRasterKinds,
                   int( sizeof( kRasterKinds ) / sizeof( kRasterKinds[ 0 ] ) ) );
}

bool isLabFileKernelKind( const QString &kind )
{
    return inList( kind, kFileKinds, 1 );
}

bool labKernelKindsMatchArtifactMode( const QString &artifactKind, const QString &kind )
{
    if ( artifactKind == QLatin1String( "file" ) )
        return isLabFileKernelKind( kind );
    return isLabRasterKernelKind( kind );
}

bool validateLabKernelParams( const QString &kind, const Json::Value &params, QString *error )
{
    const auto fail = [error]( const QString &message ) -> bool
    {
        *error = message;
        return false;
    };
    if ( !params.isObject() )
        return fail( QStringLiteral( "kernel params must be an object" ) );

    if ( kind == QLatin1String( "zone_stats" ) || kind == QLatin1String( "series_separation" )
         || kind == QLatin1String( "spectral_signature" ) )
    {
        if ( kind == QLatin1String( "spectral_signature" ) )
        {
            if ( !params.isMember( "references" ) || !params["references"].isArray()
                 || params["references"].empty() )
                return fail( QStringLiteral( "spectral_signature requires a non-empty references array" ) );
            if ( params.isMember( "bands" ) && params["bands"].isArray() )
            {
                const Json::ArrayIndex bandCount = params["bands"].size();
                for ( const Json::Value &ref : params["references"] )
                {
                    if ( !ref.isObject() || !ref.isMember( "spectrum" )
                         || !ref["spectrum"].isArray() || ref["spectrum"].empty() )
                        return fail( QStringLiteral(
                          "spectral_signature references entries need a non-empty spectrum array" ) );
                    if ( ref["spectrum"].size() != bandCount )
                        return fail( QStringLiteral(
                          "spectral_signature: every reference spectrum must have exactly one "
                          "value per declared band" ) );
                }
            }
            if ( params.isMember( "zone_reference" ) )
            {
                if ( !params["zone_reference"].isArray() )
                    return fail( QStringLiteral( "zone_reference must be an array" ) );
                const Json::ArrayIndex referenceCount = params["references"].size();
                for ( const Json::Value &entry : params["zone_reference"] )
                {
                    if ( !entry.isObject() || !entry.isMember( "zone" )
                         || !entry["zone"].isIntegral() )
                        return fail( QStringLiteral(
                          "zone_reference entries need an integer zone" ) );
                    if ( entry.isMember( "reference" )
                         && ( !entry["reference"].isIntegral()
                              || entry["reference"].asInt() < 0
                              || entry["reference"].asInt() >= static_cast<int>( referenceCount ) ) )
                        return fail( QStringLiteral(
                          "zone_reference reference index out of range" ) );
                }
            }
            return true;
        }
        if ( !params.isMember( "zones" ) || !params["zones"].isMember( "path" ) )
            return fail( QStringLiteral( "%1 requires zones.path" ).arg( kind ) );
        if ( kind == QLatin1String( "zone_stats" ) )
        {
            const QString stat = params.isMember( "stat" )
                                   ? QString::fromStdString( params["stat"].asString() )
                                   : QStringLiteral( "mean" );
            if ( stat != QLatin1String( "mean" ) && stat != QLatin1String( "enl" )
                 && stat != QLatin1String( "fisher" ) )
                return fail( QStringLiteral( "zone_stats stat must be mean|enl|fisher" ) );
            if ( stat == QLatin1String( "mean" )
                 && ( !params.isMember( "bounds" ) || !params["bounds"].isArray() ) )
                return fail( QStringLiteral( "zone_stats stat=mean requires a bounds array" ) );
            if ( params.isMember( "bounds" ) )
            {
                for ( const Json::Value &bound : params["bounds"] )
                {
                    if ( !bound.isObject() || !bound.isMember( "zone" )
                         || !bound["zone"].isIntegral() )
                        return fail( QStringLiteral(
                          "zone_stats bounds entries need an integer zone" ) );
                    if ( bound.isMember( "band" ) && !bound["band"].isIntegral() )
                        return fail( QStringLiteral(
                          "zone_stats bounds entry band must be an integer" ) );
                    if ( bound.isMember( "min" ) && !bound["min"].isNumeric() )
                        return fail( QStringLiteral(
                          "zone_stats bounds entry min must be a number" ) );
                    if ( bound.isMember( "max" ) && !bound["max"].isNumeric() )
                        return fail( QStringLiteral(
                          "zone_stats bounds entry max must be a number" ) );
                }
            }
            if ( params.isMember( "zone_deltas" ) || params.isMember( "separations" ) )
            {
                const Json::Value &list = params.isMember( "separations" )
                                            ? params["separations"]
                                            : params["zone_deltas"];
                if ( !list.isArray() )
                    return fail( QStringLiteral( "zone_deltas/separations must be an array" ) );
                for ( const Json::Value &item : list )
                {
                    if ( !item.isObject() || !item.isMember( "zone" ) || !item["zone"].isIntegral()
                         || !item.isMember( "below_zone" ) || !item["below_zone"].isIntegral()
                         || !item.isMember( "min_delta" ) || !item["min_delta"].isNumeric() )
                        return fail( QStringLiteral(
                          "separation entries need integer zone/below_zone and numeric "
                          "min_delta" ) );
                }
            }
            if ( params.isMember( "zones_nodata" ) && !params["zones_nodata"].isNumeric() )
                return fail( QStringLiteral( "zones_nodata must be a number" ) );
            if ( params.isMember( "band" ) && !params["band"].isIntegral() )
                return fail( QStringLiteral( "band must be an integer" ) );
            if ( params.isMember( "zone" ) && !params["zone"].isIntegral() )
                return fail( QStringLiteral( "zone must be an integer" ) );
            if ( stat == QLatin1String( "fisher" )
                 && ( !params.isMember( "zone" ) || !params.isMember( "compare_zone" )
                      || !params.isMember( "min_fisher" ) ) )
                return fail( QStringLiteral(
                  "zone_stats stat=fisher requires zone, compare_zone and min_fisher" ) );
        }
        if ( kind == QLatin1String( "series_separation" ) )
        {
            if ( !params.isMember( "x" ) || !params["x"].isArray() || params["x"].size() < 2 )
                return fail( QStringLiteral( "series_separation requires x (>= 2 axis values)" ) );
            for ( const Json::Value &v : params["x"] )
                if ( !v.isNumeric() )
                    return fail( QStringLiteral( "series_separation x entries must be numbers" ) );
            if ( !params.isMember( "bounds" ) && !params.isMember( "separations" ) )
                return fail( QStringLiteral(
                  "series_separation requires bounds and/or separations" ) );
        }
        return true;
    }

    if ( kind == QLatin1String( "band_layout" ) )
    {
        if ( !params.isMember( "band_count" ) && !params.isMember( "min_valid_fraction" )
             && !params.isMember( "expected_valid_pixels" ) )
            return fail( QStringLiteral(
              "band_layout requires band_count, min_valid_fraction or expected_valid_pixels" ) );
        return true;
    }

    if ( kind == QLatin1String( "provenance" ) )
    {
        static const char *const allowed[] = {
            "generator", "seed", "product", "profile", "version",
        };
        bool any = false;
        for ( const auto &key : params.getMemberNames() )
        {
            bool known = false;
            for ( const char *candidate : allowed )
                known = known || key == candidate;
            if ( !known )
                return fail( QStringLiteral(
                  "provenance params: unknown key \"%1\" (allowed: generator, seed, "
                  "product, profile, version)" ).arg( QString::fromStdString( key ) ) );
            any = true;
        }
        if ( !any )
            return fail( QStringLiteral(
              "provenance requires at least one of generator, seed, product, "
              "profile, version" ) );
        if ( params.isMember( "seed" )
             && ( !params["seed"].isIntegral() || params["seed"].asInt64() < 0
                  || params["seed"].asInt64() > 0xFFFFFFFFll ) )
            return fail( QStringLiteral( "provenance seed must be a uint32" ) );
        for ( const char *key : { "generator", "product", "profile", "version" } )
        {
            if ( params.isMember( key ) && ( !params[key].isString()
                                             || params[key].asString().empty() ) )
                return fail( QStringLiteral(
                  "provenance %1 must be a non-empty string" ).arg( key ) );
        }
        return true;
    }

    if ( kind == QLatin1String( "spatial_agreement" ) )
    {
        if ( !params.isMember( "truth" ) || !params["truth"].isMember( "path" ) )
            return fail( QStringLiteral( "spatial_agreement requires truth.path" ) );
        if ( params.isMember( "zone_expected" ) && !params["zone_expected"].isArray() )
            return fail( QStringLiteral( "zone_expected must be an array" ) );
        if ( params.isMember( "zone_expected" ) )
        {
            for ( const Json::Value &entry : params["zone_expected"] )
            {
                if ( !entry.isObject() || !entry.isMember( "zone" ) || !entry["zone"].isIntegral()
                     || !entry.isMember( "label" ) || !entry["label"].isNumeric() )
                    return fail( QStringLiteral(
                      "zone_expected entries need integer zone and numeric label" ) );
            }
        }
        if ( params.isMember( "band" ) && !params["band"].isIntegral() )
            return fail( QStringLiteral( "band must be an integer" ) );
        const bool zone = params.isMember( "zone_expected" );
        const bool binary = params.isMember( "min_hit_rate" ) || params.isMember( "max_false_alarm_rate" );
        const bool continuous = params.isMember( "tolerance" ) || params.isMember( "max_exceed_fraction" );
        const int modeCount = ( zone ? 1 : 0 ) + ( binary ? 1 : 0 ) + ( continuous ? 1 : 0 );
        if ( modeCount != 1 )
            return fail( QStringLiteral(
              "spatial_agreement mode is ambiguous: give exactly one of zone_expected | "
              "min_hit_rate/max_false_alarm_rate | tolerance/max_exceed_fraction" ) );
        if ( zone && !params.isMember( "min_accuracy" ) )
            return fail( QStringLiteral( "spatial_agreement zone mode requires min_accuracy" ) );
        // Hit and false-alarm bounds may be split across TWO assertions
        // (hit_rate in one, false_alarm_rate in another), so binary mode is
        // valid with either bound present.
        if ( binary && !params.isMember( "min_hit_rate" )
             && !params.isMember( "max_false_alarm_rate" ) )
            return fail( QStringLiteral(
              "spatial_agreement binary mode requires min_hit_rate or max_false_alarm_rate" ) );
        if ( continuous
             && !( params.isMember( "tolerance" ) && params.isMember( "max_exceed_fraction" ) ) )
            return fail( QStringLiteral(
              "spatial_agreement continuous mode requires tolerance and max_exceed_fraction" ) );
        return true;
    }

    if ( kind == QLatin1String( "file_check" ) )
    {
        if ( !params.isMember( "exists" ) && !params.isMember( "min_bytes" )
             && !params.isMember( "png" ) && !params.isMember( "mapspec" )
             && !params.isMember( "preflight" ) )
            return fail( QStringLiteral(
              "file_check requires exists, min_bytes, png, mapspec or preflight" ) );
        if ( params.isMember( "path" ) && !params["path"].isString() )
            return fail( QStringLiteral( "file_check path must be a string" ) );
        return true;
    }

    return fail( QStringLiteral( "unknown kernel kind \"%1\"" ).arg( kind ) );
}

// ============================================================================
// zone_stats + series_separation: one paired walk each
// ============================================================================

namespace {

/// A bound on one (band, zone) cell's statistic.
struct ZoneBound
{
    int band = 0;   // 1-based; 0 = kernel default band
    int zone = 0;
    bool hasMin = false;
    double min = 0.0;
    bool hasMax = false;
    double max = 0.0;
};

/// mean(below_zone) - mean(zone) >= min_delta (same band).
struct ZoneDelta
{
    int band = 0;
    int zone = 0;
    int belowZone = 0;
    double minDelta = 0.0;
};

QVector<ZoneBound> parseBounds( const Json::Value &params, int defaultBand, QString *usageError,
                                const QString &assertionId )
{
    QVector<ZoneBound> bounds;
    for ( const Json::Value &item : params["bounds"] )
    {
        if ( !item.isObject() || !item.isMember( "zone" ) )
        {
            *usageError = QStringLiteral( "Assertion \"%1\": bounds entries need zone" ).arg( assertionId );
            return {};
        }
        ZoneBound bound;
        bound.zone = item["zone"].asInt();
        bound.band = item.isMember( "band" ) ? item["band"].asInt() : defaultBand;
        if ( item.isMember( "min" ) )
        {
            bound.hasMin = true;
            bound.min = item["min"].asDouble();
        }
        if ( item.isMember( "max" ) )
        {
            bound.hasMax = true;
            bound.max = item["max"].asDouble();
        }
        bounds.append( bound );
    }
    return bounds;
}

QVector<ZoneDelta> parseDeltas( const Json::Value &params, int defaultBand, QString *usageError,
                                const QString &assertionId )
{
    QVector<ZoneDelta> deltas;
    if ( !params.isMember( "separations" ) && !params.isMember( "zone_deltas" ) )
        return deltas;
    const Json::Value &list =
      params.isMember( "separations" ) ? params["separations"] : params["zone_deltas"];
    for ( const Json::Value &item : list )
    {
        if ( !item.isObject() || !item.isMember( "zone" ) || !item.isMember( "below_zone" )
             || !item.isMember( "min_delta" ) )
        {
            *usageError = QStringLiteral(
                            "Assertion \"%1\": separation entries need zone, below_zone, min_delta" )
                            .arg( assertionId );
            return {};
        }
        ZoneDelta delta;
        delta.zone = item["zone"].asInt();
        delta.belowZone = item["below_zone"].asInt();
        delta.minDelta = item["min_delta"].asDouble();
        delta.band = item.isMember( "band" ) ? item["band"].asInt() : defaultBand;
        deltas.append( delta );
    }
    return deltas;
}

} // namespace

static bool runZoneStats( const RasterReader &reader, const LabKernelSpec &spec,
                          const QString &rulesDir, std::size_t maxBytes,
                          LabKernelOutcome &outcome, QString *artifactError, QString &usageError )
{
    const Json::Value &params = spec.params;
    const QString stat = params.isMember( "stat" ) ? QString::fromStdString( params["stat"].asString() )
                                                   : QStringLiteral( "mean" );
    const int defaultBand = params.isMember( "band" ) ? params["band"].asInt() : 1;
    const RasterMetadata &meta = reader.metadata();
    if ( defaultBand < 1 || defaultBand > meta.bandCount )
    {
        outcome.passed = false;
        outcome.message = QStringLiteral( "Artifact lacks band %1" ).arg( defaultBand );
        outcome.observed["band_count"] = meta.bandCount;
        outcome.expected["band"] = defaultBand;
        return true;
    }

    const auto zoneReader = openSideRaster(
      resolveAgainstRules( rulesDir, QString::fromStdString( params["zones"]["path"].asString() ) ),
      QStringLiteral( "zones" ), spec.id, &usageError );
    if ( !zoneReader )
        return false;
    if ( zoneReader->metadata().width != meta.width
         || zoneReader->metadata().height != meta.height )
    {
        outcome.passed = false;
        outcome.observed["artifact_width"] = meta.width;
        outcome.observed["artifact_height"] = meta.height;
        outcome.observed["zones_width"] = zoneReader->metadata().width;
        outcome.observed["zones_height"] = zoneReader->metadata().height;
        outcome.message = QStringLiteral( "Zones grid does not match the artifact grid" );
        return true;
    }

    const bool hasZonesNoData = params.isMember( "zones_nodata" );
    const double zonesNoData = hasZonesNoData ? params["zones_nodata"].asDouble() : 0.0;

    // ENL comparisons accumulate a second cell set over a reference raster
    // (the unfiltered input) joined on the same zones.
    const bool enlMode = stat == QLatin1String( "enl" );
    double enlMinRatio = 0.0, maxMeanShift = 0.0;
    std::unique_ptr<RasterReader> referenceReader;
    if ( enlMode )
    {
        if ( !params.isMember( "reference" ) || !params["reference"].isMember( "path" )
             || !params.isMember( "enl_min_ratio" ) )
        {
            usageError = QStringLiteral(
                        "Assertion \"%1\": stat=enl requires reference.path and enl_min_ratio" )
                        .arg( spec.id );
            return false;
        }
        enlMinRatio = params["enl_min_ratio"].asDouble();
        maxMeanShift = params.isMember( "max_relative_mean_shift" )
                         ? params["max_relative_mean_shift"].asDouble()
                         : 0.0;
        referenceReader = openSideRaster(
          resolveAgainstRules( rulesDir,
                               QString::fromStdString( params["reference"]["path"].asString() ) ),
          QStringLiteral( "reference" ), spec.id, &usageError );
        if ( !referenceReader )
            return false;
        if ( referenceReader->metadata().width != meta.width
             || referenceReader->metadata().height != meta.height )
        {
            outcome.passed = false;
            outcome.message = QStringLiteral( "Reference grid does not match the artifact grid" );
            return true;
        }
        if ( referenceReader->metadata().bandCount < defaultBand )
        {
            outcome.passed = false;
            outcome.message = QStringLiteral( "Reference lacks band %1" ).arg( defaultBand );
            return true;
        }
    }

    // Bounds/deltas reference cells (band, zone); bands needed = default + any
    // overrides.
    QVector<ZoneBound> bounds;
    QVector<ZoneDelta> deltas;
    if ( stat == QLatin1String( "mean" ) )
    {
        bounds = parseBounds( params, defaultBand, &usageError, spec.id );
        if ( !usageError.isEmpty() )
            return false;
        deltas = parseDeltas( params, defaultBand, &usageError, spec.id );
        if ( !usageError.isEmpty() )
            return false;
    }

    std::set<int> bands;
    bands.insert( defaultBand );
    for ( const ZoneBound &b : bounds )
        bands.insert( b.band );
    for ( const ZoneDelta &d : deltas )
        bands.insert( d.band );

    // A rules band beyond the artifact is an ARTIFACT defect (the rules are
    // authored against the lab's declared spec) — a graded failure, not a
    // read error.
    for ( const int band : bands )
    {
        if ( band < 1 )
        {
            usageError = QStringLiteral( "Assertion \"%1\": band index must be >= 1" )
                           .arg( spec.id );
            return false;
        }
        if ( band > meta.bandCount )
        {
            outcome.passed = false;
            outcome.observed["band_count"] = meta.bandCount;
            outcome.expected["band"] = band;
            outcome.message = QStringLiteral( "Artifact lacks band %1" ).arg( band );
            return true;
        }
    }

    // (band, zone) -> accumulator (artifact) and (enl) reference accumulators
    std::map<std::pair<int, int>, ZoneAcc> accs;
    std::map<std::pair<int, int>, ZoneAcc> refAccs;

    const BandInfo &zonesBandInfo = zoneReader->metadata().bands.at( 0 );
    const BandInfo &referenceBandInfo =
      enlMode ? referenceReader->metadata().bands.at( static_cast<std::size_t>( defaultBand - 1 ) )
              : meta.bands.at( 0 );

    auto sink = [&]( const TileSlice &slice, const std::vector<double> &buffer,
                     const std::vector<double> &zonesBuf, const std::vector<double> &refBuf )
    {
        const std::size_t plane = static_cast<std::size_t>( slice.width ) * slice.height;
        // readWindow returns the REQUESTED bands in order — index by list
        // position, never by band number (a non-contiguous band list would
        // otherwise read out of bounds).
        std::size_t position = 0;
        for ( const int band : bands )
        {
            const std::size_t offset = position * plane;
            ++position;
            const BandInfo &info = meta.bands.at( static_cast<std::size_t>( band - 1 ) );
            for ( std::size_t p = 0; p < plane; ++p )
            {
                const double zone = zonesBuf[p];
                if ( isNonFinite( zone ) || bandIsNoData( zonesBandInfo, zone ) )
                    continue;
                if ( hasZonesNoData && zone == zonesNoData )
                    continue;
                const double v = buffer[offset + p];
                if ( bandIsNoData( info, v ) || isNonFinite( v ) )
                    continue;
                const auto key = std::make_pair( band, static_cast<int>( zone ) );
                accs[key].add( v );
                if ( enlMode )
                {
                    const double r = refBuf[p];
                    if ( !bandIsNoData( referenceBandInfo, r ) && !isNonFinite( r ) )
                        refAccs[key].add( r );
                }
            }
        }
    };
    if ( !walkWithSides( reader, std::vector<int>( bands.begin(), bands.end() ),
                         zoneReader.get(), { 1 }, enlMode ? referenceReader.get() : nullptr,
                         enlMode ? std::vector<int>{ defaultBand } : std::vector<int>{},
                         maxBytes, sink, artifactError ) )
        return false;

    // Observed: per (band, zone) mean in ascending order.
    Json::Value perCell( Json::arrayValue );
    for ( const auto &[key, acc] : accs )
    {
        Json::Value cell( Json::objectValue );
        cell["band"] = key.first;
        cell["zone"] = key.second;
        cell["n"] = static_cast<Json::Int64>( acc.n );
        if ( acc.n > 0 )
            cell["mean"] = acc.mean;
        if ( acc.n > 1 )
            cell["sigma"] = std::sqrt( acc.variance() );
        perCell.append( cell );
    }
    outcome.observed["stat"] = stat.toStdString();
    outcome.observed["per_band_zone"] = perCell;
    outcome.expected["bounds_count"] = static_cast<Json::Int64>( bounds.size() + deltas.size() );

    // Evaluate bounds + deltas against the accumulated means.
    bool passed = true;
    QString firstFailure;
    double worstDeviation = 0.0;
    auto cellMean = [&]( int band, int zone, bool *found ) -> double
    {
        const auto it = accs.find( { band, zone } );
        if ( it == accs.end() || it->second.n == 0 )
        {
            *found = false;
            return 0.0;
        }
        *found = true;
        return it->second.mean;
    };
    for ( const ZoneBound &bound : bounds )
    {
        bool found = false;
        const double mean = cellMean( bound.band, bound.zone, &found );
        if ( !found )
        {
            passed = false;
            firstFailure = QStringLiteral( "Zone %1 has no valid pixels in band %2" )
                             .arg( bound.zone )
                             .arg( bound.band );
            break;
        }
        double deviation = 0.0;
        bool violated = false;
        if ( bound.hasMin && mean < bound.min )
        {
            violated = true;
            deviation = bound.min - mean;
        }
        if ( bound.hasMax && mean > bound.max )
        {
            violated = true;
            deviation = std::max( deviation, mean - bound.max );
        }
        if ( violated )
        {
            passed = false;
            worstDeviation = std::max( worstDeviation, deviation );
            if ( firstFailure.isEmpty() )
                firstFailure = QStringLiteral( "Zone %1 band %2 mean %3 outside bounds" )
                                 .arg( bound.zone )
                                 .arg( bound.band )
                                 .arg( mean, 0, 'g', 12 );
        }
    }
    if ( passed )
    {
        for ( const ZoneDelta &delta : deltas )
        {
            bool foundTarget = false, foundBelow = false;
            const double target = cellMean( delta.band, delta.zone, &foundTarget );
            const double below = cellMean( delta.band, delta.belowZone, &foundBelow );
            if ( !foundTarget || !foundBelow )
            {
                passed = false;
                firstFailure = QStringLiteral( "Separation zones %1/%2 lack valid pixels" )
                                 .arg( delta.belowZone )
                                 .arg( delta.zone );
                break;
            }
            const double actual = below - target;
            if ( actual < delta.minDelta )
            {
                passed = false;
                outcome.hasDelta = true;
                outcome.delta = actual - delta.minDelta;
                firstFailure = QStringLiteral(
                                 "Separation below_zone %1 - zone %2 = %3 < min_delta %4" )
                                 .arg( delta.belowZone )
                                 .arg( delta.zone )
                                 .arg( actual, 0, 'g', 12 )
                                 .arg( delta.minDelta, 0, 'g', 12 );
                break;
            }
        }
    }
    // ENL mode: per-zone ENL(filtered) must exceed ENL(raw) by the declared
    // ratio while the zone mean stays radiometrically unbiased.
    if ( enlMode )
    {
        Json::Value perZone( Json::objectValue );
        const double artifactVarFloor = 1e-12;
        for ( const auto &[key, acc] : accs )
        {
            if ( key.first != defaultBand || acc.n < 2 )
                continue;
            const auto refIt = refAccs.find( key );
            if ( refIt == refAccs.end() || refIt->second.n < 2 )
            {
                passed = false;
                firstFailure = QStringLiteral( "Zone %1 lacks reference pixels" ).arg( key.second );
                break;
            }
            const double enlFiltered =
              acc.mean * acc.mean / std::max( acc.variance(), artifactVarFloor );
            const double enlRaw =
              refIt->second.mean * refIt->second.mean
              / std::max( refIt->second.variance(), artifactVarFloor );
            Json::Value cell( Json::objectValue );
            cell["enl_filtered"] = enlFiltered;
            cell["enl_raw"] = enlRaw;
            cell["ratio"] = enlRaw > 0 ? enlFiltered / enlRaw : Json::Value( Json::nullValue );
            cell["mean_shift"] =
              refIt->second.mean != 0.0
                ? std::fabs( acc.mean - refIt->second.mean ) / std::fabs( refIt->second.mean )
                : Json::Value( Json::nullValue );
            perZone[std::to_string( key.second )] = cell;
            const double ratio = enlRaw > 0 ? enlFiltered / enlRaw : 0.0;
            if ( ratio < enlMinRatio )
            {
                passed = false;
                outcome.hasDelta = true;
                outcome.delta = std::max( outcome.delta, enlMinRatio - ratio );
                if ( firstFailure.isEmpty() )
                    firstFailure = QStringLiteral( "Zone %1 ENL ratio %2 < min %3" )
                                     .arg( key.second )
                                     .arg( ratio, 0, 'g', 12 )
                                     .arg( enlMinRatio, 0, 'g', 12 );
            }
            if ( refIt->second.mean != 0.0 && maxMeanShift >= 0.0 )
            {
                const double shift =
                  std::fabs( acc.mean - refIt->second.mean ) / std::fabs( refIt->second.mean );
                if ( shift > maxMeanShift )
                {
                    passed = false;
                    if ( firstFailure.isEmpty() )
                        firstFailure = QStringLiteral( "Zone %1 mean shift %2 > max %3" )
                                         .arg( key.second )
                                         .arg( shift, 0, 'g', 12 )
                                         .arg( maxMeanShift, 0, 'g', 12 );
                }
            }
        }
        outcome.observed["enl_per_zone"] = perZone;
        outcome.expected["enl_min_ratio"] = enlMinRatio;
        outcome.expected["max_relative_mean_shift"] = maxMeanShift;
    }

    // Fisher mode: inter-zone separability (μt−μc)²/(σt²+σc²) ≥ min_fisher.
    if ( stat == QLatin1String( "fisher" ) )
    {
        const int targetZone = params.isMember( "zone" ) ? params["zone"].asInt() : 0;
        const int compareZone = params["compare_zone"].asInt();
        const double minFisher = params["min_fisher"].asDouble();
        const auto target = accs.find( { defaultBand, targetZone } );
        const auto compare = accs.find( { defaultBand, compareZone } );
        if ( target == accs.end() || compare == accs.end() || target->second.n < 2
             || compare->second.n < 2 )
        {
            passed = false;
            firstFailure = QStringLiteral( "Fisher zones %1/%2 lack valid pixels" )
                             .arg( targetZone )
                             .arg( compareZone );
        }
        else
        {
            const double varianceSum = target->second.variance() + compare->second.variance();
            const double fisher =
              varianceSum > 0
                ? ( target->second.mean - compare->second.mean )
                    * ( target->second.mean - compare->second.mean ) / varianceSum
                : 0.0;
            outcome.observed["fisher"] = fisher;
            outcome.observed["target_mean"] = target->second.mean;
            outcome.observed["compare_mean"] = compare->second.mean;
            outcome.expected["min_fisher"] = minFisher;
            if ( fisher < minFisher )
            {
                passed = false;
                outcome.hasDelta = true;
                outcome.delta = minFisher - fisher;
                firstFailure = QStringLiteral( "Fisher ratio %1 < min %2" )
                                 .arg( fisher, 0, 'g', 12 )
                                 .arg( minFisher, 0, 'g', 12 );
            }
        }
    }

    if ( !passed && firstFailure.isEmpty() )
        firstFailure = QStringLiteral( "zone_stats bounds violated" );
    outcome.passed = passed;
    if ( !passed )
    {
        outcome.message = firstFailure;
        if ( worstDeviation > 0.0 && !outcome.hasDelta )
        {
            outcome.hasDelta = true;
            outcome.delta = worstDeviation;
        }
    }
    return true;
}

static bool runSeriesSeparation( const RasterReader &reader, const LabKernelSpec &spec,
                                 const QString &rulesDir, std::size_t maxBytes,
                                 LabKernelOutcome &outcome, QString *artifactError,
                                 QString &usageError )
{
    const Json::Value &params = spec.params;
    const RasterMetadata &meta = reader.metadata();
    const std::vector<double> xAxis = [&params]
    {
        std::vector<double> x;
        for ( const Json::Value &v : params["x"] )
            x.push_back( v.asDouble() );
        return x;
    }();
    if ( static_cast<int>( xAxis.size() ) != meta.bandCount )
    {
        outcome.passed = false;
        outcome.observed["x_size"] = static_cast<Json::Int64>( xAxis.size() );
        outcome.observed["band_count"] = meta.bandCount;
        outcome.expected["x_size"] = meta.bandCount;
        outcome.message =
          QStringLiteral( "Declared x axis (%1) does not match the artifact band count (%2)" )
            .arg( xAxis.size() )
            .arg( meta.bandCount );
        return true;
    }

    const auto zoneReader = openSideRaster(
      resolveAgainstRules( rulesDir, QString::fromStdString( params["zones"]["path"].asString() ) ),
      QStringLiteral( "zones" ), spec.id, &usageError );
    if ( !zoneReader )
        return false;
    if ( zoneReader->metadata().width != meta.width
         || zoneReader->metadata().height != meta.height )
    {
        outcome.passed = false;
        outcome.message = QStringLiteral( "Zones grid does not match the artifact grid" );
        outcome.observed["artifact_width"] = meta.width;
        outcome.observed["artifact_height"] = meta.height;
        outcome.observed["zones_width"] = zoneReader->metadata().width;
        outcome.observed["zones_height"] = zoneReader->metadata().height;
        return true;
    }

    const bool hasZonesNoData = params.isMember( "zones_nodata" );
    const double zonesNoData = hasZonesNoData ? params["zones_nodata"].asDouble() : 0.0;

    // Per band: sum/sumsq per zone (sums feed per-band zone means).
    std::map<int, std::map<int, ZoneAcc>> perBand; // band -> zone -> acc
    std::vector<int> bands( static_cast<std::size_t>( meta.bandCount ) );
    for ( int b = 1; b <= meta.bandCount; ++b )
        bands[static_cast<std::size_t>( b - 1 )] = b;

    const BandInfo &zonesBandInfo = zoneReader->metadata().bands.at( 0 );
    std::vector<BandInfo> bandInfos;
    for ( int b = 1; b <= meta.bandCount; ++b )
        bandInfos.push_back( meta.bands.at( static_cast<std::size_t>( b - 1 ) ) );

    auto sink = [&]( const TileSlice &slice, const std::vector<double> &buffer,
                     const std::vector<double> &zonesBuf, const std::vector<double> & )
    {
        const std::size_t plane = static_cast<std::size_t>( slice.width ) * slice.height;
        // Positional indexing (see the note in runZoneStats): readWindow
        // returns the requested band list in order.
        std::size_t position = 0;
        for ( int band : bands )
        {
            const std::size_t offset = position * plane;
            ++position;
            const BandInfo &info = bandInfos[static_cast<std::size_t>( band - 1 )];
            for ( std::size_t p = 0; p < plane; ++p )
            {
                const double zone = zonesBuf[p];
                if ( isNonFinite( zone ) )
                    continue;
                if ( hasZonesNoData && zone == zonesNoData )
                    continue;
                const double v = buffer[offset + p];
                if ( bandIsNoData( info, v ) || isNonFinite( v ) )
                    continue;
                perBand[band][static_cast<int>( zone )].add( v );
            }
        }
    };
    if ( !walkWithSides( reader, bands, zoneReader.get(), { 1 }, nullptr, {}, maxBytes, sink,
                         artifactError ) )
        return false;

    // Per-zone pooled least-squares slope over the declared x axis. The
    // per-band zone means are weighted by their pixel counts, exactly as if
    // every contributing pixel had been regressed individually:
    //   slope = sum_b n_b (x_b - xw)(y_b - yw) / sum_b n_b (x_b - xw)^2
    // with the weighted axis mean xw = sum_b n_b x_b / sum_b n_b.
    auto slopeOf = [&]( int zone, bool *found ) -> double
    {
        *found = false;
        double nSum = 0.0, xwNum = 0.0;
        for ( int band : bands )
        {
            const auto bandIt = perBand.find( band );
            if ( bandIt == perBand.end() )
                continue;
            const auto zoneIt = bandIt->second.find( zone );
            if ( zoneIt == bandIt->second.end() )
                continue;
            const double n = static_cast<double>( zoneIt->second.n );
            nSum += n;
            xwNum += n * xAxis[static_cast<std::size_t>( band - 1 )];
        }
        if ( nSum == 0.0 )
            return 0.0;
        const double xMean = xwNum / nSum;
        double sxy = 0.0, sxx = 0.0;
        for ( int band : bands )
        {
            const auto bandIt = perBand.find( band );
            if ( bandIt == perBand.end() )
                continue;
            const auto zoneIt = bandIt->second.find( zone );
            if ( zoneIt == bandIt->second.end() )
                continue;
            const double n = static_cast<double>( zoneIt->second.n );
            const double dx = xAxis[static_cast<std::size_t>( band - 1 )] - xMean;
            sxx += n * dx * dx;
            sxy += n * dx * zoneIt->second.mean;
        }
        if ( sxx == 0.0 )
            return 0.0;
        *found = true;
        return sxy / sxx;
    };

    Json::Value slopes( Json::objectValue );
    const std::set<int> zonesOfInterest = [&params]
    {
        std::set<int> zones;
        if ( params.isMember( "bounds" ) )
            for ( const Json::Value &b : params["bounds"] )
                if ( b.isMember( "zone" ) )
                    zones.insert( b["zone"].asInt() );
        if ( params.isMember( "separations" ) )
            for ( const Json::Value &s : params["separations"] )
            {
                if ( s.isMember( "zone" ) )
                    zones.insert( s["zone"].asInt() );
                if ( s.isMember( "below_zone" ) )
                    zones.insert( s["below_zone"].asInt() );
            }
        return zones;
    }();
    for ( const int zone : zonesOfInterest )
    {
        bool found = false;
        const double slope = slopeOf( zone, &found );
        Json::Value entry( Json::objectValue );
        entry["slope"] = found ? Json::Value( slope ) : Json::Value( Json::nullValue );
        slopes[std::to_string( zone )] = entry;
    }
    outcome.observed["slopes"] = slopes;

    bool passed = true;
    QString firstFailure;
    if ( params.isMember( "bounds" ) )
    {
        for ( const Json::Value &bound : params["bounds"] )
        {
            if ( !bound.isMember( "zone" ) )
            {
                usageError = QStringLiteral( "Assertion \"%1\": bounds entries need zone" ).arg( spec.id );
                return false;
            }
            const int zone = bound["zone"].asInt();
            bool found = false;
            const double slope = slopeOf( zone, &found );
            if ( !found )
            {
                passed = false;
                firstFailure = QStringLiteral( "Zone %1 has no valid pixels" ).arg( zone );
                break;
            }
            if ( bound.isMember( "max" ) && slope > bound["max"].asDouble() )
            {
                passed = false;
                outcome.hasDelta = true;
                outcome.delta = slope - bound["max"].asDouble();
                firstFailure = QStringLiteral( "Zone %1 slope %2 > max %3" )
                                 .arg( zone )
                                 .arg( slope, 0, 'g', 12 )
                                 .arg( bound["max"].asDouble(), 0, 'g', 12 );
                break;
            }
            if ( bound.isMember( "min" ) && slope < bound["min"].asDouble() )
            {
                passed = false;
                outcome.hasDelta = true;
                outcome.delta = bound["min"].asDouble() - slope;
                firstFailure = QStringLiteral( "Zone %1 slope %2 < min %3" )
                                 .arg( zone )
                                 .arg( slope, 0, 'g', 12 )
                                 .arg( bound["min"].asDouble(), 0, 'g', 12 );
                break;
            }
        }
    }
    if ( passed && params.isMember( "separations" ) )
    {
        for ( const Json::Value &sep : params["separations"] )
        {
            const int zone = sep["zone"].asInt();
            const int below = sep["below_zone"].asInt();
            const double minDelta = sep["min_delta"].asDouble();
            bool f1 = false, f2 = false;
            const double sZone = slopeOf( zone, &f1 );
            const double sBelow = slopeOf( below, &f2 );
            if ( !f1 || !f2 )
            {
                passed = false;
                firstFailure = QStringLiteral( "Separation zones %1/%2 lack valid pixels" ).arg( below ).arg( zone );
                break;
            }
            const double actual = sBelow - sZone;
            if ( actual < minDelta )
            {
                passed = false;
                outcome.hasDelta = true;
                outcome.delta = actual - minDelta;
                firstFailure = QStringLiteral( "Separation %1 - %2 = %3 < min_delta %4" )
                                 .arg( below )
                                 .arg( zone )
                                 .arg( actual, 0, 'g', 12 )
                                 .arg( minDelta, 0, 'g', 12 );
                break;
            }
        }
    }
    outcome.passed = passed;
    if ( !passed )
        outcome.message = firstFailure;
    return true;
}

// ============================================================================
// band_layout
// ============================================================================

static bool runBandLayout( const RasterReader &reader, const LabKernelSpec &spec,
                           std::size_t maxBytes, LabKernelOutcome &outcome,
                           QString *artifactError, QString &usageError )
{
    const Json::Value &params = spec.params;
    const RasterMetadata &meta = reader.metadata();

    bool passed = true;
    QString firstFailure;
    Json::Value observed( Json::objectValue );
    observed["band_count"] = meta.bandCount;

    if ( params.isMember( "band_count" ) )
    {
        const int expected = params["band_count"].asInt();
        outcome.expected["band_count"] = expected;
        if ( meta.bandCount != expected )
        {
            passed = false;
            outcome.hasDelta = true;
            outcome.delta = meta.bandCount - expected;
            firstFailure = QStringLiteral( "Band count %1 != expected %2" )
                             .arg( meta.bandCount )
                             .arg( expected );
        }
    }

    std::vector<int> bands;
    if ( params.isMember( "bands" ) )
    {
        for ( const Json::Value &b : params["bands"] )
            bands.push_back( b.asInt() );
    }
    else
    {
        for ( int b = 1; b <= meta.bandCount; ++b )
            bands.push_back( b );
    }
    for ( const int band : bands )
    {
        if ( band < 1 || band > meta.bandCount )
        {
            usageError = QStringLiteral( "Assertion \"%1\": band %2 out of range" )
                            .arg( spec.id )
                            .arg( band );
            return false;
        }
    }

    if ( params.isMember( "min_valid_fraction" ) || params.isMember( "expected_valid_pixels" ) )
    {
        const double minFraction = params.isMember( "min_valid_fraction" )
                                     ? params["min_valid_fraction"].asDouble()
                                     : -1.0;
        const qint64 totalPixels =
          static_cast<qint64>( meta.width ) * static_cast<qint64>( meta.height );
        Json::Value fractions( Json::objectValue );
        std::map<int, qint64> validByBand;
        auto sink = [&]( const TileSlice &slice, const std::vector<double> &buffer,
                         const std::vector<double> &, const std::vector<double> & )
        {
            const std::size_t plane = static_cast<std::size_t>( slice.width ) * slice.height;
            for ( std::size_t i = 0; i < bands.size(); ++i )
            {
                const int band = bands[i];
                const BandInfo &info = meta.bands.at( static_cast<std::size_t>( band - 1 ) );
                const std::size_t offset = i * plane;
                for ( std::size_t p = 0; p < plane; ++p )
                {
                    const double v = buffer[offset + p];
                    if ( bandIsNoData( info, v ) || isNonFinite( v ) )
                        continue;
                    ++validByBand[band];
                }
            }
        };
        if ( !walkWithSides( reader, bands, nullptr, {}, nullptr, {}, maxBytes, sink,
                             artifactError ) )
            return false;

        for ( const int band : bands )
        {
            const qint64 valid = validByBand[band];
            const double fraction =
              totalPixels > 0 ? static_cast<double>( valid ) / static_cast<double>( totalPixels )
                              : 0.0;
            fractions[std::to_string( band )] = fraction;
            if ( minFraction >= 0.0 && fraction < minFraction )
            {
                passed = false;
                outcome.hasDelta = true;
                outcome.delta = minFraction - fraction;
                if ( firstFailure.isEmpty() )
                    firstFailure = QStringLiteral(
                                     "Band %1 valid fraction %2 < min_valid_fraction %3" )
                                     .arg( band )
                                     .arg( fraction, 0, 'g', 12 )
                                     .arg( minFraction, 0, 'g', 12 );
            }
            if ( params.isMember( "expected_valid_pixels" )
                 && params["expected_valid_pixels"].isMember( std::to_string( band ) ) )
            {
                const Json::Int64 expected =
                  params["expected_valid_pixels"][std::to_string( band )].asInt64();
                if ( valid != expected )
                {
                    passed = false;
                    outcome.hasDelta = true;
                    outcome.delta = static_cast<double>( valid - expected );
                    if ( firstFailure.isEmpty() )
                        firstFailure = QStringLiteral(
                                         "Band %1 valid pixels %2 != expected %3" )
                                         .arg( band )
                                         .arg( valid )
                                         .arg( expected );
                }
            }
        }
        observed["valid_fraction_per_band"] = fractions;
    }

    outcome.observed = observed;
    outcome.passed = passed;
    if ( !passed )
        outcome.message = firstFailure.isEmpty() ? QStringLiteral( "band_layout violated" )
                                                 : firstFailure;
    return true;
}

// ============================================================================
// spatial_agreement (position-sensitive)
// ============================================================================

static bool runSpatialAgreement( const RasterReader &reader, const LabKernelSpec &spec,
                                 const QString &rulesDir, std::size_t maxBytes,
                                 LabKernelOutcome &outcome, QString *artifactError,
                                 QString &usageError )
{
    const Json::Value &params = spec.params;
    const RasterMetadata &meta = reader.metadata();
    const int band = params.isMember( "band" ) ? params["band"].asInt() : 1;
    if ( band < 1 || band > meta.bandCount )
    {
        outcome.passed = false;
        outcome.message = QStringLiteral( "Artifact lacks band %1" ).arg( band );
        outcome.observed["band_count"] = meta.bandCount;
        outcome.expected["band"] = band;
        return true;
    }

    const auto truthReader = openSideRaster(
      resolveAgainstRules( rulesDir, QString::fromStdString( params["truth"]["path"].asString() ) ),
      QStringLiteral( "truth" ), spec.id, &usageError );
    if ( !truthReader )
        return false;
    if ( truthReader->metadata().width != meta.width
         || truthReader->metadata().height != meta.height )
    {
        outcome.passed = false;
        outcome.message = QStringLiteral( "Truth grid does not match the artifact grid" );
        outcome.observed["artifact_width"] = meta.width;
        outcome.observed["artifact_height"] = meta.height;
        outcome.observed["truth_width"] = truthReader->metadata().width;
        outcome.observed["truth_height"] = truthReader->metadata().height;
        return true;
    }

    const BandInfo &artifactInfo = meta.bands.at( static_cast<std::size_t>( band - 1 ) );
    const BandInfo &truthInfo = truthReader->metadata().bands.at( 0 );
    const bool hasTruthNoData = params.isMember( "truth_nodata" );
    const double truthNoData = hasTruthNoData ? params["truth_nodata"].asDouble() : 0.0;

    const bool zoneMode = params.isMember( "zone_expected" );
    // Binary mode is valid with EITHER bound (hit and false-alarm may be
    // split across two assertions) — same vocabulary as the validator.
    const bool binaryMode = params.isMember( "min_hit_rate" )
                            || params.isMember( "max_false_alarm_rate" );
    const bool continuousMode = params.isMember( "tolerance" );

    // mode accumulators
    std::map<int, ZoneAcc> zoneCorrect;   // zone -> Welford over {0,1}
    std::map<int, qint64> zoneTotal;
    qint64 truthPositives = 0, hits = 0, falseAlarms = 0, truthNegatives = 0;
    qint64 compared = 0, exceeded = 0;
    const double tolerance = continuousMode ? params["tolerance"].asDouble() : 0.0;

    auto sink = [&]( const TileSlice &slice, const std::vector<double> &buffer,
                     const std::vector<double> &truthBuf, const std::vector<double> & )
    {
        const std::size_t plane = static_cast<std::size_t>( slice.width ) * slice.height;
        for ( std::size_t p = 0; p < plane; ++p )
        {
            const double tv = truthBuf[p];
            if ( isNonFinite( tv ) || bandIsNoData( truthInfo, tv ) )
                continue;
            if ( hasTruthNoData && tv == truthNoData )
                continue;
            const double av = buffer[p];
            if ( isNonFinite( av ) || bandIsNoData( artifactInfo, av ) )
                continue;
            if ( zoneMode )
            {
                const int zone = static_cast<int>( tv );
                const Json::Value &expected = params["zone_expected"];
                bool found = false;
                double label = 0.0;
                for ( const Json::Value &entry : expected )
                {
                    if ( entry.isMember( "zone" ) && entry["zone"].asInt() == zone )
                    {
                        label = entry.isMember( "label" ) ? entry["label"].asDouble() : 0.0;
                        found = true;
                        break;
                    }
                }
                if ( !found )
                    continue;
                ++zoneTotal[zone];
                zoneCorrect[zone].add( std::fabs( av - label ) < 1e-9 ? 1.0 : 0.0 );
            }
            else if ( binaryMode )
            {
                const double truthPositive =
                  params.isMember( "truth_positive" ) ? params["truth_positive"].asDouble() : 1.0;
                const double artifactPositive =
                  params.isMember( "artifact_positive" ) ? params["artifact_positive"].asDouble()
                                                          : 1.0;
                const bool tPos = tv == truthPositive;
                const bool aPos = av == artifactPositive;
                if ( tPos )
                {
                    ++truthPositives;
                    if ( aPos )
                        ++hits;
                }
                else
                {
                    ++truthNegatives;
                    if ( aPos )
                        ++falseAlarms;
                }
            }
            else if ( continuousMode )
            {
                ++compared;
                if ( std::fabs( av - tv ) > tolerance )
                    ++exceeded;
            }
        }
    };
    if ( !walkWithSides( reader, { band }, truthReader.get(), { 1 }, nullptr, {}, maxBytes,
                         sink, artifactError ) )
        return false;

    bool passed = true;
    QString firstFailure;

    if ( zoneMode )
    {
        const double minAccuracy = params["min_accuracy"].asDouble();
        Json::Value perZone( Json::objectValue );
        for ( const Json::Value &entry : params["zone_expected"] )
        {
            const int zone = entry["zone"].asInt();
            const qint64 total = zoneTotal[zone];
            const double accuracy =
              total > 0 ? zoneCorrect[zone].mean : 0.0;
            Json::Value cell( Json::objectValue );
            cell["accuracy"] = accuracy;
            cell["n"] = static_cast<Json::Int64>( total );
            perZone[std::to_string( zone )] = cell;
            if ( total == 0 )
            {
                passed = false;
                firstFailure = QStringLiteral( "Zone %1 has no valid pixels" ).arg( zone );
            }
            else if ( accuracy < minAccuracy )
            {
                passed = false;
                outcome.hasDelta = true;
                outcome.delta = minAccuracy - accuracy;
                if ( firstFailure.isEmpty() )
                    firstFailure = QStringLiteral( "Zone %1 accuracy %2 < min %3" )
                                     .arg( zone )
                                     .arg( accuracy, 0, 'g', 12 )
                                     .arg( minAccuracy, 0, 'g', 12 );
            }
        }
        outcome.observed["accuracy_per_zone"] = perZone;
        outcome.expected["min_accuracy"] = params["min_accuracy"].asDouble();
    }
    else if ( binaryMode )
    {
        // Each bound is optional (they may live in separate assertions); the
        // evidence always reports the measured rates.
        const bool hasHitBound = params.isMember( "min_hit_rate" );
        const double minHitRate =
          hasHitBound ? params["min_hit_rate"].asDouble()
                      : std::numeric_limits<double>::infinity();
        const double hitRate =
          truthPositives > 0 ? static_cast<double>( hits ) / static_cast<double>( truthPositives )
                             : 0.0;
        outcome.observed["hit_rate"] = hitRate;
        outcome.observed["hits"] = static_cast<Json::Int64>( hits );
        outcome.observed["truth_positives"] = static_cast<Json::Int64>( truthPositives );
        if ( hasHitBound )
            outcome.expected["min_hit_rate"] = minHitRate;
        if ( hasHitBound && truthPositives == 0 )
        {
            passed = false;
            firstFailure = QStringLiteral( "Truth contains no positive pixels" );
        }
        else if ( hasHitBound && hitRate < minHitRate )
        {
            passed = false;
            outcome.hasDelta = true;
            outcome.delta = minHitRate - hitRate;
            firstFailure = QStringLiteral( "Hit rate %1 < min %2" )
                             .arg( hitRate, 0, 'g', 12 )
                             .arg( minHitRate, 0, 'g', 12 );
        }
        if ( params.isMember( "max_false_alarm_rate" ) )
        {
            const double maxFar = params["max_false_alarm_rate"].asDouble();
            const double far = truthNegatives > 0
                                 ? static_cast<double>( falseAlarms )
                                     / static_cast<double>( truthNegatives )
                                 : 0.0;
            outcome.observed["false_alarm_rate"] = far;
            outcome.observed["false_alarms"] = static_cast<Json::Int64>( falseAlarms );
            outcome.expected["max_false_alarm_rate"] = maxFar;
            if ( far > maxFar )
            {
                passed = false;
                outcome.hasDelta = true;
                outcome.delta = std::max( outcome.delta, far - maxFar );
                if ( firstFailure.isEmpty() )
                    firstFailure = QStringLiteral( "False alarm rate %1 > max %2" )
                                     .arg( far, 0, 'g', 12 )
                                     .arg( maxFar, 0, 'g', 12 );
            }
        }
    }
    else if ( continuousMode )
    {
        const double maxExceed = params["max_exceed_fraction"].asDouble();
        const double fraction =
          compared > 0 ? static_cast<double>( exceeded ) / static_cast<double>( compared ) : 0.0;
        outcome.observed["exceed_fraction"] = fraction;
        outcome.observed["compared"] = static_cast<Json::Int64>( compared );
        outcome.observed["exceeded"] = static_cast<Json::Int64>( exceeded );
        outcome.expected["max_exceed_fraction"] = maxExceed;
        outcome.expected["tolerance"] = tolerance;
        if ( fraction > maxExceed )
        {
            passed = false;
            outcome.hasDelta = true;
            outcome.delta = fraction - maxExceed;
            firstFailure = QStringLiteral( "Exceed fraction %1 > max %2" )
                             .arg( fraction, 0, 'g', 12 )
                             .arg( maxExceed, 0, 'g', 12 );
        }
    }

    outcome.passed = passed;
    if ( !passed )
        outcome.message = firstFailure.isEmpty() ? QStringLiteral( "spatial agreement violated" )
                                                 : firstFailure;
    return true;
}

// ============================================================================
// spectral_signature
// ============================================================================

static bool runSpectralSignature( const RasterReader &reader, const LabKernelSpec &spec,
                                  const QString &rulesDir, std::size_t maxBytes,
                                  LabKernelOutcome &outcome, QString *artifactError,
                                  QString &usageError )
{
    const Json::Value &params = spec.params;
    const RasterMetadata &meta = reader.metadata();

    std::vector<int> bands;
    for ( const Json::Value &b : params["bands"] )
        bands.push_back( b.asInt() );
    struct Reference
    {
        QString name;
        std::vector<double> spectrum;
    };
    std::vector<Reference> references;
    for ( const Json::Value &ref : params["references"] )
    {
        Reference r;
        r.name = QString::fromStdString( ref.isMember( "name" ) ? ref["name"].asString() : "" );
        for ( const Json::Value &v : ref["spectrum"] )
            r.spectrum.push_back( v.asDouble() );
        references.push_back( std::move( r ) );
    }
    for ( const int band : bands )
    {
        if ( band < 1 || band > meta.bandCount )
        {
            usageError = QStringLiteral( "Assertion \"%1\": band %2 out of range" )
                            .arg( spec.id )
                            .arg( band );
            return false;
        }
    }
    if ( bands.size() != references.front().spectrum.size() )
    {
        usageError = QStringLiteral(
                        "Assertion \"%1\": bands count (%2) must match reference spectrum length (%3)" )
                        .arg( spec.id )
                        .arg( bands.size() )
                        .arg( references.front().spectrum.size() );
        return false;
    }

    const bool useZones = params.isMember( "zones" );
    std::unique_ptr<RasterReader> zoneReader;
    if ( useZones )
    {
        zoneReader = openSideRaster(
          resolveAgainstRules( rulesDir,
                               QString::fromStdString( params["zones"]["path"].asString() ) ),
          QStringLiteral( "zones" ), spec.id, &usageError );
        if ( !zoneReader )
            return false;
        if ( zoneReader->metadata().width != meta.width
             || zoneReader->metadata().height != meta.height )
        {
            outcome.passed = false;
            outcome.message = QStringLiteral( "Zones grid does not match the artifact grid" );
            return true;
        }
    }
    // zone -> reference index -> angle accumulator
    struct AngleAcc
    {
        qint64 n = 0;
        double sum = 0.0;
        double max = 0.0;
    };
    std::map<std::pair<int, int>, AngleAcc> angles;
    std::map<int, AngleAcc> unzoned;

    const bool hasZonesNoData = params.isMember( "zones_nodata" );
    const double zonesNoData = hasZonesNoData ? params["zones_nodata"].asDouble() : 0.0;

    auto sink = [&]( const TileSlice &slice, const std::vector<double> &buffer,
                     const std::vector<double> &zonesBuf, const std::vector<double> & )
    {
        const std::size_t plane = static_cast<std::size_t>( slice.width ) * slice.height;
        for ( std::size_t p = 0; p < plane; ++p )
        {
            int zone = -1;
            if ( useZones )
            {
                const double z = zonesBuf[p];
                if ( isNonFinite( z ) )
                    continue;
                if ( hasZonesNoData && z == zonesNoData )
                    continue;
                zone = static_cast<int>( z );
            }
            // zone_expected -> reference index for this zone (default 0)
            int referenceIndex = 0;
            if ( useZones && params.isMember( "zone_reference" ) )
            {
                for ( const Json::Value &entry : params["zone_reference"] )
                {
                    if ( entry.isMember( "zone" ) && entry["zone"].asInt() == zone )
                    {
                        referenceIndex =
                          entry.isMember( "reference" ) ? entry["reference"].asInt() : 0;
                        break;
                    }
                }
            }
            const Reference &ref = references[static_cast<std::size_t>( referenceIndex )];
            double dot = 0.0, normV = 0.0, normR = 0.0;
            bool skip = false;
            for ( std::size_t i = 0; i < bands.size(); ++i )
            {
                const BandInfo &info =
                  meta.bands.at( static_cast<std::size_t>( bands[i] - 1 ) );
                const double v = buffer[static_cast<std::size_t>( i ) * plane + p];
                if ( bandIsNoData( info, v ) || isNonFinite( v ) )
                {
                    skip = true;
                    break;
                }
                const double r = ref.spectrum[i];
                dot += v * r;
                normV += v * v;
                normR += r * r;
            }
            if ( skip || normV == 0.0 || normR == 0.0 )
                continue;
            double cosAngle = dot / ( std::sqrt( normV ) * std::sqrt( normR ) );
            cosAngle = std::clamp( cosAngle, -1.0, 1.0 );
            const double degrees =
              std::acos( cosAngle ) * 180.0 / 3.14159265358979323846;
            auto &acc = useZones ? angles[{ zone, referenceIndex }] : unzoned[referenceIndex];
            ++acc.n;
            acc.sum += degrees;
            acc.max = std::max( acc.max, degrees );
        }
    };
    if ( !walkWithSides( reader, bands, useZones ? zoneReader.get() : nullptr, { 1 }, nullptr,
                         {}, maxBytes, sink, artifactError ) )
        return false;

    const bool hasMeanBound = params.isMember( "sam_max_mean_degrees" );
    const double maxMean =
      hasMeanBound ? params["sam_max_mean_degrees"].asDouble()
                   : std::numeric_limits<double>::infinity();
    const bool hasSingleBound = params.isMember( "sam_max_degrees" );
    const double maxSingle = hasSingleBound
                               ? params["sam_max_degrees"].asDouble()
                               : std::numeric_limits<double>::infinity();

    Json::Value perKey( Json::objectValue );
    bool passed = true;
    QString firstFailure;
    const auto evaluate = [&]( const std::pair<int, int> &key, const AngleAcc &acc )
    {
        Json::Value cell( Json::objectValue );
        cell["n"] = static_cast<Json::Int64>( acc.n );
        if ( acc.n > 0 )
        {
            cell["sam_mean_degrees"] = acc.sum / static_cast<double>( acc.n );
            cell["sam_max_degrees"] = acc.max;
        }
        perKey[std::to_string( key.first ) + ( key.second >= 0 ? ":" : "" )
               + ( key.second >= 0 ? std::to_string( key.second ) : std::string() )] = cell;
        if ( acc.n == 0 )
        {
            passed = false;
            if ( firstFailure.isEmpty() )
                firstFailure = QStringLiteral( "No valid pixels for SAM evaluation" );
            return;
        }
        const double mean = acc.sum / static_cast<double>( acc.n );
        if ( hasMeanBound && mean > maxMean )
        {
            passed = false;
            outcome.hasDelta = true;
            outcome.delta = std::max( outcome.delta, mean - maxMean );
            if ( firstFailure.isEmpty() )
                firstFailure =
                  QStringLiteral( "Mean SAM angle %1 > max %2" )
                    .arg( mean, 0, 'g', 12 )
                    .arg( maxMean, 0, 'g', 12 );
        }
        if ( hasSingleBound && acc.max > maxSingle )
        {
            passed = false;
            outcome.hasDelta = true;
            outcome.delta = std::max( outcome.delta, acc.max - maxSingle );
            if ( firstFailure.isEmpty() )
                firstFailure = QStringLiteral( "Max SAM angle %1 > max %2" )
                                 .arg( acc.max, 0, 'g', 12 )
                                 .arg( maxSingle, 0, 'g', 12 );
        }
    };
    if ( useZones )
    {
        for ( const auto &entry : angles )
            evaluate( entry.first, entry.second );
    }
    else
    {
        for ( const auto &entry : unzoned )
            evaluate( { -1, entry.first }, entry.second );
    }
    outcome.observed["sam_per_zone_reference"] = perKey;
    outcome.passed = passed;
    if ( !passed )
        outcome.message = firstFailure.isEmpty() ? QStringLiteral( "SAM angle exceeded" )
                                                 : firstFailure;
    return true;
}

// ============================================================================
// file_check (artifact.kind == "file") — existence, bytes, PNG page geometry,
// MapSpec validation. No raster is opened: file-mode rules never reach the
// raster path in gradeForTeaching.
// ============================================================================

namespace {

/// PNG magic + IHDR width/height (same trust level as the lab report
/// thumbnail validation: parse the header, never decode pixels).
bool pngDimensions( const QByteArray &bytes, qint64 *width, qint64 *height )
{
    static const unsigned char kPngMagic[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    if ( bytes.size() < 33
         || std::memcmp( bytes.constData(), kPngMagic, sizeof( kPngMagic ) ) != 0 )
        return false;
    // IHDR is always the first chunk: length(4) + "IHDR" + width(4) + height(4).
    if ( std::memcmp( bytes.constData() + 12, "IHDR", 4 ) != 0 )
        return false;
    auto readU32 = []( const unsigned char *p ) -> qint64
    {
        return ( static_cast<qint64>( p[0] ) << 24 ) | ( static_cast<qint64>( p[1] ) << 16 )
               | ( static_cast<qint64>( p[2] ) << 8 ) | static_cast<qint64>( p[3] );
    };
    *width = readU32( reinterpret_cast<const unsigned char *>( bytes.constData() ) + 16 );
    *height = readU32( reinterpret_cast<const unsigned char *>( bytes.constData() ) + 20 );
    return *width > 0 && *height > 0;
}

} // namespace

static bool runFileCheck( const QString &artifactPath, const LabKernelSpec &spec,
                          const QString &rulesDir, LabKernelOutcome &outcome,
                          QString *usageError )
{
    const Json::Value &params = spec.params;

    // The checked file is the artifact itself unless the assertion names a
    // sibling (relative to the artifact's directory).
    QString path = artifactPath;
    if ( params.isMember( "path" ) && !params["path"].asString().empty() )
    {
        const QString ref = QString::fromStdString( params["path"].asString() );
        path = QFileInfo( ref ).isAbsolute()
                 ? ref
                 : QDir( QFileInfo( artifactPath ).absolutePath() ).filePath( ref );
        // Sibling resolution stays INSIDE the artifact directory: a rules file
        // must not probe unrelated files through the graded transcript.
        const QString probe = QFileInfo( path ).absolutePath();
        const QString bound = QFileInfo( artifactPath ).absolutePath();
        if ( !probe.startsWith( bound ) )
        {
            outcome.passed = false;
            outcome.message =
              QStringLiteral( "file_check path escapes the submission directory" );
            outcome.observed["path"] = path.toStdString();
            outcome.expected["within"] = bound.toStdString();
            return true;
        }
    }

    bool passed = true;
    QString firstFailure;
    outcome.observed["path"] = path.toStdString();

    if ( params.isMember( "exists" ) && params["exists"].asBool() && !QFile::exists( path ) )
    {
        passed = false;
        firstFailure = QStringLiteral( "File does not exist: %1" ).arg( path );
    }

    qint64 bytes = 0;
    if ( passed && ( params.isMember( "min_bytes" ) || params.isMember( "max_bytes" ) ) )
    {
        bytes = QFileInfo( path ).size();
        outcome.observed["bytes"] = static_cast<Json::Int64>( bytes );
        if ( params.isMember( "min_bytes" ) && bytes < params["min_bytes"].asInt64() )
        {
            passed = false;
            firstFailure = QStringLiteral( "File is %1 bytes, below min %2" )
                             .arg( bytes )
                             .arg( params["min_bytes"].asInt64() );
        }
        if ( params.isMember( "max_bytes" ) && bytes > params["max_bytes"].asInt64() )
        {
            passed = false;
            firstFailure = QStringLiteral( "File is %1 bytes, above max %2" )
                             .arg( bytes )
                             .arg( params["max_bytes"].asInt64() );
        }
    }

    QByteArray payload;
    const bool needPayload =
      params.isMember( "png" ) || params.isMember( "mapspec" );
    if ( passed && needPayload )
    {
        QFile file( path );
        if ( !file.open( QIODevice::ReadOnly ) )
        {
            passed = false;
            firstFailure = QStringLiteral( "Cannot read file: %1" ).arg( path );
        }
        else
        {
            payload = file.readAll();
        }
    }

    if ( passed && params.isMember( "png" ) )
    {
        const Json::Value &png = params["png"];
        qint64 width = 0, height = 0;
        if ( !pngDimensions( payload, &width, &height ) )
        {
            passed = false;
            firstFailure = QStringLiteral( "File is not a readable PNG" );
        }
        else
        {
            outcome.observed["png_width_px"] = static_cast<Json::Int64>( width );
            outcome.observed["png_height_px"] = static_cast<Json::Int64>( height );
            const double dpi = png.get( "dpi", 200.0 ).asDouble();
            const double tolerance = png.get( "size_tolerance", 0.05 ).asDouble();
            const double mmPerInch = 25.4;
            const double declaredW = png.get( "page_width_mm", 297.0 ).asDouble();
            const double declaredH = png.get( "page_height_mm", 210.0 ).asDouble();
            const double impliedW = static_cast<double>( width ) / dpi * mmPerInch;
            const double impliedH = static_cast<double>( height ) / dpi * mmPerInch;
            const bool wOk =
              std::fabs( impliedW - declaredW ) <= declaredW * tolerance;
            const bool hOk =
              std::fabs( impliedH - declaredH ) <= declaredH * tolerance;
            if ( !wOk || !hOk )
            {
                passed = false;
                firstFailure = QStringLiteral(
                                 "PNG page %1x%2 mm (at %3 dpi) does not match declared %4x%5 mm +- %6%" )
                                 .arg( impliedW, 0, 'f', 1 )
                                 .arg( impliedH, 0, 'f', 1 )
                                 .arg( dpi, 0, 'f', 0 )
                                 .arg( declaredW, 0, 'f', 1 )
                                 .arg( declaredH, 0, 'f', 1 )
                                 .arg( tolerance * 100.0, 0, 'f', 1 );
            }
        }
    }

    if ( passed && params.isMember( "preflight" ) )
    {
        // Spec-level cartography preflight (the platform's own rule catalog):
        // structural problems count directly, forbidden codes (missing
        // title/legend/scale bar/...) are each a graded failure.
        const Json::Value &preflightParams = params["preflight"];
        Json::Value specJson;
        Json::CharReaderBuilder builder;
        std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
        std::string parseErrors;
        if ( !reader->parse( payload.constData(), payload.constData() + payload.size(),
                             &specJson, &parseErrors ) )
        {
            passed = false;
            firstFailure = QStringLiteral( "Artifact is not parseable JSON: %1" )
                             .arg( QString::fromStdString( parseErrors ) );
        }
        else
        {
            const Json::Value report = sicnu::agent::cartography::preflightMapSpec( specJson );
            Json::Value codes( Json::arrayValue );
            int problems = 0;
            for ( const auto &issueEntry : report["issues"] )
            {
                const std::string code = issueEntry["code"].asString();
                codes.append( code );
                ++problems;
                const Json::Value &forbidden = preflightParams["forbidden_codes"];
                for ( const auto &needle : forbidden )
                {
                    if ( code == needle.asString() )
                    {
                        passed = false;
                        if ( firstFailure.isEmpty() )
                            firstFailure = QStringLiteral( "Preflight forbidden code %1" )
                                             .arg( QString::fromStdString( code ) );
                    }
                }
            }
            outcome.observed["preflight_problems"] = problems;
            outcome.observed["preflight_codes"] = codes;
            outcome.observed["quality_score"] = report["quality_score"];
            const int maxProblems = preflightParams.get( "max_problems", 0 ).asInt();
            if ( problems > maxProblems )
            {
                passed = false;
                if ( firstFailure.isEmpty() )
                    firstFailure = QStringLiteral( "Preflight reported %1 problems (max %2)" )
                                     .arg( problems )
                                     .arg( maxProblems );
            }
        }
    }

    if ( passed && params.isMember( "mapspec" ) )
    {
        const Json::Value &mapspecParams = params["mapspec"];
        Json::Value specJson;
        Json::CharReaderBuilder builder;
        std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );
        std::string parseErrors;
        if ( !reader->parse( payload.constData(), payload.constData() + payload.size(),
                             &specJson, &parseErrors ) )
        {
            passed = false;
            firstFailure = QStringLiteral( "Artifact is not parseable JSON: %1" )
                             .arg( QString::fromStdString( parseErrors ) );
        }
        else
        {
            const std::vector<std::string> problems =
              sicnu::agent::mapspec::validateMapSpec( specJson );
            outcome.observed["mapspec_problems"] =
              static_cast<Json::Int64>( problems.size() );
            Json::Value codes( Json::arrayValue );
            for ( const std::string &problem : problems )
                codes.append( problem );
            outcome.observed["mapspec_problem_list"] = codes;
            const int maxProblems = mapspecParams.get( "max_problems", 0 ).asInt();
            if ( static_cast<int>( problems.size() ) > maxProblems )
            {
                passed = false;
                firstFailure = QStringLiteral( "MapSpec validation reported %1 problems (max %2)" )
                                 .arg( problems.size() )
                                 .arg( maxProblems );
            }
            if ( mapspecParams.isMember( "forbidden_codes" ) )
            {
                for ( const Json::Value &forbidden : mapspecParams["forbidden_codes"] )
                {
                    const std::string needle = forbidden.asString();
                    for ( const std::string &problem : problems )
                    {
                        if ( problem.find( needle ) != std::string::npos )
                        {
                            passed = false;
                            if ( firstFailure.isEmpty() )
                                firstFailure = QStringLiteral(
                                                 "MapSpec problem contains forbidden code %1" )
                                                 .arg( QString::fromStdString( needle ) );
                        }
                    }
                }
            }
        }
    }

    outcome.passed = passed;
    // The evidence contract: every deduction carries {observed, expected}.
    if ( params.isMember( "exists" ) )
        outcome.expected["exists"] = params["exists"].asBool();
    if ( params.isMember( "min_bytes" ) )
        outcome.expected["min_bytes"] = static_cast<Json::Int64>( params["min_bytes"].asInt64() );
    if ( params.isMember( "max_bytes" ) )
        outcome.expected["max_bytes"] = static_cast<Json::Int64>( params["max_bytes"].asInt64() );
    if ( params.isMember( "png" ) )
        outcome.expected["png"] = params["png"];
    if ( params.isMember( "mapspec" ) )
        outcome.expected["mapspec"] = params["mapspec"];
    if ( params.isMember( "preflight" ) )
        outcome.expected["preflight"] = params["preflight"];
    if ( !passed )
        outcome.message = firstFailure.isEmpty() ? QStringLiteral( "file_check violated" )
                                                 : firstFailure;
    ( void )rulesDir;
    return true;
}

/// provenance — the artifact must carry the foundry's dataset metadata
/// (SICNU_GENERATOR / _SEED / _PRODUCT / _PROFILE / _GENERATOR_VERSION), so a
/// submission computed from a different (or tampered) input can be refused by
/// the rules themselves. Metadata is read from the already-open raster; every
/// declared expectation must be present and exactly equal (string compare;
/// seed compares as decimal integer).
bool runProvenance( const RasterReader &reader, const LabKernelSpec &spec,
                    LabKernelOutcome &outcome, QString *artifactError, QString &usageError )
{
    ( void )artifactError;
    ( void )usageError;
    const Json::Value &params = spec.params;
    const RasterMetadata &meta = reader.metadata();
    const auto item = [ &meta ]( const char *key ) {
        const auto it = meta.metadata.find( key );
        return it == meta.metadata.end() ? QString() : QString::fromStdString( it->second );
    };

    QString firstFailure;
    const auto failWith = [ &firstFailure ]( const QString &message )
    {
        if ( firstFailure.isEmpty() )
            firstFailure = message;
    };

    if ( params.isMember( "generator" ) )
    {
        static const QString kKey = QStringLiteral( "SICNU_GENERATOR" );
        const QString observed = item( "SICNU_GENERATOR" );
        const QString expected = QString::fromStdString( params["generator"].asString() );
        if ( observed.isEmpty() )
            failWith( QStringLiteral( "missing provenance metadata %1" ).arg( kKey ) );
        else if ( observed != expected )
            failWith( QStringLiteral( "SICNU_GENERATOR mismatch" ) );
        outcome.observed[ "generator" ] = observed.toStdString();
        outcome.expected[ "generator" ] = expected.toStdString();
    }
    if ( params.isMember( "seed" ) )
    {
        const QString observed = item( "SICNU_SEED" );
        const QString expected = QString::number( params["seed"].asInt64() );
        if ( observed.isEmpty() )
            failWith( QStringLiteral( "missing provenance metadata SICNU_SEED" ) );
        else if ( observed != expected )
            failWith( QStringLiteral( "SICNU_SEED mismatch" ) );
        outcome.observed[ "seed" ] = observed.toStdString();
        outcome.expected[ "seed" ] = expected.toStdString();
    }
    if ( params.isMember( "product" ) )
    {
        const QString observed = item( "SICNU_PRODUCT" );
        const QString expected = QString::fromStdString( params["product"].asString() );
        if ( observed.isEmpty() )
            failWith( QStringLiteral( "missing provenance metadata SICNU_PRODUCT" ) );
        else if ( observed != expected )
            failWith( QStringLiteral( "SICNU_PRODUCT mismatch" ) );
        outcome.observed[ "product" ] = observed.toStdString();
        outcome.expected[ "product" ] = expected.toStdString();
    }
    if ( params.isMember( "profile" ) )
    {
        const QString observed = item( "SICNU_PROFILE" );
        const QString expected = QString::fromStdString( params["profile"].asString() );
        if ( observed.isEmpty() )
            failWith( QStringLiteral( "missing provenance metadata SICNU_PROFILE" ) );
        else if ( observed != expected )
            failWith( QStringLiteral( "SICNU_PROFILE mismatch" ) );
        outcome.observed[ "profile" ] = observed.toStdString();
        outcome.expected[ "profile" ] = expected.toStdString();
    }
    if ( params.isMember( "version" ) )
    {
        // The foundry stamps SICNU_GENERATOR_VERSION (sample_foundry.cpp),
        // not SICNU_VERSION — the two spellings must not drift.
        const QString observed = item( "SICNU_GENERATOR_VERSION" );
        const QString expected = QString::fromStdString( params["version"].asString() );
        if ( observed.isEmpty() )
            failWith( QStringLiteral( "missing provenance metadata SICNU_GENERATOR_VERSION" ) );
        else if ( observed != expected )
            failWith( QStringLiteral( "SICNU_GENERATOR_VERSION mismatch" ) );
        outcome.observed[ "version" ] = observed.toStdString();
        outcome.expected[ "version" ] = expected.toStdString();
    }

    outcome.passed = firstFailure.isEmpty();
    if ( !outcome.passed )
        outcome.message = firstFailure;
    return true;
}

bool runLabKernelWalks( const sicnu::geo::RasterReader &reader,
                        const std::vector<LabKernelSpec> &specs, const QString &rulesDir,
                        std::size_t maxBytes, std::map<QString, LabKernelOutcome> &outcomes,
                        bool *usageClass, QString *error )
{
    for ( const LabKernelSpec &spec : specs )
    {
        LabKernelOutcome outcome;
        QString usageError;
        bool ok = false;
        if ( spec.kind == QLatin1String( "zone_stats" ) )
            ok = runZoneStats( reader, spec, rulesDir, maxBytes, outcome, error, usageError );
        else if ( spec.kind == QLatin1String( "series_separation" ) )
            ok = runSeriesSeparation( reader, spec, rulesDir, maxBytes, outcome, error,
                                      usageError );
        else if ( spec.kind == QLatin1String( "band_layout" ) )
            ok = runBandLayout( reader, spec, maxBytes, outcome, error, usageError );
        else if ( spec.kind == QLatin1String( "spatial_agreement" ) )
            ok = runSpatialAgreement( reader, spec, rulesDir, maxBytes, outcome, error,
                                      usageError );
        else if ( spec.kind == QLatin1String( "spectral_signature" ) )
            ok = runSpectralSignature( reader, spec, rulesDir, maxBytes, outcome, error,
                                       usageError );
        else if ( spec.kind == QLatin1String( "provenance" ) )
            ok = runProvenance( reader, spec, outcome, error, usageError );
        else
        {
            *error = QStringLiteral( "Unknown kernel kind \"%1\"" ).arg( spec.kind );
            *usageClass = true;
            return false;
        }
        if ( !ok )
        {
            *usageClass = !usageError.isEmpty();
            if ( *usageClass )
                *error = usageError;
            return false; // artifact errors already sit in *error
        }
        outcomes[spec.id] = std::move( outcome );
    }
    return true;
}

bool runLabFileChecks( const QString &artifactPath, const std::vector<LabKernelSpec> &specs,
                       const QString &rulesDir, std::map<QString, LabKernelOutcome> &outcomes,
                       QString *usageError )
{
    for ( const LabKernelSpec &spec : specs )
    {
        LabKernelOutcome outcome;
        if ( !runFileCheck( artifactPath, spec, rulesDir, outcome, usageError ) )
            return false;
        outcomes[spec.id] = std::move( outcome );
    }
    return true;
}

} // namespace sicnu::agent
