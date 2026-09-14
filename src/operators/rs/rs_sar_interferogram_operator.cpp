/***************************************************************************
 * rs_sar_interferogram_operator.cpp — interferogram + coherence
 * (Advanced SAR / PolSAR / InSAR 10.0, package C).
 *
 * Streams master and slave through twin complex tile iterators with
 * identical tile geometry (same-grid contract), forms s1·conj(s2) and the
 * window coherence tile-by-tile, optionally removes a robust flat-earth
 * ramp (streaming pre-pass), and writes bounded tiles only.
 ***************************************************************************/
#include "rs_sar_interferogram_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "data/raster_grid_compat.h"
#include "processing/algorithms/sar/sar_complex.h"
#include "processing/algorithms/sar/sar_insar.h"
#include "processing/algorithms/sar/sar_metadata.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_grid_compat.h"
#include "processing/gdal/gdal_multiband_block_stream.h"

#include <QString>

#include <gdal.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

const std::vector<std::string> s_rampModes = { "none", "linear", "quadratic" };

constexpr int kTileDim = 256;
constexpr int kMaxWindow = 33;
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

void requireComplexPair( const GdalDatasetWrapper &master, int masterBand,
                         const GdalDatasetWrapper &slave, int slaveBand )
{
    QString error;
    if ( !sicnu::sar::validateComplexBands( master, { masterBand }, &error )
         || !sicnu::sar::validateComplexBands( slave, { slaveBand }, &error ) )
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "COMPLEX_BANDS_REQUIRED: " + error.toStdString() );
}

} // anonymous namespace

Json::Value RsSarInterferogramOperator::schema() const {
    using namespace schema;
    Json::Value props( Json::objectValue );
    props["master"] = makeRasterParam( "master", "Master (reference) complex SLC raster" );
    props["slave"] = makeRasterParam( "slave", "Slave (secondary) complex SLC raster" );
    props["output"] = makeOutputParam( "output", "Output complex interferogram path", "tif" );
    props["coherenceOutput"] = makeOutputParam( "coherenceOutput",
                                                "Optional output coherence raster path", "tif" );
    props["masterBand"] = makeNumberParam( "masterBand", "1-based master complex band", 1.0 );
    props["slaveBand"] = makeNumberParam( "slaveBand", "1-based slave complex band", 1.0 );
    props["coherenceWindow"] = makeNumberParam( "coherenceWindow",
                                                "Coherence window size in pixels (odd)", 5.0 );
    props["flattenRamp"] = makeEnumParam( "flattenRamp",
                                          "Robust flat-earth ramp removal (low-order "
                                          "approximation, not topographic phase removal)",
                                          s_rampModes, "none" );

    Json::Value outputs( Json::objectValue );
    outputs["output"] = makeRasterParam( "output", "Output complex interferogram path" );
    outputs["coherence"] = makeStringParam( "coherence", "Coherence summary (mean of valid)", "" );

    Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
    root["required"] = makeRequired( { "master", "slave", "output" } );
    return root;
}

Json::Value RsSarInterferogramOperator::metadata() const {
    Json::Value meta( Json::objectValue );
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append( "sar" );
    meta["tags"].append( "insar" );
    meta["tags"].append( "interferometry" );
    meta["task"] = "sar-insar";
    meta["notes"] = "Base chain only: interferogram + coherence. No topographic phase "
                    "removal, no atmospheric correction, no PSI/SBAS — requests beyond the "
                    "base chain must not be claimed (sar_insar.h honesty scope).";
    meta["gpu"] = false;
    meta["purpose"] = "Form the interferometric phase and coherence quality layer of a "
                      "co-registered SLC pair.";
    meta["prerequisites"].append( "Co-registered complex (CFloat32) SLC pair on the SAME grid "
                                  "(CRS, resolution, origin, extent); use rs:sar_coregister "
                                  "first when the scenes are misaligned." );
    meta["limitations"].append( "Same-grid contract — no hidden resampling." );
    meta["limitations"].append( "flattenRamp is a low-order polynomial approximation of the "
                                "flat-earth phase, not DEM/orbit-based topographic removal." );
    return meta;
}

Json::Value RsSarInterferogramOperator::executionEstimate() const {
    Json::Value est( Json::objectValue );
    est["tileWidth"] = kTileDim;
    est["tileHeight"] = kTileDim;
    est["estimatedRamBytes"] = Json::Value::UInt64(
        16ULL * ( kTileDim + kMaxWindow ) * ( kTileDim + kMaxWindow ) );
    return est;
}

