/***************************************************************************
 * rs_sar_coregister_local_operator.cpp — see the header
 * (Advanced InSAR 11.0, package C; DECISIONS D-003)
 ***************************************************************************/
#include "rs_sar_coregister_local_operator.h"

#include "data/raster_grid_compat.h"
#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/sar/sar_complex.h"
#include "processing/algorithms/sar/sar_coregistration.h"
#include "processing/algorithms/sar/sar_metadata.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_grid_compat.h"
#include "processing/gdal/gdal_multiband_block_stream.h"

#include <QCoreApplication>
#include <QString>

#include <gdal.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;

namespace {

constexpr uint64_t kPlaneBudgetBytes = 2ULL * 1024ULL * 1024ULL * 1024ULL; // 2 GiB

void requireComplexPair( GdalDatasetWrapper &master, int masterBand,
                         GdalDatasetWrapper &slave, int slaveBand )
{
    QString error;
    if ( !sicnu::sar::validateComplexBands( master, { masterBand }, &error ) )
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "COMPLEX_BANDS_REQUIRED: master — " + error.toStdString() );
    if ( !sicnu::sar::validateComplexBands( slave, { slaveBand }, &error ) )
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "COMPLEX_BANDS_REQUIRED: slave — " + error.toStdString() );
}

} // anonymous namespace

Json::Value RsSarCoregisterLocalOperator::schema() const {
    using namespace schema;
    Json::Value props( Json::objectValue );
    props["master"] = makeRasterParam( "master", "Master complex (CFloat32) SLC raster" );
    props["slave"] = makeRasterParam( "slave", "Slave complex (CFloat32) SLC raster "
                                               "(same grid as the master)" );
    props["masterBand"] = makeNumberParam( "masterBand", "1-based master band", 1.0 );
    props["slaveBand"] = makeNumberParam( "slaveBand", "1-based slave band", 1.0 );
    props["output"] = makeOutputParam( "output", "Aligned slave raster path (CFloat32)",
                                       "tif" );
    props["offsetFieldOutput"] =
        makeOutputParam( "offsetFieldOutput",
                         "Optional 3-band Float32 offset field (dx, dy pixels; peakRatio "
                         "confidence), lattice-node resolution",
                         "tif" );
    props["searchRadius"] = makeNumberParam( "searchRadius",
                                             "Per-patch NCC search radius (pixels)", 8.0 );
    props["patchSize"] = makeNumberParam( "patchSize", "Patch size (pixels)", 64.0 );
    props["patchStride"] = makeNumberParam( "patchStride", "Lattice stride (pixels)", 32.0 );
    props["minPeakRatio"] = makeNumberParam(
        "minPeakRatio", "Best/second-best NCC confidence floor per patch", 1.2 );
    props["medianRadius"] = makeNumberParam(
        "medianRadius",
        "Lattice median-filter radius (0 disables; disable for strong "
        "deformation gradients)", 1.0 );

    Json::Value outputs( Json::objectValue );
    outputs["output"] = makeRasterParam( "output", "Aligned slave raster" );
    outputs["offsetFieldOutput"] =
        makeRasterParam( "offsetFieldOutput", "Offset field (dx, dy, confidence)" );

    Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
    root["required"] = makeRequired( { "master", "slave", "output" } );
    return root;
}

Json::Value RsSarCoregisterLocalOperator::metadata() const {
    Json::Value meta( Json::objectValue );
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append( "sar" );
    meta["tags"].append( "insar" );
    meta["task"] = "sar-insar";
    meta["gpu"] = false;
    meta["purpose"] = "Bring an SLC pair onto a common sampling grid so the "
                      "interferometric phase measures geometry, not misregistration.";
    meta["prerequisites"].append( "Same-grid complex SLC pair (rs:sar_coregister "
                                  "preflight semantics apply)." );
    meta["limitations"].append( "Translation-field model: no affine/polynomial warp and "
                                "no DEM-based refinement; strong range ramps need a "
                                "lattice finer than the ramp scale." );
    meta["limitations"].append( "Both planes are materialized behind a 2 GiB gate "
                                "(MEMORY_BUDGET_EXCEEDED beyond — use a smaller AOI)." );
    return meta;
}

Json::Value RsSarCoregisterLocalOperator::executionEstimate() const {
    Json::Value est( Json::objectValue );
    est["tileWidth"] = 0;
    est["tileHeight"] = 0;
    est["estimatedRamBytes"] = Json::Value::UInt64( kPlaneBudgetBytes );
    return est;
}

