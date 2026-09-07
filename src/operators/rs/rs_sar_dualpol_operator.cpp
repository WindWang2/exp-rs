/***************************************************************************
 * rs_sar_dualpol_operator.cpp — Milestone D.2 (dual-pol features)
 ***************************************************************************/
#include "rs_sar_dualpol_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "processing/algorithms/sar/sar_dualpol.h"
#include "processing/algorithms/sar/sar_metadata.h"
#include "processing/framework/resource_estimation.h"
#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h"

#include <QString>

#include <gdal.h>

#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace sicnu::operators::rs {

using namespace params;
using sicnu::sar::DualPolFeature;

namespace {

const std::vector<std::string> s_features = { "ratio", "normalized_difference",
                                              "log_ratio", "rvi", "span" };
const std::vector<std::string> s_domains = { "auto", "linear", "db" };

} // anonymous namespace

Json::Value RsSarDualPolOperator::schema() const {
    using namespace schema;
    Json::Value props( Json::objectValue );
    props["input"] = makeRasterParam( "input", "Calibrated dual-pol SAR raster (VV and VH bands)" );
    props["output"] = makeOutputParam( "output", "Output feature raster", "tif" );
    props["feature"] = makeEnumParam( "feature", "Dual-pol feature to compute", s_features, "rvi" );
    props["vv_band"] = makeIntegerParam( "vv_band", "1-based VV (co-pol) band", 1 );
    props["vh_band"] = makeIntegerParam( "vh_band", "1-based VH (cross-pol) band", 2 );
    props["domain"] = makeEnumParam( "domain", "Input radiometric domain", s_domains, "auto" );

    Json::Value outputs( Json::objectValue );
    outputs["output"] = makeRasterParam( "output", "Output raster path" );
    outputs["feature"] = makeStringParam( "feature", "Computed feature", "" );

    Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
    root["required"] = makeRequired( { "input", "output", "feature" } );
    return root;
}

Json::Value RsSarDualPolOperator::metadata() const {
    Json::Value meta( Json::objectValue );
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append( "sar" );
    meta["tags"].append( "dual-pol" );
    meta["tags"].append( "rvi" );
    meta["task"] = "sar-feature-engineering";
    meta["notes"] = "Tile-streamed element-wise features on linear power; dB inputs "
                    "are converted before the kernel so the output domain is "
                    "independent of the input domain; bit-exact grade.";
    meta["gpu"] = false;
    meta["purpose"] = "Derive polarization-contrast features (vegetation index proxy, "
                      "ratio change inputs) from dual-pol backscatter.";
    meta["prerequisites"].append( "Input should be radiometrically calibrated backscatter (rs:sar_calibrate)." );
    meta["workflowHints"].append( "Chain rs:sar_change or rs:temporal_* over rvi/normalized_difference series for vegetation monitoring." );
    meta["limitations"].append( "rvi is the dual-pol Sentinel-1 approximation 4VH/(VV+VH), NOT the quad-pol RVI (needs a second cross-pol channel this platform does not model)." );
    meta["limitations"].append( "Nonpositive linear-power values are outside the SAR domain and yield NaN, never clamped." );
    return meta;
}

Json::Value RsSarDualPolOperator::executionEstimate() const {
    Json::Value est( Json::objectValue );
    est["tileWidth"] = 256;
    est["tileHeight"] = 256;
    est["estimatedRamBytes"] = Json::Value::UInt64( 3ULL * 256ULL * 256ULL * sizeof( float ) );
    return est;
}

Json::Value RsSarDualPolOperator::estimateExecution( const Json::Value &params ) const {
    if ( params.isObject() && params.isMember( "input" ) && params["input"].isString() )
    {
        GdalDatasetWrapper probe;
        if ( probe.open( QString::fromStdString( params["input"].asString() ) ) && probe.bandCount() > 0 )
        {
            std::optional<std::uint64_t> ram = sicnu::processing::checkedMulN(
                { 256ULL, 256ULL, static_cast<std::uint64_t>( probe.bandCount() ) + 1ULL,
                  static_cast<std::uint64_t>( sizeof( float ) ) } );
            if ( ram )
            {
                Json::Value est( Json::objectValue );
                est["tileWidth"] = Json::Value::UInt64( 256 );
                est["tileHeight"] = Json::Value::UInt64( 256 );
                est["estimatedRamBytes"] = Json::Value::UInt64( *ram );
                est["basis"] = "dynamic";
                return est;
            }
        }
    }
    return executionEstimate();
}

