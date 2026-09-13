/***************************************************************************
 * rs_sar_coregister_operator.cpp — global complex shift estimation +
 * bilinear complex resample (Advanced SAR / PolSAR / InSAR 10.0, package C;
 * DECISIONS D-005).
 *
 * The NCC patch lattice and the resampler need the full master/slave
 * planes; both are materialized behind a fixed budget gate (typed refusal
 * beyond). The shift estimation and the resample are pure kernel calls
 * (sar_insar.h) — this file owns preflight, budget gating, and I/O only.
 ***************************************************************************/
#include "rs_sar_coregister_operator.h"

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

constexpr uint64_t kPlaneBudgetBytes = 4ULL * 1024ULL * 1024ULL * 1024ULL; // 4 GiB
constexpr int kMaxSearchRadius = 64;
constexpr int kMaxPatchSize = 128;

} // anonymous namespace

Json::Value RsSarCoregisterOperator::schema() const {
    using namespace schema;
    Json::Value props( Json::objectValue );
    props["master"] = makeRasterParam( "master", "Master (reference) complex SLC raster" );
    props["slave"] = makeRasterParam( "slave", "Slave (secondary) complex SLC raster" );
    props["output"] = makeOutputParam( "output", "Resampled slave raster path", "tif" );
    props["masterBand"] = makeNumberParam( "masterBand", "1-based master complex band", 1.0 );
    props["slaveBand"] = makeNumberParam( "slaveBand", "1-based slave complex band", 1.0 );
    props["searchRadius"] = makeNumberParam( "searchRadius",
                                             "Patch NCC search radius in pixels", 8.0 );
    props["patchSize"] = makeNumberParam( "patchSize", "NCC patch size in pixels", 16.0 );
    props["patchStride"] = makeNumberParam( "patchStride", "Patch lattice stride in pixels",
                                            8.0 );
    props["minPeakRatio"] = makeNumberParam( "minPeakRatio",
                                             "Best/second-best NCC ratio for a confident "
                                             "patch (0–1]",
                                             0.9 );
    props["reportOnly"] = makeNumberParam( "reportOnly",
                                           "1 = estimate and report the shift without "
                                           "writing the resampled slave",
                                           0.0 );

    Json::Value outputs( Json::objectValue );
    outputs["output"] = makeRasterParam( "output", "Resampled slave raster path" );
    outputs["dx"] = makeStringParam( "dx", "Estimated slave shift (x pixels)", "" );
    outputs["dy"] = makeStringParam( "dy", "Estimated slave shift (y pixels)", "" );

    Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
    root["required"] = makeRequired( { "master", "slave" } );
    return root;
}

Json::Value RsSarCoregisterOperator::metadata() const {
    Json::Value meta( Json::objectValue );
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append( "sar" );
    meta["tags"].append( "insar" );
    meta["tags"].append( "coregistration" );
    meta["task"] = "sar-insar";
    meta["gpu"] = false;
    meta["purpose"] = "Refine the residual global misalignment of a same-grid SLC pair "
                      "before interferometry.";
    meta["prerequisites"].append( "Two complex (CFloat32) SLC rasters on the same grid; "
                                  "granular misalignment within the search radius." );
    meta["limitations"].append( "Global translation model only — no affine/polynomial warp, "
                                "no DEM-based or range-Doppler coregistration." );
    meta["limitations"].append( "Full planes are materialized behind a 4 GiB budget "
                                "(MEMORY_BUDGET_EXCEEDED beyond)." );
    return meta;
}

Json::Value RsSarCoregisterOperator::executionEstimate() const {
    Json::Value est( Json::objectValue );
    est["tileWidth"] = 0;
    est["tileHeight"] = 0;
    est["estimatedRamBytes"] = Json::Value::UInt64( kPlaneBudgetBytes );
    return est;
}

Json::Value RsSarCoregisterOperator::estimateExecution( const Json::Value &params ) const {
    (void)params;
    return executionEstimate();
}