Json::Value RsSarCoregisterLocalOperator::estimateExecution(
    const Json::Value &params ) const {
    (void)params;
    return executionEstimate();
}

Json::Value RsSarCoregisterLocalOperator::run( const Json::Value &params,
                                               RSOperatorContext &context )
{
    if ( !params.isObject() )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "Operator parameters must be a JSON object" );

    const std::string masterPath = requireString( params, "master" );
    const std::string slavePath = requireString( params, "slave" );
    const std::string outputPath = requireString( params, "output" );
    const std::string offsetPath = getString( params, "offsetFieldOutput", std::string() );
    const int masterBand = getInt( params, "masterBand", 1 );
    const int slaveBand = getInt( params, "slaveBand", 1 );
    const int searchRadius = getInt( params, "searchRadius", 8 );
    const int patchSize = getInt( params, "patchSize", 64 );
    const int patchStride = getInt( params, "patchStride", 32 );
    const double minPeakRatio = getDouble( params, "minPeakRatio", 1.2 );
    const int medianRadius = getInt( params, "medianRadius", 1 );

    if ( searchRadius < 1 || patchSize < 4 || patchStride < 1 || medianRadius < 0 )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "degenerate estimation parameters (searchRadius ≥ 1, "
                               "patchSize ≥ 4, patchStride ≥ 1, medianRadius ≥ 0)" );

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

    // Same-grid preflight (the shared grid-compat authority, rs:sar_interferogram
    // semantics).
    const sicnu::data::GridCompatReport gridReport = sicnu::data::compareGrids(
        sicnu::processing::gridFromDataset( master ),
        sicnu::processing::gridFromDataset( slave ) );
    if ( !gridReport.compatible() )
    {
        std::string detail;
        for ( const sicnu::data::GridCompatIssue &issue : gridReport.issues )
            detail += issue.message.toStdString() + "; ";
        throw RSOperatorError(
            ErrorCode::InvalidInputData,
            "GRID_MISMATCH: master and slave must share CRS, resolution, origin and "
            "extent (issues: " + detail
                + "remedy: coarser products or external alignment)" );
    }
    if ( master.width() != slave.width() || master.height() != slave.height() )
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "DIMENSION_MISMATCH: master and slave raster sizes differ" );
    requireComplexPair( master, masterBand, slave, slaveBand );

    const int width = master.width();
    const int height = master.height();
    const uint64_t wh = static_cast<uint64_t>( width ) * height;
    // Both magnitude planes enter the NCC and the warp materializes the
    // aligned slave: 2 × 16 + 8 B/px — the honest plane budget.
    const uint64_t planeBytes = 40ULL * wh;
    if ( planeBytes > kPlaneBudgetBytes )
        throw RSOperatorError(
            ErrorCode::InvalidInputData,
            "MEMORY_BUDGET_EXCEEDED: local co-registration materializes both planes "
            "(needs ~" + std::to_string( planeBytes ) + " bytes for "
                + std::to_string( width ) + "x" + std::to_string( height )
                + "). Use a smaller AOI." );

    // Read both planes (halo-less native complex reads).
    std::vector<std::complex<float>> masterPlane( static_cast<size_t>( wh ) );
    std::vector<std::complex<float>> slavePlane( static_cast<size_t>( wh ) );
    if ( !master.readBandWindowNative( masterBand, 0, 0, width, height,
                                       static_cast<void *>( masterPlane.data() ) ) )
        throw RSOperatorError( ErrorCode::GdalError, "Failed to read the master plane" );
    if ( !slave.readBandWindowNative( slaveBand, 0, 0, width, height,
                                      static_cast<void *>( slavePlane.data() ) ) )
        throw RSOperatorError( ErrorCode::GdalError, "Failed to read the slave plane" );

    sicnu::sar::OffsetField field;
    if ( !sicnu::sar::estimateOffsetField(
             masterPlane.data(), slavePlane.data(), width, height, searchRadius, patchSize,
             patchStride, minPeakRatio, medianRadius, &field,
             [ &context ] { context.throwIfCancelled(); } ) )
        throw RSOperatorError(
            ErrorCode::InvalidInputData,
            "COREGISTRATION_FAILED: fewer than 3 confident patches survived — the "
            "pair is too decorrelated or the search radius too small for the actual "
            "misregistration" );

    // Warped slave (single full-plane pass; the offset field is absolute-y).
    std::vector<std::complex<float>> warped( static_cast<size_t>( wh ) );
    sicnu::sar::warpComplexByOffsetField( slavePlane.data(), width, height, field,
                                          warped.data(),
                                          [ &context ] { context.throwIfCancelled(); } );

    GdalStreamingOutput out( QString::fromStdString( outputPath ), width, height, 1,
                             GDT_CFloat32, master.geoTransform(), master.projection() );
    if ( !out.isOpen() )
        throw RSOperatorError( ErrorCode::FileNotWritable,
                               "Failed to create output raster: " + outputPath );
    out.setMetadataItem( sicnu::sar::kModalityKey, "sar" );
    out.setMetadataItem( "SICNU_SAR_INSAR_PRODUCT", "coregistered_slave" );
    out.setBandNoDataValue( 1, std::numeric_limits<double>::quiet_NaN() );

    if ( !sicnu::sar::writeComplexTile( out, 1, 0, 0, width, height, warped.data() ) )
    {
        out.abandon();
        throw RSOperatorError( ErrorCode::GdalError,
                               "Failed to write the aligned slave raster" );
    }
    context.reportProgress( 0.95, "Warping slave" );

    // Optional offset-field product (3 bands: dx, dy, confidence at the
    // lattice-node resolution).
    if ( !offsetPath.empty() )
    {
        GdalStreamingOutput offsets( QString::fromStdString( offsetPath ), field.latticeCols,
                                     field.latticeRows, 3, GDT_Float32,
                                     master.geoTransform(), master.projection() );
        if ( !offsets.isOpen() )
        {
            out.abandon();
            throw RSOperatorError( ErrorCode::FileNotWritable,
                                   "Failed to create offset field raster: " + offsetPath );
        }
        offsets.setMetadataItem( sicnu::sar::kModalityKey, "sar" );
        offsets.setMetadataItem( "SICNU_SAR_INSAR_PRODUCT", "coregistration_offset_field" );
        std::vector<float> dxPlane( static_cast<size_t>( field.latticeCols )
                                    * field.latticeRows );
        std::vector<float> dyPlane = dxPlane;
        std::vector<float> confPlane = dxPlane;
        for ( size_t i = 0; i < field.patches.size(); ++i )
        {
            const sicnu::sar::OffsetPatch &node = field.patches[i];
            dxPlane[i] = static_cast<float>( node.dx );
            dyPlane[i] = static_cast<float>( node.dy );
            confPlane[i] = static_cast<float>( node.confident ? node.peakRatio : 0.0 );
        }
        const GdalBlockStream::Tile tile{ 0, 0, field.latticeCols, field.latticeRows, 0,
                                          field.latticeCols, field.latticeRows, 0, 1,
                                          field.latticeCols, field.latticeRows };
        const bool okWrite = offsets.writeTile( 1, tile, dxPlane.data() )
                             && offsets.writeTile( 2, tile, dyPlane.data() )
                             && offsets.writeTile( 3, tile, confPlane.data() )
                             && offsets.closeWithError();
        if ( !okWrite )
        {
            out.abandon();
            offsets.abandon();
            throw RSOperatorError( ErrorCode::GdalError,
                                   "Failed to write the offset field raster" );
        }
    }

    if ( !out.closeWithError() )
    {
        out.abandon();
        throw RSOperatorError( ErrorCode::GdalError,
                               "Failed to finalize the aligned slave raster" );
    }

    Json::Value json;
    json["output"] = outputPath;
    if ( !offsetPath.empty() )
        json["offsetFieldOutput"] = offsetPath;
    Json::Value global( Json::objectValue );
    global["dx"] = field.global.dx;
    global["dy"] = field.global.dy;
    global["confidentPatches"] = Json::Value::Int64( field.global.confidentPatches );
    json["globalShift"] = global;
    json["latticeCols"] = field.latticeCols;
    json["latticeRows"] = field.latticeRows;
    json["confidentPatches"] = Json::Value::Int64( field.confidentPatches );
    json["totalPatches"] =
        Json::Value::Int64( static_cast<long long>( field.latticeCols ) * field.latticeRows );
    json["medianAdjusted"] = Json::Value::Int64( field.medianAdjusted );
    context.reportProgress( 1.0, "Local co-registration complete" );
    return json;
}

} // namespace sicnu::operators::rs
