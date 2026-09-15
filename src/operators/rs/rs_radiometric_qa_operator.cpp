/***************************************************************************
 * rs_radiometric_qa_operator.cpp — see the header for the contract.
 * Streaming evaluation: read tile → per-band flags → union mask/QA bits →
 * write uint16 tile; summaries accumulated in double-counting size_t.
 ***************************************************************************/
#include "rs_radiometric_qa_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/radiometric_qa.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_grid_compat.h"
#include "processing/gdal/gdal_multiband_block_stream.h"
#include "data/raster_grid_compat.h"

#include <QStringList>

#include <gdal.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;
using RadiometricQa::FlagCloud;
using RadiometricQa::FlagCloudShadow;
using RadiometricQa::FlagSnow;

namespace {
constexpr int kTileDim = 256;

/// Flag-token → bit for the mask_flag parameter.
uint16_t maskFlagFromToken( const std::string &token )
{
    if ( token == "cloud" )
        return FlagCloud;
    if ( token == "cloud_shadow" )
        return FlagCloudShadow;
    if ( token == "snow" )
        return FlagSnow;
    return 0;
}

constexpr const char *kQaSchemaKey = "SICNU_QA_FLAG_SCHEMA";
constexpr const char *kQaSchemaId = "exp_rs_radiometric_qa_flags/1";
} // namespace

Json::Value RsRadiometricQaOperator::schema() const {
    using namespace schema;
    Json::Value props( Json::objectValue );
    props["input"] = makeRasterParam( "input", "Reflectance raster to evaluate (any band count; flags are per band)" );
    props["output"] = makeOutputParam( "output", "Output uint16 flag raster (one band per input band; 0 = clean)", "tif" );
    props["saturation_level"] = makeNumberParam( "saturation_level", "Values >= this are FlagSaturated; <= 0 disables threshold saturation", 0.0 );
    props["cloud_mask"] = makeRasterParam( "cloud_mask", "Optional binary mask raster (1 = obscured, QaMask convention) on the same grid", false );
    props["mask_flag"] = makeEnumParam( "mask_flag", "Flag raised where the cloud mask is set", { "cloud", "cloud_shadow", "snow" }, "cloud" );
    props["qa_radsat_band"] = makeIntegerParam( "qa_radsat_band", "Optional 1-based band index in 'input' holding QA_RADSAT-style uint16 words", 0 );
    props["qa_radsat_bits"] = makeStringParam( "qa_radsat_bits", "Comma-separated per-band saturation bit masks aligned to input bands, e.g. \"1,2,4,8\" (used with qa_radsat_band; 0 disables a band)", "" );

    Json::Value outputs( Json::objectValue );
    outputs["output"] = makeRasterParam( "output", "Output flag raster path", false );
    outputs["bandCount"] = makeIntegerParam( "bandCount", "Number of flag bands written", 0 );

    Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
    root["required"] = makeRequired( { "input", "output" } );
    return root;
}

Json::Value RsRadiometricQaOperator::metadata() const {
    Json::Value meta( Json::objectValue );
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append( "optical" );
    meta["tags"].append( "qa" );
    meta["tags"].append( "saturation" );
    meta["task"] = "radiometric-normalization";
    meta["notes"] = "Flag vocabulary (uint16): bit0 saturation, bit1 negative, bit2 over-range, "
                    "bit3 non-finite, bit4 cloud, bit5 cloud-shadow, bit6 snow, bit7 "
                    "QA_RADSAT saturation, bit8 invalid angles. Schema id "
                    "exp_rs_radiometric_qa_flags/1 is stamped on the output.";
    meta["gpu"] = false;
    meta["purpose"] = "Trace which pixels calibration/atmospheric steps rendered unphysical or "
                      "obscured, so downstream statistics can exclude them by evidence.";
    meta["prerequisites"].append( "The cloud mask must share the input grid (CRS, geotransform, "
                                  "size); mismatched grids are refused, never resampled." );
    meta["workflowHints"].append( "Run after calibration/atmospheric correction; feed the flag "
                                  "bands to rs:apply_mask or custom band math to exclude "
                                  "flagged pixels." );
    meta["limitations"].append( "Flags describe the delivered values, not the sensor's full "
                                "quality model; pair with QaMask on QA_PIXEL/SCL for the "
                                "complete classification." );
    return meta;
}