Json::Value RsSarInterferogramOperator::estimateExecution( const Json::Value &params ) const {
    int window = getInt( params, "coherenceWindow", 5 );
    if ( window < 1 )
        window = 5;
    const uint64_t bufDim = static_cast<uint64_t>( kTileDim + window + 1 );
    Json::Value est( Json::objectValue );
    est["tileWidth"] = Json::Value::UInt64( kTileDim );
    est["tileHeight"] = Json::Value::UInt64( kTileDim );
    est["estimatedRamBytes"] = Json::Value::UInt64( 16ULL * bufDim * bufDim );
    est["basis"] = "dynamic";
    return est;
}

Json::Value RsSarInterferogramOperator::run( const Json::Value &params,
                                             RSOperatorContext &context )
{
    if ( !params.isObject() )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "Operator parameters must be a JSON object" );

    const std::string masterPath = requireString( params, "master" );
    const std::string slavePath = requireString( params, "slave" );
    const std::string outputPath = requireString( params, "output" );
    const std::string coherencePath = getString( params, "coherenceOutput", std::string() );
    const int masterBand = getInt( params, "masterBand", 1 );
    const int slaveBand = getInt( params, "slaveBand", 1 );
    const int window = getInt( params, "coherenceWindow", 5 );
    if ( window < 1 || window > kMaxWindow || window % 2 == 0 )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "coherenceWindow must be an odd integer in [1, "
                                   + std::to_string( kMaxWindow ) + "]" );
    const int radius = window / 2;
    const std::string rampMode = getEnum( params, "flattenRamp", s_rampModes, "none" );
    const bool wantCoherence = !coherencePath.empty();

    ensureGdalInit();

    GdalDatasetWrapper master;
    if ( !master.open( QString::fromStdString( masterPath ) ) )
        throw RSOperatorError( ErrorCode::GdalError,
                               "Failed to open master SLC: " + masterPath );
    GdalDatasetWrapper slave;
    if ( !slave.open( QString::fromStdString( slavePath ) ) )
        throw RSOperatorError( ErrorCode::GdalError,
                               "Failed to open slave SLC: " + slavePath );

    if ( master.width() <= 0 || master.height() <= 0 )
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "Master SLC is empty: " + masterPath );

    // Same-grid preflight (#929 shared authority) + explicit dimension check.
    const sicnu::data::GridCompatReport gridReport = sicnu::data::compareGrids(
        sicnu::processing::gridFromDataset( master ), sicnu::processing::gridFromDataset( slave ) );
    if ( !gridReport.compatible() )
    {
        std::string detail;
        for ( const sicnu::data::GridCompatIssue &issue : gridReport.issues )
            detail += issue.message.toStdString() + "; ";
        throw RSOperatorError(
            ErrorCode::InvalidInputData,
            "GRID_MISMATCH: master and slave must share CRS, resolution, origin and "
            "extent (issues: " + detail + "remedy: rs:sar_coregister or external alignment)" );
    }
    if ( master.width() != slave.width() || master.height() != slave.height() )
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "DIMENSION_MISMATCH: master and slave raster sizes differ" );

    requireComplexPair( master, masterBand, slave, slaveBand );

    const int halo = wantCoherence ? radius : 0;
    sicnu::sar::ComplexBandTileStream masterStream( master, { masterBand }, kTileDim, kTileDim,
                                                    halo );
    sicnu::sar::ComplexBandTileStream slaveStream( slave, { slaveBand }, kTileDim, kTileDim,
                                                   halo );

    // Optional ramp pre-pass: streaming robust fit of the WRAPPED phase
    // (O(1) working memory; the same twin indexed walk as the main pass).
    sicnu::sar::PhaseRampModel ramp;
    bool haveRamp = false;
    if ( rampMode != "none" )
    {
        sicnu::sar::PhaseRampFitter fitter;
        const size_t tiles = masterStream.tileCount();
        std::vector<std::complex<float>> mbuf( masterStream.bandCount()
                                               * static_cast<size_t>( kTileDim + 2 * halo + 2 )
                                               * ( kTileDim + 2 * halo + 2 ) );
        std::vector<std::complex<float>> sbuf = mbuf;
        for ( size_t i = 0; i < tiles; ++i )
        {
            context.throwIfCancelled();
            const sicnu::sar::ComplexTile &tile = masterStream.tile( static_cast<int>( i ) );
            if ( !masterStream.readTile( static_cast<int>( i ), mbuf.data() )
                 || !slaveStream.readTile( static_cast<int>( i ), sbuf.data() ) )
                throw RSOperatorError( ErrorCode::GdalError,
                                       "Failed to read SLC pair tiles for the ramp fit" );
            for ( int y = 0; y < tile.height; ++y )
                for ( int x = 0; x < tile.width; ++x )
                {
                    // Core pixels sit at (+halo, +halo) in the halo buffer
                    // (ComplexBandTileStream BIP layout).
                    const size_t idx = static_cast<size_t>( y + halo ) * tile.bufferWidth
                                       + x + halo;
                    fitter.addSample( tile.xOffset + x, tile.yOffset + y,
                                      sicnu::sar::interferogramPhase( mbuf[idx], sbuf[idx] ) );
                }
        }
        if ( !fitter.fit( rampMode == "quadratic", 2, &ramp ) )
            throw RSOperatorError( ErrorCode::ComputationError,
                                   "flattenRamp fitting failed: too few valid sample pairs" );
        haveRamp = true;
    }

    GdalStreamingOutput out( QString::fromStdString( outputPath ), master.width(),
                             master.height(), 1, GDT_CFloat32, master.geoTransform(),
                             master.projection() );
    if ( !out.isOpen() )
        throw RSOperatorError( ErrorCode::FileNotWritable,
                               "Failed to create output raster: " + outputPath );
    out.setMetadataItem( sicnu::sar::kModalityKey, "sar" );
    out.setMetadataItem( "SICNU_SAR_INSAR_PRODUCT", "interferogram" );
    out.setMetadataItem( "SICNU_SAR_INSAR_MASTER", masterPath.c_str() );
    out.setMetadataItem( "SICNU_SAR_INSAR_SLAVE", slavePath.c_str() );
    out.setMetadataItem( "SICNU_SAR_INSAR_RAMP", rampMode.c_str() );

    GdalStreamingOutput coherenceOut(
        QString::fromStdString( coherencePath ), master.width(), master.height(), 1,
        GDT_Float32, master.geoTransform(), master.projection() );
    if ( wantCoherence )
    {
        if ( !coherenceOut.isOpen() )
        {
            out.abandon();
            throw RSOperatorError( ErrorCode::FileNotWritable,
                                   "Failed to create coherence raster: " + coherencePath );
        }
        coherenceOut.setMetadataItem( sicnu::sar::kModalityKey, "sar" );
        coherenceOut.setMetadataItem( "SICNU_SAR_INSAR_PRODUCT", "coherence" );
        coherenceOut.setBandNoDataValue( 1, kNaN );
    }

    std::vector<std::complex<float>> mbuf;
    std::vector<std::complex<float>> sbuf;
    std::vector<std::complex<float>> flatM; // ramp-flattened copies of the
    std::vector<std::complex<float>> flatS; // halo buffers (review F6: the
    // coherence must see the flattened field, not the raw ramp)
    std::vector<std::complex<float>> ifgTile;
    std::vector<float> cohTile;
    const size_t maxTileSamples = static_cast<size_t>( kTileDim + 2 * halo )
                                  * ( kTileDim + 2 * halo );
    mbuf.resize( maxTileSamples );
    sbuf.resize( maxTileSamples );
    flatM.resize( maxTileSamples );
    flatS.resize( maxTileSamples );
    ifgTile.resize( static_cast<size_t>( kTileDim ) * kTileDim );
    cohTile.resize( static_cast<size_t>( kTileDim ) * kTileDim );

    long validPixels = 0;
    long validCoherencePixels = 0;
    double coherenceSum = 0.0;
    const long totalTiles = masterStream.tileCount();
    long tileIndex = 0;

    for ( ; tileIndex < totalTiles; ++tileIndex )
    {
        context.throwIfCancelled();
        const sicnu::sar::ComplexTile &tile = masterStream.tile( tileIndex );
        if ( !masterStream.readTile( tileIndex, mbuf.data() )
             || !slaveStream.readTile( tileIndex, sbuf.data() ) )
        {
            out.abandon();
            coherenceOut.abandon();
            throw RSOperatorError( ErrorCode::GdalError,
                                   "Failed to read SLC pair tiles (twin walk)" );
        }

        if ( haveRamp )
        {
            // Flatten the WHOLE halo buffer once; every downstream product
            // (interferogram AND coherence) then sees the same flattened
            // field.
            for ( int y = 0; y < tile.bufferHeight; ++y )
                for ( int x = 0; x < tile.bufferWidth; ++x )
                {
                    const size_t idx = static_cast<size_t>( y ) * tile.bufferWidth + x;
                    const std::complex<float> m( mbuf[idx] );
                    const std::complex<float> sv( sbuf[idx] );
                    // Flat-earth removal multiplies the MASTER by
                    // e^{−i·ramp}: the product flatM·conj(flatS) then keeps
                    // exactly |s1|·|s2| with phase (φ1−φ2−ramp), and the
                    // coherence window sees the flattened pair (review
                    // P1-1/P2-1 — an earlier draft rotated the pair phase
                    // into the master amplitude slot and double-counted).
                    const double corr =
                        -sicnu::sar::evalPhaseRamp( ramp, tile.xOffset - halo + x,
                                                    tile.yOffset - halo + y );
                    const double amp = sicnu::sar::complexAmplitude( m );
                    const double phase = std::arg( m );
                    if ( std::isfinite( phase ) && std::isfinite( amp )
                         && std::isfinite( corr ) )
                    {
                        flatM[idx] = std::complex<float>(
                            static_cast<float>( amp * std::cos( phase + corr ) ),
                            static_cast<float>( amp * std::sin( phase + corr ) ) );
                    }
                    else
                    {
                        flatM[idx] = std::complex<float>( kNaN, kNaN );
                    }
                    flatS[idx] = sv;
                }
        }
        else
        {
            std::copy( mbuf.begin(), mbuf.end(), flatM.begin() );
            std::copy( sbuf.begin(), sbuf.end(), flatS.begin() );
        }

        for ( int y = 0; y < tile.height; ++y )
        {
            for ( int x = 0; x < tile.width; ++x )
            {
                const size_t center =
                    static_cast<size_t>( y + halo ) * tile.bufferWidth + x + halo;
                const std::complex<float> m( flatM[center] );
                const std::complex<float> s( flatS[center] );

                const std::complex<float> ifg = static_cast<std::complex<float>>(
                    sicnu::sar::interferogramSample( m, s ) );
                ifgTile[static_cast<size_t>( y ) * tile.width + x] = ifg;
                if ( std::isfinite( ifg.real() ) )
                    ++validPixels;

                if ( wantCoherence )
                {
                    const double coherence = sicnu::sar::windowCoherence(
                        flatM.data(), flatS.data(), tile.bufferWidth, tile.bufferHeight,
                        x + halo, y + halo, radius );
                    cohTile[static_cast<size_t>( y ) * tile.width + x] =
                        static_cast<float>( coherence );
                    if ( std::isfinite( coherence ) )
                    {
                        ++validCoherencePixels;
                        coherenceSum += coherence;
                    }
                }
            }
        }

        if ( !out.writeTileRaw( 1,
                                GdalBlockStream::Tile{ tile.xOffset, tile.yOffset, tile.width,
                                                       tile.height, 0, tile.width,
                                                       tile.height, tile.index,
                                                       tile.totalTiles, 0, 0 },
                                ifgTile.data(), GDT_CFloat32 ) )
        {
            out.abandon();
            coherenceOut.abandon();
            throw RSOperatorError( ErrorCode::GdalError,
                                   "Failed to write interferogram tile" );
        }

        if ( wantCoherence )
        {
            if ( !coherenceOut.writeTile(
                     1,
                     GdalBlockStream::Tile{ tile.xOffset, tile.yOffset, tile.width, tile.height,
                                            0, tile.width, tile.height, tile.index,
                                            tile.totalTiles, 0, 0 },
                     cohTile.data() ) )
            {
                out.abandon();
                coherenceOut.abandon();
                throw RSOperatorError( ErrorCode::GdalError,
                                       "Failed to write coherence tile" );
            }
        }

        context.reportProgress( 0.95 * ( tileIndex + 1 ) / totalTiles,
                                "Interferogram" );
    }

    if ( !out.closeWithError() || ( wantCoherence && !coherenceOut.closeWithError() ) )
    {
        out.abandon();
        coherenceOut.abandon();
        throw RSOperatorError( ErrorCode::GdalError,
                               "Failed to finalize the interferogram outputs" );
    }

    Json::Value result;
    result["output"] = outputPath;
    result["ramp"] = rampMode;
    if ( haveRamp )
    {
        // D-006: the fit is reported so callers can audit the removal.
        Json::Value coefs( Json::arrayValue );
        const int nCoef = ramp.quadratic ? 6 : 3;
        for ( int c = 0; c < nCoef; ++c )
            coefs.append( ramp.coef[c] );
        result["rampCoefficients"] = coefs;
        result["rampCoefficientOrder"] =
            ramp.quadratic ? "1,x,y,x^2,xy,y^2" : "1,x,y";
    }
    result["validPixels"] = Json::Value::Int64( validPixels );
    if ( wantCoherence )
    {
        result["coherenceOutput"] = coherencePath;
        result["coherenceWindow"] = window;
        result["validCoherencePixels"] = Json::Value::Int64( validCoherencePixels );
        result["meanCoherence"] = validCoherencePixels > 0
                                      ? Json::Value( coherenceSum / validCoherencePixels )
                                      : Json::Value();
    }
    context.reportProgress( 1.0, "Interferogram complete" );
    return result;
}

} // namespace sicnu::operators::rs
