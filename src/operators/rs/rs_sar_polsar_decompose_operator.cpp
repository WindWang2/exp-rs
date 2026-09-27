/***************************************************************************
 * rs_sar_polsar_decompose_operator.cpp — full-polarimetric decomposition
 * (Advanced SAR / PolSAR / InSAR 10.0, package B).
 *
 * Streams the complex channel raster tile-by-tile (halo = window radius),
 * accumulates the per-pixel ensemble covariance over the window, runs the
 * requested decomposition kernel, and writes the product bands tile-by-tile
 * — O(tile · window²) memory and time, never full-frame.
 ***************************************************************************/
#include "rs_sar_polsar_decompose_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/sar/sar_complex.h"
#include "processing/algorithms/sar/sar_metadata.h"
#include "processing/algorithms/sar/sar_polsar.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h"

#include <QString>

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

const std::vector<std::string> s_decompositions = { "pauli", "h_alpha",
                                                    "freeman_durden", "yamaguchi" };

constexpr int kTileDim = 256;
constexpr int kMaxWindow = 101;
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

struct ProductBandSpec
{
    const char *bands;
    int count;
};

ProductBandSpec productBands( const std::string &decomposition )
{
    if ( decomposition == "pauli" )
        return { "odd_bounce;double_bounce;volume;span", 4 };
    if ( decomposition == "h_alpha" )
        return { "entropy;anisotropy;alpha_deg;dominance;lambda1;lambda2;lambda3", 7 };
    if ( decomposition == "freeman_durden" )
        return { "surface;double_bounce;volume;span", 4 };
    return { "surface;double_bounce;volume;helix;span", 5 }; // yamaguchi
}

struct ChannelMapping
{
    int hh = 0;
    int hv = 0; // cross-pol channel (HV preferred; VH accepted as reciprocal)
    int vv = 0;
    bool reciprocalAssumed = false;
    QString source; // "parameters" | "metadata"
};

/// Resolves the 3-channel mapping: explicit parameters win over the
/// declared SICNU_SAR_COMPLEX_CHANNELS metadata (DECISIONS D-002).
ChannelMapping resolveChannels( const GdalDatasetWrapper &ds, const Json::Value &params )
{
    const bool assumeReciprocity = getInt( params, "assumeReciprocity", 1 ) != 0;
    ChannelMapping m;

    const bool hasHh = params.isMember( "hhBand" );
    const bool hasVv = params.isMember( "vvBand" );
    const bool hasHv = params.isMember( "hvBand" );
    const bool hasVh = params.isMember( "vhBand" );
    if ( hasHh || hasVv || hasHv || hasVh )
    {
        if ( !hasHh || !hasVv )
            throw RSOperatorError( ErrorCode::InvalidParameter,
                                   "hhBand and vvBand must be given together (hvBand or "
                                   "vhBand completes the reciprocal triple)" );
        m.hh = getInt( params, "hhBand", 0 );
        m.vv = getInt( params, "vvBand", 0 );
        m.hv = hasHv ? getInt( params, "hvBand", 0 ) : ( hasVh ? getInt( params, "vhBand", 0 ) : 0 );
        if ( m.hv <= 0 )
            throw RSOperatorError( ErrorCode::InvalidParameter,
                                   "full-pol decomposition needs a cross-pol channel "
                                   "(hvBand or vhBand)" );
        m.reciprocalAssumed = !hasHv && hasVh; // VH standing in for HV
        m.source = QStringLiteral( "parameters" );
        return m;
    }

    const QString declared = sicnu::sar::datasetMeta( ds, sicnu::sar::kComplexChannelsKey );
    if ( declared.isEmpty() )
        throw RSOperatorError(
            ErrorCode::InvalidInputData,
            "POLARIZATION_MISMATCH: no complex channel declaration found — declare "
            "SICNU_SAR_COMPLEX_CHANNELS on the input (e.g. \"HH;HV;VV\") or pass explicit "
            "hhBand/hvBand/vvBand parameters" );

    std::vector<sicnu::sar::PolChannel> channels;
    QString error;
    if ( !sicnu::sar::parseChannelDeclaration( declared, &channels, &error ) )
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "invalid SICNU_SAR_COMPLEX_CHANNELS: " + error.toStdString() );

    std::map<sicnu::sar::PolChannel, int> bandOf;
    for ( size_t i = 0; i < channels.size(); ++i )
        bandOf.emplace( channels[i], static_cast<int>( i ) + 1 );

    const auto hhIt = bandOf.find( sicnu::sar::PolChannel::Hh );
    const auto vvIt = bandOf.find( sicnu::sar::PolChannel::Vv );
    if ( hhIt == bandOf.end() || vvIt == bandOf.end() )
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "POLARIZATION_MISMATCH: full-pol decomposition needs HH and VV "
                               "complex channels; declared: " + declared.toStdString() );
    m.hh = hhIt->second;
    m.vv = vvIt->second;
    const auto hvIt = bandOf.find( sicnu::sar::PolChannel::Hv );
    const auto vhIt = bandOf.find( sicnu::sar::PolChannel::Vh );
    if ( hvIt != bandOf.end() )
        m.hv = hvIt->second;
    else if ( vhIt != bandOf.end() )
    {
        m.hv = vhIt->second;
        m.reciprocalAssumed = true; // VH standing in for HV
    }
    else
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "POLARIZATION_MISMATCH: no cross-pol (HV/VH) complex channel "
                               "in the declaration: " + declared.toStdString() );
    if ( channels.size() == 4 && hvIt != bandOf.end() && vhIt != bandOf.end() )
    {
        if ( !assumeReciprocity )
            throw RSOperatorError(
                ErrorCode::InvalidInputData,
                "NON_RECIPROCAL_CHANNELS: the declaration carries both HV and VH; "
                "assumeReciprocity=0 refuses to collapse them (use reciprocal data or "
                "drop one channel explicitly)" );
        m.reciprocalAssumed = true; // HV ≠ VH information deliberately collapsed
    }
    m.source = QStringLiteral( "metadata" );
    return m;
}

} // anonymous namespace