Json::Value RsSarCoregisterOperator::run( const Json::Value &params, RSOperatorContext &context )
{
    if ( !params.isObject() )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "Operator parameters must be a JSON object" );

    const std::string masterPath = requireString( params, "master" );
    const std::string slavePath = requireString( params, "slave" );
    const std::string outputPath = getString( params, "output", std::string() );
    const int masterBand = getInt( params, "masterBand", 1 );
    const int slaveBand = getInt( params, "slaveBand", 1 );
    const int searchRadius = getInt( params, "searchRadius", 8 );
    const int patchSize = getInt( params, "patchSize", 16 );
    const int patchStride = getInt( params, "patchStride", 8 );
    const double minPeakRatio = getDouble( params, "minPeakRatio", 0.9 );
    const bool reportOnly = getInt( params, "reportOnly", 0 ) != 0;

    if ( searchRadius < 1 || searchRadius > kMaxSearchRadius )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "searchRadius must be in [1, "
                                   + std::to_string( kMaxSearchRadius ) + "]" );
    if ( patchSize < 4 || patchSize > kMaxPatchSize )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "patchSize must be in [4, " + std::to_string( kMaxPatchSize )
                                   + "]" );
    if ( patchStride < 1 || patchStride > patchSize )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "patchStride must be in [1, patchSize]" );
    if ( !( minPeakRatio > 0.0 && minPeakRatio <= 1.0 ) )
        throw RSOperatorError( ErrorCode::InvalidParameter, "minPeakRatio must be in (0, 1]" );
    if ( !reportOnly && outputPath.empty() )
        throw RSOperatorError( ErrorCode::MissingRequiredParameter,
                               "output is required unless reportOnly=1" );

    ensureGdalInit();

    GdalDatasetWrapper master;
    if ( !master.open( QString::fromStdString( masterPath ) ) )
        throw RSOperatorError( ErrorCode::GdalError,
                               "Failed to open master SLC: " + masterPath );
    GdalDatasetWrapper slave;
    if ( !slave.open( QString::fromStdString( slavePath ) ) )
        throw RSOperatorError( ErrorCode::GdalError,
                               "Failed to open slave SLC: " + slavePath );

    const sicnu::data::GridCompatReport gridReport = sicnu::data::compareGrids(
        sicnu::processing::gridFromDataset( master ), sicnu::processing::gridFromDataset( slave ) );
    if ( !gridReport.compatible() )
    {
        std::string detail;
        for ( const sicnu::data::GridCompatIssue &issue : gridReport.issues )
            detail += issue.message.toStdString() + "; ";
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "GRID_MISMATCH: coregistration refines residual shifts of a "
                               "same-grid pair (issues: " + detail + ")" );
    }
    const int width = master.width();
    const int height = master.height();
    if ( width <= 0 || height <= 0 )
        throw RSOperatorError( ErrorCode::InvalidInputData, "Master SLC is empty" );
    if ( slave.width() != width || slave.height() != height )
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "DIMENSION_MISMATCH: master and slave raster sizes differ" );

    QString error;
    if ( !sicnu::sar::validateComplexBands( master, { masterBand }, &error )
         || !sicnu::sar::validateComplexBands( slave, { slaveBand }, &error ) )
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "COMPLEX_BANDS_REQUIRED: " + error.toStdString() );

    const uint64_t planeBytes = 8ULL * static_cast<uint64_t>( width ) * height * 3;
    if ( planeBytes > kPlaneBudgetBytes )
        throw RSOperatorError(
            ErrorCode::InvalidInputData,
            "MEMORY_BUDGET_EXCEEDED: coregistration materializes master + slave + "
            "resampled planes (needs ~" + std::to_string( planeBytes )
                + " bytes). Use a smaller AOI." );

    // Load planes (halo-less tile walk).
    auto loadPlane = [&]( GdalDatasetWrapper &ds, int band ) {
        std::vector<std::complex<float>> plane( static_cast<size_t>( width ) * height );
        sicnu::sar::ComplexBandTileStream stream( ds, { band }, 256, 256, 0 );
        std::vector<std::complex<float>> buf( 256 * 256 );
        for ( int i = 0; i < stream.tileCount(); ++i )
        {
            context.throwIfCancelled();
            const sicnu::sar::ComplexTile &tile = stream.tile( i );
            if ( !stream.readTile( i, buf.data() ) )
                throw RSOperatorError( ErrorCode::GdalError,
                                       "Failed to read SLC tile while loading the plane" );
            for ( int y = 0; y < tile.height; ++y )
                for ( int x = 0; x < tile.width; ++x )
                    plane[static_cast<size_t>( tile.yOffset + y ) * width + tile.xOffset + x] =
                        buf[static_cast<size_t>( y ) * tile.bufferWidth + x];
        }
        return plane;
    };

    context.reportProgress( 0.1, "Loading master plane" );
    std::vector<std::complex<float>> masterPlane = loadPlane( master, masterBand );
    context.reportProgress( 0.3, "Loading slave plane" );
    std::vector<std::complex<float>> slavePlane = loadPlane( slave, slaveBand );

    context.reportProgress( 0.5, "Estimating the global shift" );
    sicnu::sar::CoregisterShift shift;
    if ( !sicnu::sar::coregistrationShift( masterPlane.data(), slavePlane.data(), width, height,
                                           searchRadius, patchSize, patchStride, minPeakRatio,
                                           &shift ) )
        throw RSOperatorError(
            ErrorCode::ComputationError,
            "COREGISTRATION_FAILED: fewer than 3 confident patch peaks — the scenes may "
            "be decorrelated or the shift exceeds the search radius" );

    Json::Value result;
    result["dx"] = shift.dx;
    result["dy"] = shift.dy;
    result["confidentPatches"] = Json::Value::Int64( shift.confidentPatches );
    result["meanPeakRatio"] = shift.meanPeakRatio;
    result["model"] = "global_translation";

    if ( reportOnly )
    {
        context.reportProgress( 1.0, "Coregistration report complete" );
        return result;
    }

    context.reportProgress( 0.8, "Resampling the slave" );
    std::vector<std::complex<float>> resampled( static_cast<size_t>( width ) * height );
    // The NCC peak (dx, dy) is the displacement OF the slave content
    // relative to the master (slave(x, y) ≈ master(x − dx, y − dy));
    // aligning therefore shifts the slave content by (−dx, −dy).
    sicnu::sar::shiftComplexBilinear( slavePlane.data(), width, height, -shift.dx, -shift.dy,
                                      resampled.data() );

    GdalStreamingOutput out( QString::fromStdString( outputPath ), width, height, 1,
                             GDT_CFloat32, master.geoTransform(), master.projection() );
    if ( !out.isOpen() )
        throw RSOperatorError( ErrorCode::FileNotWritable,
                               "Failed to create output raster: " + outputPath );
    out.setMetadataItem( sicnu::sar::kModalityKey, "sar" );
    out.setMetadataItem( "SICNU_SAR_INSAR_PRODUCT", "coregistered_slave" );
    out.setMetadataItem( "SICNU_SAR_INSAR_COREG_DX", std::to_string( shift.dx ).c_str() );
    out.setMetadataItem( "SICNU_SAR_INSAR_COREG_DY", std::to_string( shift.dy ).c_str() );

    std::vector<std::complex<float>> tile( static_cast<size_t>( 256 ) * 256 );
    for ( int ty = 0; ty < height; ty += 256 )
    {
        for ( int tx = 0; tx < width; tx += 256 )
        {
            context.throwIfCancelled();
            const int tw = std::min( 256, width - tx );
            const int th = std::min( 256, height - ty );
            for ( int y = 0; y < th; ++y )
                for ( int x = 0; x < tw; ++x )
                    tile[static_cast<size_t>( y ) * tw + x] =
                        resampled[static_cast<size_t>( ty + y ) * width + tx + x];
            if ( !sicnu::sar::writeComplexTile( out, 1, tx, ty, tw, th, tile.data() ) )
            {
                out.abandon();
                throw RSOperatorError( ErrorCode::GdalError,
                                       "Failed to write the resampled slave" );
            }
        }
    }
    if ( !out.closeWithError() )
    {
        out.abandon();
        throw RSOperatorError( ErrorCode::GdalError, "Failed to finalize the resampled slave" );
    }

    result["output"] = outputPath;
    context.reportProgress( 1.0, "Coregistration complete" );
    return result;
}

} // namespace sicnu::operators::rs