Json::Value RsRadiometricQaOperator::executionEstimate() const {
    Json::Value est( Json::objectValue );
    est["tileWidth"] = kTileDim;
    est["tileHeight"] = kTileDim;
    // float tile + uint16 flag tile + optional single-band mask tile.
    est["estimatedRamBytes"] = Json::Value::UInt64(
        3ULL * sizeof( float ) * kTileDim * kTileDim );
    return est;
}

Json::Value RsRadiometricQaOperator::run( const Json::Value &params, RSOperatorContext &context )
{
    if ( !params.isObject() )
        throw RSOperatorError( ErrorCode::InvalidParameter, "Operator parameters must be a JSON object" );

    const std::string inputPath = requireString( params, "input" );
    const std::string outputPath = requireString( params, "output" );
    const float saturationLevel = static_cast<float>( getDouble( params, "saturation_level", 0.0 ) );
    const std::string maskToken = getEnum( params, "mask_flag", { "cloud", "cloud_shadow", "snow" }, "cloud" );
    const uint16_t maskFlag = maskFlagFromToken( maskToken );
    const int qaBand = getInt( params, "qa_radsat_band", 0 );

    const bool hasCloudMask = params.isMember( "cloud_mask" ) && params["cloud_mask"].isString()
                              && !params["cloud_mask"].asString().empty();
    if ( qaBand < 0 )
        throw RSOperatorError( ErrorCode::InvalidParameter, "qa_radsat_band must be >= 0 (0 = disabled)" );

    ensureGdalInit();

    GdalDatasetWrapper ds;
    if ( !ds.open( QString::fromStdString( inputPath ) ) )
        throw RSOperatorError( ErrorCode::GdalError, "Failed to open input raster: " + inputPath );
    const int width = ds.width();
    const int height = ds.height();
    const int bandCount = ds.bandCount();
    if ( width <= 0 || height <= 0 || bandCount <= 0 )
        throw RSOperatorError( ErrorCode::InvalidInputData, "Input raster is empty: " + inputPath );
    if ( qaBand > bandCount )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "qa_radsat_band " + std::to_string( qaBand )
                                   + " exceeds the input band count " + std::to_string( bandCount ) );

    // Per-band saturation bit masks (uint16); default all zero = disabled.
    std::vector<uint16_t> radsatBits( bandCount, 0 );
    if ( qaBand > 0 && params.isMember( "qa_radsat_bits" ) && params["qa_radsat_bits"].isString()
         && !params["qa_radsat_bits"].asString().empty() )
    {
        const QStringList tokens =
            QString::fromStdString( params["qa_radsat_bits"].asString() )
                .split( ',', Qt::SkipEmptyParts );
        if ( tokens.size() != bandCount )
            throw RSOperatorError( ErrorCode::InvalidParameter,
                                   "qa_radsat_bits must have one entry per input band" );
        for ( int b = 0; b < bandCount; ++b )
        {
            bool okNum = false;
            const qlonglong v = tokens[b].toLongLong( &okNum );
            if ( !okNum || v < 0 || v > 65535 )
                throw RSOperatorError( ErrorCode::InvalidParameter,
                                       "qa_radsat_bits entries must be integers in [0, 65535]" );
            radsatBits[b] = static_cast<uint16_t>( v );
        }
    }

    GdalDatasetWrapper maskDs;
    if ( hasCloudMask )
    {
        if ( !maskDs.open( QString::fromStdString( params["cloud_mask"].asString() ) ) )
            throw RSOperatorError( ErrorCode::GdalError,
                                   "Failed to open cloud mask: " + params["cloud_mask"].asString() );
        const sicnu::data::GridCompatReport gridReport = sicnu::data::compareGrids(
            sicnu::processing::gridFromDataset( ds ),
            sicnu::processing::gridFromDataset( maskDs ) );
        if ( !gridReport.compatible() )
        {
            std::string message = "Cloud mask must share the input grid";
            if ( const std::optional<sicnu::data::GridCompatIssue> primary = gridReport.primaryBlocking() )
                message += ": " + primary->message.toStdString();
            throw RSOperatorError( ErrorCode::InvalidInputData, message );
        }
    }

    context.logInfo( "Radiometric QA over " + std::to_string( bandCount ) + " band(s)" );

    GdalMultibandBlockStream reflStream( ds, bandCount, kTileDim, kTileDim );
    GdalStreamingOutput output( QString::fromStdString( outputPath ), width, height, bandCount,
                                GDT_UInt16, ds.geoTransform(), ds.projection() );
    if ( !output.isOpen() )
        throw RSOperatorError( ErrorCode::FileNotWritable,
                               "Failed to create output raster: " + outputPath );

    const size_t tilePixels = static_cast<size_t>( kTileDim ) * kTileDim;
    std::vector<uint16_t> outFlags( tilePixels ), qaWords( tilePixels, 0 );
    std::vector<float> srcTile( tilePixels );
    std::vector<uint8_t> maskTile( hasCloudMask ? tilePixels : size_t{ 0 }, 0 );
    std::vector<uint64_t> flaggedTotals( bandCount, 0 ), pixelTotals( bandCount, 0 );
    const int totalTiles = reflStream.tileCount();
    int tileIndex = 0;
    const bool ok = reflStream.forEach( [&]( const GdalBlockStream::Tile &tile, const float *bip ) {
        context.throwIfCancelled();
        const size_t n = static_cast<size_t>( tile.width ) * tile.height;
        if ( hasCloudMask
             && !maskDs.readBandWindow( 1, tile.xOffset, tile.yOffset, tile.width, tile.height,
                                        maskTile.data() ) )
            return false;

        for ( int b = 0; b < bandCount; ++b )
        {
            for ( size_t i = 0; i < n; ++i )
                srcTile[i] = bip[i * bandCount + b];
            RadiometricQa::evaluateReflectance( srcTile.data(), outFlags.data(), n,
                                                saturationLevel, nullptr );
            if ( hasCloudMask )
                RadiometricQa::markFromMask( outFlags.data(), maskTile.data(), n, maskFlag );
            if ( qaBand > 0 && radsatBits[b] != 0 )
            {
                for ( size_t i = 0; i < n; ++i )
                    qaWords[i] = static_cast<uint16_t>( bip[i * bandCount + ( qaBand - 1 )] );
                RadiometricQa::addSaturationBits( outFlags.data(), qaWords.data(), n,
                                                  radsatBits[b] );
            }
            uint64_t flagged = 0;
            for ( size_t i = 0; i < n; ++i )
                flagged += outFlags[i] != 0 ? 1 : 0;
            flaggedTotals[b] += flagged;
            pixelTotals[b] += n;
            if ( !output.writeTile( b + 1, tile, outFlags.data() ) )
                return false;
        }
        ++tileIndex;
        context.reportProgress( static_cast<double>( tileIndex ) / totalTiles,
                                "Evaluating QA flags" );
        return true;
    } );
    if ( !ok )
    {
        output.abandon();
        throw RSOperatorError( ErrorCode::GdalError, "Failed to stream/evaluate tiles" );
    }

    output.setMetadataItem( QLatin1String( kQaSchemaKey ), QLatin1String( kQaSchemaId ) );
    QString closeError;
    if ( !output.closeWithError( &closeError ) )
        throw RSOperatorError( ErrorCode::GdalError,
                               "Failed to finalize output: " + closeError.toStdString() );

    Json::Value result( Json::objectValue );
    result["output"] = outputPath;
    result["bandCount"] = bandCount;
    result["width"] = width;
    result["height"] = height;
    result["mask_flag"] = maskToken;
    Json::Value bands( Json::objectValue );
    for ( int b = 0; b < bandCount; ++b )
    {
        Json::Value bj( Json::objectValue );
        bj["flagged_pixels"] = Json::UInt64( flaggedTotals[b] );
        bj["pixels"] = Json::UInt64( pixelTotals[b] );
        bj["flagged_fraction"] = pixelTotals[b]
                                     ? static_cast<double>( flaggedTotals[b] )
                                           / static_cast<double>( pixelTotals[b] )
                                     : 0.0;
        bands["band_" + std::to_string( b + 1 )] = bj;
    }
    result["bands"] = bands;
    context.reportProgress( 1.0, "Radiometric QA complete" );
    return result;
}

} // namespace sicnu::operators::rs