Json::Value RsSarPolsarDecomposeOperator::schema() const {
    using namespace schema;
    Json::Value props( Json::objectValue );
    props["input"] = makeRasterParam( "input", "Full-pol complex (CFloat32) SAR raster" );
    props["output"] = makeOutputParam( "output", "Output raster path", "tif" );
    props["decomposition"] = makeEnumParam( "decomposition", "Polarimetric decomposition product",
                                            s_decompositions, "h_alpha" );
    props["windowSize"] = makeNumberParam( "windowSize",
                                           "Ensemble window size in pixels (odd; 1 = single "
                                           "look — H is then identically 0)",
                                           5.0 );
    props["hhBand"] = makeNumberParam( "hhBand", "Explicit 1-based HH complex band", 0.0 );
    props["hvBand"] = makeNumberParam( "hvBand", "Explicit 1-based HV complex band", 0.0 );
    props["vhBand"] = makeNumberParam( "vhBand", "Explicit 1-based VH complex band "
                                                   "(reciprocal stand-in for HV)", 0.0 );
    props["vvBand"] = makeNumberParam( "vvBand", "Explicit 1-based VV complex band", 0.0 );
    props["assumeReciprocity"] = makeNumberParam( "assumeReciprocity",
                                                  "1 (default) = collapse a 4-channel HV≠VH "
                                                  "declaration onto HV; 0 = refuse instead "
                                                  "(NON_RECIPROCAL_CHANNELS)",
                                                  1.0 );

    Json::Value outputs( Json::objectValue );
    outputs["output"] = makeRasterParam( "output", "Output raster path" );
    outputs["bands"] = makeStringParam( "bands", "Semicolon-separated product band order", "" );

    Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
    root["required"] = makeRequired( { "input", "output", "decomposition" } );
    return root;
}

