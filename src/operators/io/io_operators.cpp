/***************************************************************************
 * io_operators.cpp — io:* operator family implementation (thin adapters).
 ***************************************************************************/
#include "io_operators.h"

#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_schema.h"

#include "geospatial/convert/raster_convert.h"
#include "geospatial/cog/cog_presets.h"
#include "geospatial/doctor/data_doctor.h"
#include "geospatial/formats/format_profiles.h"

#include <atomic>
#include <cstring>

namespace sicnu::operators::io
{
namespace
{

// Returns the operator-layer ErrorCode (namespace-scope enum in
// rs_operator_error.h — NOT nested in RSOperatorError).
ErrorCode mapGeoCode( sicnu::geo::ErrorCode code )
{
  using G = sicnu::geo::ErrorCode;
  switch ( code )
  {
    case G::InvalidArgument: return ErrorCode::InvalidParameter;
    case G::OpenFailed: return ErrorCode::FileNotReadable;
    case G::DriverMissing: return ErrorCode::GdalError;
    case G::MissingCrs: return ErrorCode::InvalidParameter;
    case G::InvalidCrs: return ErrorCode::InvalidParameter;
    case G::TransformFailed: return ErrorCode::GdalError;
    case G::WriteFailed: return ErrorCode::FileNotWritable;
    case G::FidelityLoss: return ErrorCode::InvalidInputData;
    case G::Unsupported: return ErrorCode::GdalError;
    case G::Cancelled: return ErrorCode::Cancelled;
    case G::IoError: return ErrorCode::FileNotWritable;
    // Foundation 5.0 additions (ADR 0141) — mapped so remote/corruption/
    // budget failures keep their meaning at the operator boundary instead
    // of collapsing into Unknown.
    case G::NotFound: return ErrorCode::FileNotFound;
    case G::PermissionDenied: return ErrorCode::FileNotReadable;
    case G::UnsupportedFormat: return ErrorCode::InvalidInputData;
    case G::UnsupportedProduct: return ErrorCode::InvalidInputData;
    case G::InvalidMetadata: return ErrorCode::InvalidInputData;
    case G::CorruptData: return ErrorCode::InvalidInputData;
    case G::NetworkError: return ErrorCode::GdalError;
    case G::Timeout: return ErrorCode::ExternalProcessTimeout;
    case G::ResourceExhausted: return ErrorCode::OutOfRange;
    case G::Incompatible: return ErrorCode::InvalidInputData;
  }
  return ErrorCode::Unknown;
}

/// Runs `body`, translating foundation GeoError into RSOperatorError.
Json::Value guarded( const std::function<Json::Value()> &body )
{
  try
  {
    return body();
  }
  catch ( const sicnu::geo::GeoError &error )
  {
    throw RSOperatorError( mapGeoCode( error.code() ), error.what(), error.details() );
  }
}

std::vector<std::string> creationOptionsFrom( const Json::Value &params )
{
  std::vector<std::string> options;
  for ( const std::string &option : params::getStringArray( params, "creationOptions" ) )
    options.push_back( option );
  return options;
}

std::vector<int> bandsFrom( const Json::Value &params )
{
  std::vector<int> bands;
  for ( const std::string &text : params::getStringArray( params, "bands" ) )
  {
    try
    {
      bands.push_back( std::stoi( text ) );
    }
    catch ( const std::exception & )
    {
      throw RSOperatorError( ErrorCode::TypeMismatch,
                             "bands entries must be numeric strings: " + text );
    }
  }
  return bands;
}

/// A ConvertProgress sink wired to the operator context (progress + cancel).
class ContextProgress final : public sicnu::geo::ConvertProgress
{
  public:
    explicit ContextProgress( RSOperatorContext &context )
      : mContext( context )
    {
      report = [ this ]( double fraction, const std::string &message ) {
        if ( fraction >= 0.0 )
          mContext.reportProgressForced( fraction, message );
        else
          mContext.logInfo( message );
        if ( mContext.isCancelled() )
          mCancel.store( true );
      };
      cancelFlag = &mCancel;
    }