Json::Value RsSarDualPolOperator::run( const Json::Value &params, RSOperatorContext &context )
{
    if ( !params.isObject() )
        throw RSOperatorError( ErrorCode::InvalidParameter, "Operator parameters must be a JSON object" );

    const std::string inputPath = requireString( params, "input" );
    const std::string outputPath = requireString( params, "output" );
    const std::string featureToken = getEnum( params, "feature", s_features, "rvi" );
    DualPolFeature feature = DualPolFeature::Rvi;
    if ( !sicnu::sar::parseDualPolFeature( featureToken.c_str(), &feature ) )
        throw RSOperatorError( ErrorCode::InvalidParameter, "Unknown feature: " + featureToken );

    const int vvBand = getInt( params, "vv_band", 1 );
    const int vhBand = getInt( params, "vh_band", 2 );

    ensureGdalInit();

    GdalDatasetWrapper ds;
    if ( !ds.open( QString::fromStdString( inputPath ) ) )
        throw RSOperatorError( ErrorCode::GdalError, "Failed to open input raster: " + inputPath );
    const int width = ds.width();
    const int height = ds.height();
    const int bandCount = ds.bandCount();
    if ( vvBand < 1 || vvBand > bandCount || vhBand < 1 || vhBand > bandCount )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "vv_band/vh_band out of range (1-" + std::to_string( bandCount ) + ")" );
    if ( vvBand == vhBand )
        throw RSOperatorError( ErrorCode::InvalidParameter, "vv_band and vh_band must differ" );

    // Domain resolution: explicit > declared SICNU_SAR_DOMAIN > linear (warn).
    bool inputIsDb = false;
    const std::string domainParam = getEnum( params, "domain", s_domains, "auto" );
    const QString declaredDomain = sicnu::sar::datasetMeta( ds, sicnu::sar::kDomainKey );
    if ( domainParam == "db" )
        inputIsDb = true;
    else if ( domainParam == "linear" )
        inputIsDb = false;
    else if ( declaredDomain == QLatin1String( "db" ) )
        inputIsDb = true;
    else if ( !declaredDomain.isEmpty() && declaredDomain != QLatin1String( "linear_power" ) )
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "Unrecognized declared SICNU_SAR_DOMAIN: " + declaredDomain.toStdString() );
    else
        context.logWarning( "No declared SICNU_SAR_DOMAIN on the input and no explicit 'domain' "
                            "parameter; assuming linear power." );

    const float vvSentinel = [ & ] {
        bool has = false;
        const double nd = ds.bandNoDataValue( vvBand, &has );
        return ( has && std::isfinite( nd ) ) ? static_cast<float>( nd )
                                              : std::numeric_limits<float>::quiet_NaN();
    }();
    const float vhSentinel = [ & ] {
        bool has = false;
        const double nd = ds.bandNoDataValue( vhBand, &has );
        return ( has && std::isfinite( nd ) ) ? static_cast<float>( nd )
                                              : std::numeric_limits<float>::quiet_NaN();
    }();

    constexpr int kTile = 256;
    GdalMultibandBlockStream stream( ds, { vvBand, vhBand }, kTile, kTile );

    GdalStreamingOutput out( QString::fromStdString( outputPath ), width, height, 1, GDT_Float32,
                             ds.geoTransform(), ds.projection() );
    if ( !out.isOpen() )
        throw RSOperatorError( ErrorCode::FileNotWritable, "Failed to create output raster: " + outputPath );
    out.setNoDataValue( std::numeric_limits<double>::quiet_NaN() );

    const int totalTiles = stream.tileCount();
    int tileIndex = 0;
    std::vector<float> outTile;
    const bool ok = stream.forEach( [&]( const GdalMultibandBlockStream::Tile &tile, const float *bip ) {
        context.throwIfCancelled();
        const size_t tp = static_cast<size_t>( tile.width ) * tile.height;
        outTile.resize( tp );
        constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
        for ( size_t p = 0; p < tp; ++p )
        {
            double vv = bip[p * 2 + 0];
            double vh = bip[p * 2 + 1];
            if ( vv == vvSentinel || vh == vhSentinel )
            {
                outTile[p] = kNaN;
                continue;
            }
            if ( inputIsDb )
            {
                if ( !std::isfinite( vv ) || !std::isfinite( vh ) )
                {
                    outTile[p] = kNaN;
                    continue;
                }
                vv = std::pow( 10.0, vv / 10.0 );
                vh = std::pow( 10.0, vh / 10.0 );
            }
            outTile[p] = static_cast<float>( sicnu::sar::dualPolFeature( feature, vv, vh ) );
        }
        if ( !out.writeTile( 1, tile, outTile.data() ) )
            return false;
        context.reportProgress( 0.9 * ( ++tileIndex ) / totalTiles, "Computing " + featureToken );
        return true;
    } );
    if ( !ok )
    {
        out.abandon();
        throw RSOperatorError( ErrorCode::GdalError, "Failed to stream/derive tiles" );
    }

    // Provenance: keep the SAR contract truthful about the derived output.
    out.setMetadataItem( QLatin1String( sicnu::sar::kModalityKey ), QLatin1String( "sar" ) );
    out.setMetadataItem( QLatin1String( "SICNU_SAR_FEATURE" ), QString::fromStdString( featureToken ) );
    // dB inputs are converted to linear power before the kernel, so span is
    // linear power regardless of the input domain; the other features are
    // dimensionless.
    out.setMetadataItem( QLatin1String( sicnu::sar::kDomainKey ),
                         feature == DualPolFeature::Span ? QLatin1String( "linear_power" )
                                                         : QLatin1String( "dimensionless" ) );
    if ( const QString pols = sicnu::sar::datasetMeta( ds, sicnu::sar::kPolarizationsKey ); !pols.isEmpty() )
        out.setMetadataItem( QLatin1String( sicnu::sar::kPolarizationsKey ), pols );

    QString closeError;
    if ( !out.closeWithError( &closeError ) )
        throw RSOperatorError( ErrorCode::GdalError, "Failed to finalize output: " + closeError.toStdString() );

    Json::Value result( Json::objectValue );
    result["output"] = outputPath;
    result["feature"] = featureToken;
    result["inputDomain"] = inputIsDb ? "db" : "linear_power";
    result["width"] = width;
    result["height"] = height;
    context.reportProgress( 1.0, "Dual-pol feature complete" );
    return result;
}

} // namespace sicnu::operators::rs