Json::Value RsSarPolsarDecomposeOperator::metadata() const {
    Json::Value meta( Json::objectValue );
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append( "sar" );
    meta["tags"].append( "polsar" );
    meta["tags"].append( "polarimetry" );
    meta["task"] = "sar-polarimetry";
    meta["notes"] = "Model conventions (volume/helix matrices, branch rules) are defined in "
                    "sar_polsar.h and pinned by known-answer tests (test_sar_polsar.cpp). "
                    "The refined (2012) orientation-adapted Yamaguchi volume is NOT implemented.";
    meta["gpu"] = false;
    meta["purpose"] = "Physical scattering mechanism analysis (surface / double-bounce / "
                      "volume / helix) and Cloude-Pottier eigen statistics for full-pol data.";
    meta["prerequisites"].append( "Complex CFloat32 HH/HV/VV (reciprocal) channels from a "
                                  "full-pol SLC product; calibrated scattering amplitudes." );
    meta["limitations"].append( "Dual-pol detected inputs are refused — no quad-pol "
                                "approximation from dual-pol data." );
    meta["limitations"].append( "Single-look ensembles are rank 1: H is identically 0 and "
                                "anisotropy is NaN — use windowSize > 1 for H/A/alpha." );
    meta["limitations"].append( "Freeman-Durden/Yamaguchi powers may clamp negative "
                                "residuals to 0 (documented SPAN break near the noise floor)." );
    meta["limitations"].append( "Cost is O(windowSize^2) per pixel: windowSize=101 is a "
                                "deliberately expensive ensemble — use the smallest window "
                                "that decorrelates the speckle." );
    return meta;
}

Json::Value RsSarPolsarDecomposeOperator::executionEstimate() const {
    Json::Value est( Json::objectValue );
    est["tileWidth"] = kTileDim;
    est["tileHeight"] = kTileDim;
    est["estimatedRamBytes"] = Json::Value::UInt64(
        8ULL * ( kTileDim + kMaxWindow + 2 ) * ( kTileDim + kMaxWindow + 2 ) * 3 );
    return est;
}

Json::Value RsSarPolsarDecomposeOperator::estimateExecution( const Json::Value &params ) const {
    int window = getInt( params, "windowSize", 5 );
    if ( window < 1 )
        window = 5;
    const uint64_t bufDim = static_cast<uint64_t>( kTileDim + window + 1 );
    Json::Value est( Json::objectValue );
    est["tileWidth"] = Json::Value::UInt64( kTileDim );
    est["tileHeight"] = Json::Value::UInt64( kTileDim );
    est["estimatedRamBytes"] = Json::Value::UInt64( 8ULL * bufDim * bufDim * 3 );
    est["basis"] = "dynamic";
    return est;
}