  private:
    RSOperatorContext &mContext;
    std::atomic<bool> mCancel{ false };
};

} // namespace

void IoOperatorBase::translateExceptions() {} // kept for future per-op hooks; translation is in guarded()

std::string IoTranslateOperator::description() const
{
  return "Convert a raster between formats/subsets through GDAL translate; output is staged, "
         "validated and published atomically. Metadata fidelity follows the certified profile.";
}

Json::Value IoTranslateOperator::schema() const
{
  using namespace sicnu::operators::schema;
  Json::Value params;
  params["input"] = makeRasterParam( "input", "Input raster path" );
  params["output"] = makeOutputParam( "output", "Output raster path" );
  params["driver"] = makeStringParam( "driver", "Output GDAL driver short name", "GTiff" );
  params["creationOptions"] = makeStringParam( "creationOptions", "Driver creation options (NAME=VALUE array)" );
  params["bands"] = makeStringParam( "bands", "1-based band indices to keep" );
  params["width"] = makeIntegerParam( "width", "Target width (with height)" );
  params["height"] = makeIntegerParam( "height", "Target height (with width)" );
  params["targetCrs"] = makeStringParam( "targetCrs", "Assign/override CRS explicitly (EPSG:xxxx or WKT)" );
  params["resampling"] = makeEnumParam( "resampling", "Resampling for shrunk reads",
                                        { "near", "bilinear", "cubic", "cubicspline", "lanczos", "average", "mode" },
                                        "near" );
  Json::Value outputs;
  outputs["output"] = makeOutputParam( "output", "Converted raster", "tif" );
  Json::Value root = makeRootSchema( "Translate Raster", description(), params, outputs );
  root["required"] = makeRequired( { "input", "output" } );
  stampDeterminismGrade( root, determinismGrade() );
  return root;
}

Json::Value IoTranslateOperator::metadata() const
{
  Json::Value meta;
  meta["provider"] = "geospatial-io-foundation";
  meta["purpose"] = "Authoritative raster format conversion";
  meta["useCases"] = Json::Value( Json::arrayValue );
  meta["useCases"].append( "GeoTIFF ↔ VRT / PNG / NetCDF raster export" );
  meta["useCases"].append( "Band subset + windowed size change" );
  meta["tags"] = Json::Value( Json::arrayValue );
  meta["tags"].append( "io" );
  meta["tags"].append( "convert" );
  return meta;
}

Json::Value IoTranslateOperator::run( const Json::Value &params, RSOperatorContext &context )
{
  return guarded( [ & ] {
    sicnu::geo::TranslateOptions options;
    options.outputFormat = params::getString( params, "driver", "GTiff" );
    options.creationOptions = creationOptionsFrom( params );
    options.bands = bandsFrom( params );
    const int width = params::getInt( params, "width", 0 );
    const int height = params::getInt( params, "height", 0 );
    if ( ( width > 0 ) != ( height > 0 ) )
      throw RSOperatorError( ErrorCode::InvalidParameter,
                             "width and height must be given together" );
    options.targetWidth = width;
    options.targetHeight = height;
    options.exportCrs = params::getString( params, "targetCrs" );
    options.resampling = params::getString( params, "resampling" );

    ContextProgress progress( context );
    const sicnu::geo::TranslateResult result =
      sicnu::geo::translateRaster( params::requireString( params, "input" ),
                                   params::requireString( params, "output" ), options, &progress );
    context.reportProgressForced( 1.0, "translate complete" );
    return result.toJson();
  } );
}

std::string IoWarpOperator::description() const
{
  return "Reproject/warp a raster through GDALWarp with an explicitly declared target CRS; "
         "resampling, resolution, alignment and extent are explicit parameters.";
}

Json::Value IoWarpOperator::schema() const
{
  using namespace sicnu::operators::schema;
  Json::Value params;
  params["input"] = makeRasterParam( "input", "Input raster path" );
  params["output"] = makeOutputParam( "output", "Output raster path" );
  params["targetCrs"] = makeStringParam( "targetCrs", "Target CRS (EPSG:xxxx or WKT) — mandatory, never guessed" );
  params["resampling"] = makeEnumParam( "resampling", "Resampling method",
                                        { "near", "bilinear", "cubic", "cubicspline", "lanczos", "average", "mode" },
                                        "near" );
  params["resolution"] = makeNumberParam( "resolution", "Target pixel size in target CRS units" );
  params["bounds"] = makeStringParam( "bounds", "Target extent [minX,minY,maxX,maxY] in target CRS" );
  params["targetAlignedPixels"] = makeBooleanParam( "targetAlignedPixels", "Align target grid (-tap)", false );
  params["creationOptions"] = makeStringParam( "creationOptions", "Driver creation options" );
  Json::Value outputs;
  outputs["output"] = makeOutputParam( "output", "Warped raster", "tif" );
  Json::Value root = makeRootSchema( "Warp Raster", description(), params, outputs );
  root["required"] = makeRequired( { "input", "output", "targetCrs" } );
  stampDeterminismGrade( root, determinismGrade() );
  return root;
}

Json::Value IoWarpOperator::metadata() const
{
  Json::Value meta;
  meta["provider"] = "geospatial-io-foundation";
  meta["purpose"] = "Authoritative raster reprojection";
  meta["limitations"] = "Missing target CRS is refused; dataset CRS is never guessed.";
  meta["tags"] = Json::Value( Json::arrayValue );
  meta["tags"].append( "io" );
  meta["tags"].append( "crs" );
  return meta;
}

Json::Value IoWarpOperator::run( const Json::Value &params, RSOperatorContext &context )
{
  return guarded( [ & ] {
    sicnu::geo::WarpOptions options;
    options.targetCrs = params::requireString( params, "targetCrs" );
    options.resampling = params::getString( params, "resampling", "near" );
    if ( params::hasNumber( params, "resolution" ) )
    {
      options.targetResolutionX = params::getDouble( params, "resolution", 0.0 );
      options.targetResolutionY = options.targetResolutionX;
    }
    if ( params.isMember( "bounds" ) && params["bounds"].isArray() && params["bounds"].size() == 4 )
    {
      for ( const Json::Value &bound : params["bounds"] )
        options.targetBounds.push_back( bound.asDouble() );
    }
    options.targetAlignedPixels = params::getBool( params, "targetAlignedPixels", false ) ? "YES" : "NO";
    options.creationOptions = creationOptionsFrom( params );
    options.creationOptions.emplace_back( "COMPRESS=LZW" );
    options.creationOptions.emplace_back( "TILED=YES" );

    ContextProgress progress( context );
    const sicnu::geo::TranslateResult result =
      sicnu::geo::warpRaster( params::requireString( params, "input" ),
                              params::requireString( params, "output" ), options, &progress );
    context.reportProgressForced( 1.0, "warp complete" );
    return result.toJson();
  } );
}

std::string IoReprojectOperator::description() const
{
  return "Reproject a raster to a declared target CRS (io:warp with a minimal, explicit contract). "
         "The dataset CRS is read from the file; a CRS-less input is refused unless srcCrsOverride "
         "is declared by the caller.";
}

Json::Value IoReprojectOperator::schema() const
{
  using namespace sicnu::operators::schema;
  Json::Value params;
  params["input"] = makeRasterParam( "input", "Input raster path" );
  params["output"] = makeOutputParam( "output", "Output raster path" );
  params["targetCrs"] = makeStringParam( "targetCrs", "Target CRS — mandatory" );
  params["srcCrsOverride"] = makeStringParam( "srcCrsOverride", "Declare the source CRS when the file carries none "
                                                              "(the only sanctioned fallback)" );
  params["resampling"] = makeEnumParam( "resampling", "Resampling method",
                                        { "near", "bilinear", "cubic", "cubicspline", "lanczos" }, "near" );
  Json::Value outputs;
  outputs["output"] = makeOutputParam( "output", "Reprojected raster", "tif" );
  Json::Value root = makeRootSchema( "Reproject Raster", description(), params, outputs );
  root["required"] = makeRequired( { "input", "output", "targetCrs" } );
  stampDeterminismGrade( root, determinismGrade() );
  return root;
}

Json::Value IoReprojectOperator::metadata() const
{
  Json::Value meta;
  meta["provider"] = "geospatial-io-foundation";
  meta["purpose"] = "CRS-explicit reprojection for agents and pipelines";
  meta["tags"] = Json::Value( Json::arrayValue );
  meta["tags"].append( "io" );
  meta["tags"].append( "crs" );
  return meta;
}

Json::Value IoReprojectOperator::run( const Json::Value &params, RSOperatorContext &context )
{
  return guarded( [ & ] {
    const std::string input = params::requireString( params, "input" );
    const sicnu::geo::RasterMetadata meta = sicnu::geo::inspectRaster( input );
    if ( !meta.crs.valid && params::getString( params, "srcCrsOverride" ).empty() )
    {
      Json::Value details;
      details["path"] = input;
      details["policy"] = "declare srcCrsOverride to take responsibility for the source CRS";
      throw RSOperatorError( ErrorCode::InvalidParameter,
                             "input raster carries no CRS; refusing to guess", details );
    }
    sicnu::geo::WarpOptions options;
    options.targetCrs = params::requireString( params, "targetCrs" );
    options.resampling = params::getString( params, "resampling", "near" );
    options.creationOptions = { "COMPRESS=LZW", "TILED=YES" };
    ContextProgress progress( context );
    const sicnu::geo::TranslateResult result =
      sicnu::geo::warpRaster( input, params::requireString( params, "output" ), options, &progress );
    context.reportProgressForced( 1.0, "reproject complete" );
    return result.toJson();
  } );
}

std::string IoClipOperator::description() const
{
  return "Clip a raster to a declared extent in the source CRS (bounds mandatory). Refuses when the "
         "dataset carries no CRS — declare srcCrsOverride explicitly to take responsibility.";
}

Json::Value IoClipOperator::schema() const
{
  using namespace sicnu::operators::schema;
  Json::Value params;
  params["input"] = makeRasterParam( "input", "Input raster path" );
  params["output"] = makeOutputParam( "output", "Output raster path" );
  params["bounds"] = makeStringParam( "bounds", "Clip extent [minX,minY,maxX,maxY] — mandatory" );
  params["srcCrsOverride"] = makeStringParam( "srcCrsOverride", "Declare the source CRS when the file carries none" );
  params["creationOptions"] = makeStringParam( "creationOptions", "Driver creation options" );
  Json::Value outputs;
  outputs["output"] = makeOutputParam( "output", "Clipped raster", "tif" );
  Json::Value root = makeRootSchema( "Clip Raster", description(), params, outputs );
  root["required"] = makeRequired( { "input", "output", "bounds" } );
  stampDeterminismGrade( root, determinismGrade() );
  return root;
}

Json::Value IoClipOperator::metadata() const
{
  Json::Value meta;
  meta["provider"] = "geospatial-io-foundation";
  meta["purpose"] = "Extent clipping with explicit CRS policy";
  meta["tags"] = Json::Value( Json::arrayValue );
  meta["tags"].append( "io" );
  return meta;
}

Json::Value IoClipOperator::run( const Json::Value &params, RSOperatorContext &context )
{
  return guarded( [ & ] {
    const std::string input = params::requireString( params, "input" );
    const std::string srcOverride = params::getString( params, "srcCrsOverride" );
    const sicnu::geo::RasterMetadata meta = sicnu::geo::inspectRaster( input );
    if ( !meta.crs.valid && srcOverride.empty() )
    {
      Json::Value details;
      details["path"] = input;
      throw RSOperatorError( ErrorCode::InvalidParameter,
                             "input raster carries no CRS; declare srcCrsOverride to clip anyway", details );
    }
    std::vector<double> bounds;
    if ( params.isMember( "bounds" ) && params["bounds"].isArray() && params["bounds"].size() == 4 )
    {
      for ( const Json::Value &bound : params["bounds"] )
        bounds.push_back( bound.asDouble() );
    }
    if ( bounds.size() != 4 )
      throw RSOperatorError( ErrorCode::MissingRequiredParameter,
                             "bounds must be [minX,minY,maxX,maxY]" );

    sicnu::geo::WarpOptions options;
    // Clipping is spatially lossless: keep the source grid via near sampling.
    options.targetCrs = srcOverride.empty()
                          ? ( meta.crs.authid.empty() ? meta.crs.wkt : meta.crs.authid )
                          : srcOverride;
    options.resampling = "near";
    options.targetBounds = bounds;
    options.creationOptions = creationOptionsFrom( params );
    if ( options.creationOptions.empty() )
      options.creationOptions = { "COMPRESS=LZW", "TILED=YES" };

    ContextProgress progress( context );
    const sicnu::geo::TranslateResult result = sicnu::geo::warpRaster( input, params::requireString( params, "output" ),
                                                                       options, &progress );
    context.reportProgressForced( 1.0, "clip complete" );
    return result.toJson();
  } );
}

std::string IoConvertFormatOperator::description() const
{
  return "Convert a dataset to another format (raster and vector auto-detected). Rasters use the "
         "translate kernel; vectors stream through the foundation reader→writer contract with "
         "dataset-group atomic publish.";
}

Json::Value IoConvertFormatOperator::schema() const
{
  using namespace sicnu::operators::schema;
  Json::Value params;
  params["input"] = makeRasterParam( "input", "Input dataset (raster or vector)" );
  params["output"] = makeOutputParam( "output", "Output dataset path" );
  params["driver"] = makeStringParam( "driver", "Output GDAL driver short name (GTiff/GPKG/GeoJSON/...)", "GTiff" );
  params["creationOptions"] = makeStringParam( "creationOptions", "Driver creation options" );
  Json::Value outputs;
  outputs["output"] = makeOutputParam( "output", "Converted dataset", "" );
  Json::Value root = makeRootSchema( "Convert Format", description(), params, outputs );
  root["required"] = makeRequired( { "input", "output" } );
  stampDeterminismGrade( root, determinismGrade() );
  return root;
}

Json::Value IoConvertFormatOperator::metadata() const
{
  Json::Value meta;
  meta["provider"] = "geospatial-io-foundation";
  meta["purpose"] = "Format conversion with certified-profile fidelity";
  meta["useCases"] = Json::Value( Json::arrayValue );
  meta["useCases"].append( "GeoPackage → GeoJSON → GeoPackage round trips" );
  meta["useCases"].append( "GeoTIFF → NetCDF raster export" );
  meta["tags"] = Json::Value( Json::arrayValue );
  meta["tags"].append( "io" );
  return meta;
}

Json::Value IoConvertFormatOperator::run( const Json::Value &params, RSOperatorContext &context )
{
  return guarded( [ & ] {
    const std::string input = params::requireString( params, "input" );
    const std::string output = params::requireString( params, "output" );
    const std::string driver = params::getString( params, "driver", "GTiff" );
    // Vector path: a vector driver name. Raster otherwise.
    if ( driver == "GPKG" || driver == "GeoJSON" || driver == "ESRI Shapefile" || driver == "FlatGeobuf"
         || driver == "CSV" )
    {
      ContextProgress progress( context );
      Json::Value result = sicnu::geo::vectorConvert( input, output, driver, "", "", "", {}, &progress );
      context.reportProgressForced( 1.0, "conversion complete" );
      return result;
    }
    sicnu::geo::TranslateOptions options;
    options.outputFormat = driver;
    options.creationOptions = creationOptionsFrom( params );
    ContextProgress progress( context );
    sicnu::geo::TranslateResult result = sicnu::geo::translateRaster( input, output, options, &progress );
    context.reportProgressForced( 1.0, "conversion complete" );
    return result.toJson();
  } );
}

std::string IoBuildOverviewsOperator::description() const
{
  return "Build overviews in place (gdaladdo semantics). Overviews are derived data; base pixels are "
         "never modified by this contract.";
}

Json::Value IoBuildOverviewsOperator::schema() const
{
  using namespace sicnu::operators::schema;
  Json::Value params;
  params["input"] = makeRasterParam( "input", "Raster path (modified in place)" );
  params["levels"] = makeStringParam( "levels", "Overview levels (powers of two, e.g. [2,4,8,16])" );
  params["resampling"] = makeEnumParam( "resampling", "Overview resampling",
                                        { "NEAREST", "AVERAGE", "GAUSS", "CUBIC", "MODE" }, "GAUSS" );
  Json::Value root = makeRootSchema( "Build Overviews", description(), params, Json::Value() );
  root["required"] = makeRequired( { "input" } );
  stampDeterminismGrade( root, determinismGrade() );
  return root;
}

Json::Value IoBuildOverviewsOperator::metadata() const
{
  Json::Value meta;
  meta["provider"] = "geospatial-io-foundation";
  meta["purpose"] = "Overview/remote-readiness building";
  meta["limitations"] = "In-place (derived data only).";
  meta["tags"] = Json::Value( Json::arrayValue );
  meta["tags"].append( "io" );
  return meta;
}

Json::Value IoBuildOverviewsOperator::run( const Json::Value &params, RSOperatorContext &context )
{
  return guarded( [ & ] {
    std::vector<int> levels;
    if ( params.isMember( "levels" ) && params["levels"].isArray() )
    {
      for ( const Json::Value &level : params["levels"] )
        levels.push_back( level.asInt() );
    }
    if ( levels.empty() )
    {
      // Default pyramid: 2..64.
      for ( int level = 2; level <= 64; level *= 2 )
        levels.push_back( level );
    }
    ContextProgress progress( context );
    const int built = sicnu::geo::buildOverviews( params::requireString( params, "input" ), levels,
                                                  params::getString( params, "resampling", "GAUSS" ), &progress );
    Json::Value result;
    result["input"] = params::requireString( params, "input" );
    result["levels_built"] = built;
    context.reportProgressForced( 1.0, "overviews complete" );
    return result;
  } );
}

std::string IoMakeCogOperator::description() const
{
  return "Produce a Cloud Optimized GeoTIFF through the COG driver with a safe preset "
         "(lossless_scientific / visualization / categorical / continuous_float / sar), then "
         "validate before publishing. Categorical scientific products are lossless by policy.";
}

Json::Value IoMakeCogOperator::schema() const
{
  using namespace sicnu::operators::schema;
  Json::Value params;
  params["input"] = makeRasterParam( "input", "Input raster path" );
  params["output"] = makeOutputParam( "output", "Output COG path" );
  params["preset"] = makeEnumParam( "preset", "COG preset",
                                    { "lossless_scientific", "visualization", "categorical",
                                      "continuous_float", "sar" },
                                    "lossless_scientific" );
  params["creationOptions"] = makeStringParam( "creationOptions", "Extra COG creation options" );
  Json::Value outputs;
  outputs["output"] = makeOutputParam( "output", "Cloud Optimized GeoTIFF", "tif" );
  Json::Value root = makeRootSchema( "Make COG", description(), params, outputs );
  root["required"] = makeRequired( { "input", "output" } );
  stampDeterminismGrade( root, determinismGrade() );
  return root;
}

Json::Value IoMakeCogOperator::metadata() const
{
  Json::Value meta;
  meta["provider"] = "geospatial-io-foundation";
  meta["purpose"] = "First-class COG production";
  meta["useCases"] = Json::Value( Json::arrayValue );
  meta["useCases"].append( "Publish scientific products for cloud range reads" );
  meta["limitations"] = "visualization preset is LOSSY — scientific products must use lossless presets.";
  meta["tags"] = Json::Value( Json::arrayValue );
  meta["tags"].append( "io" );
  meta["tags"].append( "cog" );
  return meta;
}

Json::Value IoMakeCogOperator::run( const Json::Value &params, RSOperatorContext &context )
{
  return guarded( [ & ] {
    const std::string presetName = params::getEnum( params, "preset",
                                                    { "lossless_scientific", "visualization", "categorical",
                                                      "continuous_float", "sar" },
                                                    "lossless_scientific" );
    sicnu::geo::CogPreset preset = sicnu::geo::CogPreset::LosslessScientific;
    if ( presetName == "visualization" )
      preset = sicnu::geo::CogPreset::Visualization;
    else if ( presetName == "categorical" )
      preset = sicnu::geo::CogPreset::Categorical;
    else if ( presetName == "continuous_float" )
      preset = sicnu::geo::CogPreset::ContinuousFloat;
    else if ( presetName == "sar" )
      preset = sicnu::geo::CogPreset::Sar;

    ContextProgress progress( context );
    const sicnu::geo::TranslateResult result = sicnu::geo::makeCog(
      params::requireString( params, "input" ), params::requireString( params, "output" ), preset,
      creationOptionsFrom( params ), &progress );
    context.reportProgressForced( 1.0, "COG creation complete" );
    return result.toJson();
  } );
}

std::string IoVectorConvertOperator::description() const
{
  return "Convert vector data (GPKG/GeoJSON/Shapefile/FlatGeobuf) with optional declared CRS "
         "transform, attribute filter and clip box; streams in bounded batches.";
}

Json::Value IoVectorConvertOperator::schema() const
{
  using namespace sicnu::operators::schema;
  Json::Value params;
  params["input"] = makeVectorParam( "input", "Input vector path" );
  params["output"] = makeOutputParam( "output", "Output vector path" );
  params["driver"] = makeStringParam( "driver", "Output driver short name", "GPKG" );
  params["layer"] = makeStringParam( "layer", "Layer name (default first)" );
  params["targetCrs"] = makeStringParam( "targetCrs", "Reproject features to this declared CRS" );
  params["where"] = makeStringParam( "where", "Attribute filter (OGR SQL WHERE)" );
  params["clipBounds"] = makeStringParam( "clipBounds", "Clip box [minX,minY,maxX,maxY] in layer CRS" );
  Json::Value outputs;
  outputs["output"] = makeOutputParam( "output", "Converted vector", "" );
  Json::Value root = makeRootSchema( "Convert Vector", description(), params, outputs );
  root["required"] = makeRequired( { "input", "output" } );
  stampDeterminismGrade( root, determinismGrade() );
  return root;
}

Json::Value IoVectorConvertOperator::metadata() const
{
  Json::Value meta;
  meta["provider"] = "geospatial-io-foundation";
  meta["purpose"] = "Streaming vector conversion";
  meta["limitations"] = "100k+ features stream in batches; memory stays bounded.";
  meta["tags"] = Json::Value( Json::arrayValue );
  meta["tags"].append( "io" );
  meta["tags"].append( "vector" );
  return meta;
}

Json::Value IoVectorConvertOperator::run( const Json::Value &params, RSOperatorContext &context )
{
  return guarded( [ & ] {
    std::vector<double> clipBounds;
    if ( params.isMember( "clipBounds" ) && params["clipBounds"].isArray() && params["clipBounds"].size() == 4 )
    {
      for ( const Json::Value &bound : params["clipBounds"] )
        clipBounds.push_back( bound.asDouble() );
    }
    ContextProgress progress( context );
    Json::Value result = sicnu::geo::vectorConvert(
      params::requireString( params, "input" ), params::requireString( params, "output" ),
      params::getString( params, "driver", "GPKG" ), params::getString( params, "layer" ),
      params::getString( params, "targetCrs" ), params::getString( params, "where" ), clipBounds, &progress );
    context.reportProgressForced( 1.0, "vector conversion complete" );
    return result;
  } );
}

std::string IoInspectOperator::description() const
{
  return "Canonical metadata inspection (raster/vector/multidimensional). Read-only; never triggers "
         "a full pixel or feature scan.";
}

Json::Value IoInspectOperator::schema() const
{
  using namespace sicnu::operators::schema;
  Json::Value params;
  params["input"] = makeRasterParam( "input", "Dataset path (raster, vector or multidimensional)" );
  params["includeStatistics"] = makeBooleanParam( "includeStatistics", "Compute and include statistics", false );
  Json::Value root = makeRootSchema( "Inspect Dataset", description(), params, Json::Value() );
  root["required"] = makeRequired( { "input" } );
  stampDeterminismGrade( root, determinismGrade() );
  return root;
}

Json::Value IoInspectOperator::metadata() const
{
  Json::Value meta;
  meta["provider"] = "geospatial-io-foundation";
  meta["purpose"] = "One canonical metadata vocabulary for every surface";
  meta["tags"] = Json::Value( Json::arrayValue );
  meta["tags"].append( "io" );
  meta["tags"].append( "metadata" );
  return meta;
}

Json::Value IoInspectOperator::run( const Json::Value &params, RSOperatorContext &context )
{
  return guarded( [ & ] {
    sicnu::geo::InspectOptions options;
    options.includeStatistics = params::getBool( params, "includeStatistics", false );
    Json::Value result = sicnu::geo::runInspect( params::requireString( params, "input" ), options );
    context.reportProgressForced( 1.0, "inspect complete" );
    return result;
  } );
}

std::string IoDoctorOperator::description() const
{
  return "Read-only structured dataset diagnostics: readability, driver, CRS, geotransform, nodata, "
         "band metadata, scale/offset, overviews, geometry, encoding, sidecars and remote access.";
}

Json::Value IoDoctorOperator::schema() const
{
  using namespace sicnu::operators::schema;
  Json::Value params;
  params["input"] = makeRasterParam( "input", "Dataset path to diagnose" );
  params["includeStatistics"] = makeBooleanParam( "includeStatistics", "Compute bounded band statistics", false );
  Json::Value root = makeRootSchema( "Data Doctor", description(), params, Json::Value() );
  root["required"] = makeRequired( { "input" } );
  stampDeterminismGrade( root, determinismGrade() );
  return root;
}

Json::Value IoDoctorOperator::metadata() const
{
  Json::Value meta;
  meta["provider"] = "geospatial-io-foundation";
  meta["purpose"] = "Pre-flight health checks for datasets";
  meta["limitations"] = "Strictly read-only: never writes metadata domains or sidecars.";
  meta["tags"] = Json::Value( Json::arrayValue );
  meta["tags"].append( "io" );
  meta["tags"].append( "doctor" );
  return meta;
}

Json::Value IoDoctorOperator::run( const Json::Value &params, RSOperatorContext &context )
{
  return guarded( [ & ] {
    sicnu::geo::InspectOptions options;
    options.includeStatistics = params::getBool( params, "includeStatistics", false );
    const sicnu::geo::DoctorReport report = sicnu::geo::runDoctor( params::requireString( params, "input" ), options );
    context.reportProgressForced( 1.0, "doctor complete" );
    return report.toJson();
  } );
}

} // namespace sicnu::operators::io