Json::Value RsSarPolsarDecomposeOperator::run( const Json::Value &params,
                                               RSOperatorContext &context )
{
    if ( !params.isObject() )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "Operator parameters must be a JSON object" );

    const std::string inputPath = requireString( params, "input" );
    const std::string outputPath = requireString( params, "output" );
    const std::string decomposition = getEnum( params, "decomposition", s_decompositions, "h_alpha" );
    const int window = getInt( params, "windowSize", 5 );
    if ( window < 1 || window > kMaxWindow || window % 2 == 0 )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "windowSize must be an odd integer in [1, "
                                   + std::to_string( kMaxWindow ) + "]" );
    const int radius = window / 2;

    ensureGdalInit();

    GdalDatasetWrapper ds;
    if ( !ds.open( QString::fromStdString( inputPath ) ) )
        throw RSOperatorError( ErrorCode::GdalError, "Failed to open input raster: " + inputPath );
    if ( ds.width() <= 0 || ds.height() <= 0 )
        throw RSOperatorError( ErrorCode::InvalidInputData, "Input raster is empty: " + inputPath );

    const ChannelMapping mapping = resolveChannels( ds, params );

    const std::vector<int> bands = { mapping.hh, mapping.hv, mapping.vv };
    QString bandError;
    if ( !sicnu::sar::validateComplexBands( ds, bands, &bandError ) )
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "COMPLEX_BANDS_REQUIRED: " + bandError.toStdString() );

    const ProductBandSpec spec = productBands( decomposition );
    const int halo = radius;

    // Per-channel declared sentinels (R4 NoData audit): complex covariance
    // windows must not ingest declared NoData as signal. A sample is
    // skipped when any of its three channels matches that channel's declared
    // finite sentinel on either component (fail-closed); non-finite samples
    // stay excluded inside accumulatePolSample.
    std::array<bool, 3> channelHasSentinel {};
    std::array<float, 3> channelSentinel {};
    for ( int i = 0; i < 3; ++i )
    {
        bool has = false;
        const double nd = ds.bandNoDataValue( bands[static_cast<size_t>( i )], &has );
        if ( has && std::isfinite( nd ) )
        {
            channelHasSentinel[static_cast<size_t>( i )] = true;
            channelSentinel[static_cast<size_t>( i )] = static_cast<float>( nd );
        }
    }

    sicnu::sar::ComplexBandTileStream stream( ds, bands, kTileDim, kTileDim, halo );
    GdalStreamingOutput out( QString::fromStdString( outputPath ), ds.width(), ds.height(),
                             spec.count, GDT_Float32, ds.geoTransform(), ds.projection() );
    if ( !out.isOpen() )
        throw RSOperatorError( ErrorCode::FileNotWritable,
                               "Failed to create output raster: " + outputPath );
    // Windows that cannot finalize any sample (e.g. fully NoData) write NaN;
    // declare it so the voids are readable NoData instead of bare NaN.
    for ( int b = 1; b <= spec.count; ++b )
        out.setBandNoDataValue( b, kNaN );

    out.setMetadataItem( sicnu::sar::kModalityKey, "sar" );
    out.setMetadataItem( "SICNU_SAR_POLARIZATION_MODE", "full_complex" );
    out.setMetadataItem( sicnu::sar::kComplexChannelsKey,
                         "HH;HV;VV" );
    out.setMetadataItem( "SICNU_SAR_POLSAR_DECOMPOSITION", decomposition.c_str() );
    out.setMetadataItem( "SICNU_SAR_POLSAR_BANDS", spec.bands );
    out.setMetadataItem( "SICNU_SAR_ENSEMBLE_WINDOW", std::to_string( window ).c_str() );
    // linear_power labels the POWER products; h_alpha's entropy/alpha bands
    // are dimensionless statistics and carry no domain label.
    if ( decomposition != "h_alpha" )
        out.setMetadataItem( sicnu::sar::kDomainKey, "linear_power" );

    std::vector<float> product( static_cast<size_t>( spec.count ) * kTileDim * kTileDim );

    long noValidSamplePixels = 0;
    long noDataSamples = 0;
    long tiles = 0;
    const long totalTiles = stream.tileCount();

    auto writeProducts = [&]( const sicnu::sar::ComplexTile &tile,
                              const float *values, int count ) {
        for ( int b = 0; b < count; ++b )
        {
            if ( !out.writeTile( b + 1, GdalBlockStream::Tile{ tile.xOffset, tile.yOffset,
                                                              tile.width, tile.height, 0,
                                                              tile.width, tile.height,
                                                              tile.index, tile.totalTiles,
                                                              0, 0 },
                                 values + static_cast<size_t>( b ) * tile.width * tile.height ) )
                return false;
        }
        return true;
    };

    const bool ok = stream.forEach( [&]( const sicnu::sar::ComplexTile &tile,
                                         const std::complex<float> *bip ) {
        context.throwIfCancelled();

        const int bw = tile.bufferWidth;
        for ( int y = 0; y < tile.height; ++y )
        {
            for ( int x = 0; x < tile.width; ++x )
            {
                sicnu::sar::PolEnsemble3 ens;
                const int x0 = x;
                const int y0 = y;
                for ( int wy = -radius; wy <= radius; ++wy )
                {
                    const int sy = y0 + wy + halo;
                    for ( int wx = -radius; wx <= radius; ++wx )
                    {
                        const int sx = x0 + wx + halo;
                        const size_t base = ( static_cast<size_t>( sy ) * bw + sx ) * 3;
                        bool sentinel = false;
                        for ( int ch = 0; ch < 3; ++ch )
                        {
                            if ( !channelHasSentinel[static_cast<size_t>( ch )] )
                                continue;
                            const std::complex<float> v = bip[base + static_cast<size_t>( ch )];
                            const float s = channelSentinel[static_cast<size_t>( ch )];
                            if ( v.real() == s || v.imag() == s )
                            {
                                sentinel = true;
                                break;
                            }
                        }
                        if ( sentinel )
                        {
                            ++noDataSamples;
                            continue;
                        }
                        sicnu::sar::accumulatePolSample(
                            ens, bip[base + 0], bip[base + 1], bip[base + 2] );
                    }
                }

                float *dst =
                    product.data() + static_cast<size_t>( y ) * tile.width + x;
                sicnu::sar::PolEnsemble3 cov;
                if ( !sicnu::sar::finalizeEnsemble( ens, &cov ) )
                {
                    ++noValidSamplePixels;
                    for ( int b = 0; b < spec.count; ++b )
                        dst[static_cast<size_t>( b ) * tile.width * tile.height] = kNaN;
                    continue;
                }

                if ( decomposition == "pauli" )
                {
                    // Pauli product from the ENSEMBLE means (window-averaged
                    // Pauli powers): E-powers of the mean covariance.
                    const double odd = ( cov.c11 + cov.c33 + 2.0 * cov.c13.real() ) / 2.0;
                    const double dbl = ( cov.c11 + cov.c33 - 2.0 * cov.c13.real() ) / 2.0;
                    const double vol = 2.0 * cov.c22;
                    dst[0] = static_cast<float>( odd );
                    dst[1 * tile.width * tile.height] = static_cast<float>( dbl );
                    dst[2 * tile.width * tile.height] = static_cast<float>( vol );
                    dst[3 * tile.width * tile.height] =
                        static_cast<float>( odd + dbl + vol );
                }
                else if ( decomposition == "h_alpha" )
                {
                    sicnu::sar::HAlphaResult r;
                    if ( !sicnu::sar::cloudePottier( cov, &r ) )
                    {
                        for ( int b = 0; b < spec.count; ++b )
                            dst[static_cast<size_t>( b ) * tile.width * tile.height] = kNaN;
                        continue;
                    }
                    const double values[7] = { r.entropy, r.anisotropy, r.alphaMean,
                                               r.dominance, r.lambda[0], r.lambda[1],
                                               r.lambda[2] };
                    for ( int b = 0; b < 7; ++b )
                        dst[static_cast<size_t>( b ) * tile.width * tile.height] =
                            static_cast<float>( values[b] );
                }
                else if ( decomposition == "freeman_durden" )
                {
                    sicnu::sar::FreemanDurdenResult r;
                    if ( !sicnu::sar::freemanDurden( cov, &r ) )
                    {
                        for ( int b = 0; b < spec.count; ++b )
                            dst[static_cast<size_t>( b ) * tile.width * tile.height] = kNaN;
                        continue;
                    }
                    dst[0] = static_cast<float>( r.surface );
                    dst[1 * tile.width * tile.height] = static_cast<float>( r.doubleBounce );
                    dst[2 * tile.width * tile.height] = static_cast<float>( r.volume );
                    dst[3 * tile.width * tile.height] = static_cast<float>(
                        r.surface + r.doubleBounce + r.volume );
                }
                else // yamaguchi
                {
                    sicnu::sar::YamaguchiResult r;
                    if ( !sicnu::sar::yamaguchi4( cov, &r ) )
                    {
                        for ( int b = 0; b < spec.count; ++b )
                            dst[static_cast<size_t>( b ) * tile.width * tile.height] = kNaN;
                        continue;
                    }
                    dst[0] = static_cast<float>( r.surface );
                    dst[1 * tile.width * tile.height] = static_cast<float>( r.doubleBounce );
                    dst[2 * tile.width * tile.height] = static_cast<float>( r.volume );
                    dst[3 * tile.width * tile.height] = static_cast<float>( r.helix );
                    dst[4 * tile.width * tile.height] = static_cast<float>(
                        r.surface + r.doubleBounce + r.volume + r.helix );
                }
            }
        }

        ++tiles;
        context.reportProgress( 0.95 * tiles / totalTiles, "PolSAR decomposition" );
        return writeProducts( tile, product.data(), spec.count );
    } );

    if ( !ok || !out.closeWithError() )
    {
        out.abandon();
        throw RSOperatorError( ErrorCode::GdalError,
                               "Failed to stream/compute the PolSAR decomposition" );
    }

    Json::Value result;
    result["output"] = outputPath;
    result["decomposition"] = decomposition;
    result["bands"] = spec.bands;
    result["windowSize"] = window;
    result["channels"] = "HH;HV;VV";
    result["reciprocityAssumed"] = mapping.reciprocalAssumed;
    result["channelSource"] = mapping.source.toStdString();
    result["noValidSamplePixels"] = Json::Value::Int64( noValidSamplePixels );
    result["noDataSamples"] = Json::Value::Int64( noDataSamples );
    context.reportProgress( 1.0, "PolSAR decomposition complete" );
    return result;
}

} // namespace sicnu::operators::rs
