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
#include "geospatial/io/cog_options.h"
#include "geospatial/io/param_guard.h"
#include "geospatial/io/finalize_manifest.h"
#include "geospatial/io/metadata_patch.h"
#include "geospatial/io/stage_ledger.h"
#include "geospatial/io/subdataset_inventory.h"
#include "geospatial/io/vector_interchange.h"
#include "geospatial/metadata/canonical_metadata.h"

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
      sicnu::geo::translateRaster( sicnu::geo::io::checkSourcePath( params::requireString( params, "input" ) ).openPath(),
                                   sicnu::geo::io::checkTargetPath( params::requireString( params, "output" ) ).openPath(),
                                   options, &progress );
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
      sicnu::geo::warpRaster( sicnu::geo::io::checkSourcePath( params::requireString( params, "input" ) ).openPath(),
                              sicnu::geo::io::checkTargetPath( params::requireString( params, "output" ) ).openPath(),
                              options, &progress );
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
    const std::string input = sicnu::geo::io::checkSourcePath( params::requireString( params, "input" ) ).openPath();
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
    // F-OPS-4: the declared source CRS must reach the warp — before this it
    // was validated and then dropped, so a CRS-less input was reprojected
    // as a no-op while the output carried the target CRS tag.
    options.sourceCrsOverride = params::getString( params, "srcCrsOverride", "" );
    options.resampling = params::getString( params, "resampling", "near" );
    options.creationOptions = { "COMPRESS=LZW", "TILED=YES" };
    ContextProgress progress( context );
    const sicnu::geo::TranslateResult result =
      sicnu::geo::warpRaster( input, sicnu::geo::io::checkTargetPath( params::requireString( params, "output" ) ).openPath(),
                              options, &progress );
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
    const std::string input = sicnu::geo::io::checkSourcePath( params::requireString( params, "input" ) ).openPath();
    const std::string srcOverride = params::getString( params, "srcCrsOverride" );
    const sicnu::geo::RasterMetadata meta = sicnu::geo::inspectRaster( input );
    if ( !meta.crs.valid && srcOverride.empty() )
    {
      Json::Value details;
      details["path"] = sicnu::geo::ResourceUri::parse( input ).display(); // redacted

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

    // F-OPS-4 follow-up (#1001): srcCrsOverride DECLARES the source CRS of a
    // CRS-less input — it is never the clip target. Clipping keeps the source
    // grid (near sampling, no reprojection). An override on an input that
    // already carries a CRS is a contradiction (the caller would silently
    // re-tag georeferenced pixels), so it is refused instead of guessed.
    if ( meta.crs.valid && !srcOverride.empty() )
    {
      Json::Value details;
      details["path"] = sicnu::geo::ResourceUri::parse( input ).display(); // redacted

      details["input_crs"] = meta.crs.authid.empty() ? meta.crs.wkt : meta.crs.authid;
      details["srcCrsOverride"] = srcOverride;
      throw RSOperatorError( ErrorCode::InvalidParameter,
                             "input already carries a CRS; srcCrsOverride would reinterpret it — "
                             "remove the override or reproject explicitly with io:reproject",
                             details );
    }

    sicnu::geo::WarpOptions options;
    // Clipping is spatially lossless: keep the source grid via near sampling.
    options.targetCrs = meta.crs.authid.empty() ? ( srcOverride.empty() ? meta.crs.wkt : srcOverride )
                                                : meta.crs.authid;
    options.sourceCrsOverride = srcOverride;
    options.resampling = "near";
    options.targetBounds = bounds;
    options.creationOptions = creationOptionsFrom( params );
    if ( options.creationOptions.empty() )
      options.creationOptions = { "COMPRESS=LZW", "TILED=YES" };

    ContextProgress progress( context );
    const sicnu::geo::TranslateResult result = sicnu::geo::warpRaster(
      input, sicnu::geo::io::checkTargetPath( params::requireString( params, "output" ) ).openPath(), options, &progress );
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
    const std::string input = sicnu::geo::io::checkSourcePath( params::requireString( params, "input" ) ).openPath();
    const std::string output = sicnu::geo::io::checkTargetPath( params::requireString( params, "output" ) ).openPath();
    const std::string driver = params::getString( params, "driver", "GTiff" );
    // Vector routing is capability-based (11.0): a vector driver that can
    // create datasets goes through the streaming reader→writer contract —
    // no hard-coded name list, so GeoParquet/CSV/whatever this GDAL build
    // supports route correctly, and a non-create-capable driver falls
    // through to the raster kernel where its refusal names the reason.
    const sicnu::geo::io::VectorTargetCheck vectorTarget = sicnu::geo::io::checkVectorWriteTarget( driver );
    // Routing: a vector-capable driver normally goes to the vector kernel —
    // EXCEPT dual-capability drivers (netCDF, PDF, MBTiles, ... carry both
    // DCAP_RASTER and DCAP_VECTOR), where the INPUT's kind decides: a raster
    // input keeps the raster kernel (preserves e.g. "GeoTIFF → NetCDF raster
    // export"), a non-raster input takes the vector kernel.
    bool routeVector = false;
    if ( vectorTarget.usable )
      routeVector = vectorTarget.alsoRaster ? !sicnu::geo::io::inputOpensAsRaster( input ) : true;
    if ( routeVector )
    {
      ContextProgress progress( context );
      Json::Value result = sicnu::geo::vectorConvert( input, output, driver, "", "", "", {}, &progress );
      context.reportProgressForced( 1.0, "conversion complete" );
      return result;
    }
    if ( !vectorTarget.usable && vectorTarget.reasonCode != "not_vector" )
    {
      // A declared vector driver that cannot receive writes fails here with
      // the typed reason — never silently re-routed into the raster kernel.
      Json::Value details = vectorTarget.toJson();
      throw RSOperatorError( ErrorCode::InvalidParameter, vectorTarget.message, details );
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
    const std::string input = sicnu::geo::io::checkSourcePath( params::requireString( params, "input" ) ).openPath();
    const int built = sicnu::geo::buildOverviews( input, levels,
                                                  params::getString( params, "resampling", "GAUSS" ), &progress );
    Json::Value result;
    result["input"] = input;
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
  params["blocksize"] = makeIntegerParam( "blocksize", "Tile blocksize override (power of two, 128..4096)" );
  params["overviews"] = makeBooleanParam( "overviews", "Build overviews inside the COG (default true)" );
  params["deterministic"] = makeBooleanParam(
    "deterministic", "Deterministic byte output: NUM_THREADS=1 + pinned DEFLATE level" );
  params["deflateLevel"] = makeIntegerParam( "deflateLevel", "DEFLATE level for deterministic mode (1..9, default 6)" );
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

    // 11.0 explicit production options: the planner merges preset + caller
    // knobs (blocksize / overviews / determinism / extras) by REPLACE, so
    // overrides actually reach the COG driver (first-match-wins lookup makes
    // appended duplicates dead letters), and every key's provenance is
    // explained in the result.
    const std::string input = sicnu::geo::io::checkSourcePath( params::requireString( params, "input" ) ).openPath();
    sicnu::geo::io::CogProductionOptions options;
    options.preset = preset;
    options.blocksize = params::getInt( params, "blocksize", 0 );
    options.buildOverviews = params::getBool( params, "overviews", true );
    options.deterministic = params::getBool( params, "deterministic", false );
    options.deflateLevel = params::getInt( params, "deflateLevel", 6 );
    options.extraCreationOptions = creationOptionsFrom( params );
    // Preset fidelity policy is dtype-aware; the planner needs the dtype.
    const sicnu::geo::RasterMetadata inputMeta = sicnu::geo::inspectRaster( input );
    options.expectedDtypeName = inputMeta.bands.empty() ? std::string() : inputMeta.bands.front().dtype;

    const sicnu::geo::io::CogProductionPlan plan = sicnu::geo::io::planCogProduction( options );

    ContextProgress progress( context );
    const sicnu::geo::TranslateResult result = sicnu::geo::makeCogWithOptions(
      input, sicnu::geo::io::checkTargetPath( params::requireString( params, "output" ) ).openPath(), plan.creationOptions,
      &progress );

    // Deterministic mode implies reproducibility demands; record the plan so
    // the produced COG is explainable (which knob came from where).
    Json::Value cogResult = result.toJson();
    cogResult["cog_plan"] = plan.toJson();
    context.reportProgressForced( 1.0, "COG creation complete" );
    return cogResult;
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
      sicnu::geo::io::checkSourcePath( params::requireString( params, "input" ) ).openPath(),
      sicnu::geo::io::checkTargetPath( params::requireString( params, "output" ) ).openPath(),
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
  // #880 (merged with master's parallel fix): the schema must describe
  // exactly the parameters run() reads — includeStatistics is a real runtime
  // input (bounded, opt-in band statistics), declared, never silently
  // accepted.
  params["includeStatistics"] = makeBooleanParam( "includeStatistics",
                                                  "Include bounded per-band statistics (decimated read, <=512x512)",
                                                  false );
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
    Json::Value result = sicnu::geo::runInspect( sicnu::geo::io::checkSourcePath( params::requireString( params, "input" ) ).openPath(), options );
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
    const sicnu::geo::DoctorReport report =
      sicnu::geo::runDoctor( sicnu::geo::io::checkSourcePath( params::requireString( params, "input" ) ).openPath(), options );
    context.reportProgressForced( 1.0, "doctor complete" );
    return report.toJson();
  } );
}


// --- 11.0 interchange surface ------------------------------------------------

std::string IoSubdatasetsOperator::description() const
{
  return "Enumerate HDF/NetCDF/VRT subdatasets (bounded, classified, redacted) and optionally project "
         "one selected subdataset into the canonical metadata model.";
}

Json::Value IoSubdatasetsOperator::schema() const
{
  using namespace sicnu::operators::schema;
  Json::Value params;
  params["input"] = makeRasterParam( "input", "Multidimensional source (HDF/NetCDF/VRT)" );
  params["select"] = makeIntegerParam( "select", "1-based subdataset index: project this entry instead of listing" );
  params["maxEntries"] = makeIntegerParam( "maxEntries", "Inventory cap (1..64, default 64)" );
  Json::Value outputs;
  outputs["entries"] = makeOutputParam( "entries", "Subdataset entries (list mode)", "" );
  outputs["metadata"] = makeOutputParam( "metadata", "Canonical metadata of the selected subdataset", "" );
  Json::Value root = makeRootSchema( "Subdataset Inventory", description(), params, outputs );
  root["required"] = makeRequired( { "input" } );
  stampDeterminismGrade( root, determinismGrade() );
  return root;
}

Json::Value IoSubdatasetsOperator::metadata() const
{
  Json::Value meta;
  meta["provider"] = "geospatial-io-foundation";
  meta["purpose"] = "Subdataset inventory and selection with safe URIs";
  meta["limitations"] = "Inventory is capped at 64 entries; truncation is reported, never silent.";
  meta["tags"] = Json::Value( Json::arrayValue );
  meta["tags"].append( "io" );
  meta["tags"].append( "multidim" );
  return meta;
}

Json::Value IoSubdatasetsOperator::run( const Json::Value &params, RSOperatorContext &context )
{
  return guarded( [ & ] {
    const std::string input = sicnu::geo::io::checkSourcePath( params::requireString( params, "input" ) ).openPath();
    const int select = params::getInt( params, "select", 0 );
    if ( select == 0 )
    {
      const int maxEntries = params::getInt( params, "maxEntries", sicnu::geo::io::kMaxSubdatasetEntries );
      const sicnu::geo::io::SubdatasetInventory inventory = sicnu::geo::io::inventorySubdatasets( input, maxEntries );
      context.reportProgressForced( 1.0, "inventory complete" );
      return inventory.toJson();
    }
    const sicnu::geo::io::SubdatasetInventory inventory =
      sicnu::geo::io::inventorySubdatasets( input, sicnu::geo::io::kMaxSubdatasetEntries );
    if ( select < 1 || select > static_cast<int>( inventory.entries.size() ) )
    {
      Json::Value details;
      details["select"] = select;
      details["count"] = static_cast<Json::UInt64>( inventory.entries.size() );
      throw RSOperatorError( ErrorCode::InvalidParameter, "subdataset selection out of range", details );
    }
    const sicnu::geo::RasterMetadata meta =
      sicnu::geo::io::inspectSubdataset( inventory.entries[static_cast<std::size_t>( select ) - 1].name );
    Json::Value result = meta.toJson();
    result["selected_index"] = select;
    context.reportProgressForced( 1.0, "projection complete" );
    return result;
  } );
}

std::string IoMetadataPatchOperator::description() const
{
  return "Whitelist-validated metadata write-back (scale/offset/unit/nodata/role/wavelength/fwhm/color/"
         "SICNU_* stamps): validate-then-apply, update-capability gate, read-back verification, and "
         "finalize-manifest digest continuity.";
}

Json::Value IoMetadataPatchOperator::schema() const
{
  using namespace sicnu::operators::schema;
  Json::Value params;
  params["input"] = makeRasterParam( "input", "Target dataset (update-capable driver required)" );
  params["patches"] = makeStringParam( "patches", "Array of {band, field, value} (band 0 = dataset stamp)" );
  Json::Value outputs;
  outputs["report"] = makeOutputParam( "report", "Applied fields and manifest status", "" );
  Json::Value root = makeRootSchema( "Patch Metadata", description(), params, outputs );
  root["required"] = makeRequired( { "input", "patches" } );
  stampDeterminismGrade( root, determinismGrade() );
  return root;
}

Json::Value IoMetadataPatchOperator::metadata() const
{
  Json::Value meta;
  meta["provider"] = "geospatial-io-foundation";
  meta["purpose"] = "Declared metadata correction on existing datasets";
  meta["limitations"] = "Whitelist fields only; drivers without update support are refused (never a full "
                        "rewrite); verify with io:verify_dataset.";
  meta["tags"] = Json::Value( Json::arrayValue );
  meta["tags"].append( "io" );
  meta["tags"].append( "metadata" );
  return meta;
}

Json::Value IoMetadataPatchOperator::run( const Json::Value &params, RSOperatorContext &context )
{
  return guarded( [ & ] {
    const std::string input = sicnu::geo::io::checkTargetPath( params::requireString( params, "input" ) ).openPath();
    const Json::Value &patchParams = params["patches"];
    if ( !patchParams.isArray() || patchParams.empty() )
      throw RSOperatorError( ErrorCode::InvalidParameter, "patches must be a non-empty array" );
    std::vector<sicnu::geo::io::MetadataPatch> patches;
    for ( const Json::Value &entry : patchParams )
    {
      if ( !entry.isObject() || !entry.isMember( "field" ) || !entry.isMember( "value" ) )
        throw RSOperatorError( ErrorCode::InvalidParameter, "each patch needs field and value" );
      if ( !entry["field"].isString() || !entry["value"].isString() )
        throw RSOperatorError( ErrorCode::InvalidParameter, "patch field and value must be strings" );
      sicnu::geo::io::MetadataPatch patch;
      if ( entry.isMember( "band" ) )
      {
        if ( !entry["band"].isInt() )
          throw RSOperatorError( ErrorCode::InvalidParameter, "patch band must be an integer" );
        patch.band = entry["band"].asInt();
      }
      patch.field = entry["field"].asString();
      patch.value = entry["value"].asString();
      patches.push_back( patch );
    }
    const sicnu::geo::io::MetadataPatchReport report = sicnu::geo::io::applyMetadataPatch( input, patches );
    context.reportProgressForced( 1.0, "patch complete" );
    return report.toJson();
  } );
}

std::string IoVerifyDatasetOperator::description() const
{
  return "Independent dataset integrity check: recompute the streamed SHA-256 against the finalize "
         "manifest sidecar and re-check the declared shape. Fails closed when the manifest is absent "
         "unless allowMissingManifest is set (absence is then reported, never green-washed).";
}

Json::Value IoVerifyDatasetOperator::schema() const
{
  using namespace sicnu::operators::schema;
  Json::Value params;
  params["input"] = makeRasterParam( "input", "Dataset to verify" );
  params["allowMissingManifest"] = makeBooleanParam( "allowMissingManifest",
                                                     "Legacy tolerance: report absence instead of failing hard" );
  Json::Value outputs;
  outputs["verified"] = makeOutputParam( "verified", "Verification report", "" );
  Json::Value root = makeRootSchema( "Verify Dataset", description(), params, outputs );
  root["required"] = makeRequired( { "input" } );
  stampDeterminismGrade( root, determinismGrade() );
  return root;
}

Json::Value IoVerifyDatasetOperator::metadata() const
{
  Json::Value meta;
  meta["provider"] = "geospatial-io-foundation";
  meta["purpose"] = "Digest/shape integrity verification against finalize manifests";
  meta["limitations"] = "Requires a finalize manifest; datasets written without one verify only as "
                        "explicitly-tolerated legacy.";
  meta["tags"] = Json::Value( Json::arrayValue );
  meta["tags"].append( "io" );
  meta["tags"].append( "provenance" );
  return meta;
}

Json::Value IoVerifyDatasetOperator::run( const Json::Value &params, RSOperatorContext &context )
{
  return guarded( [ & ] {
    const bool allowMissing = params::getBool( params, "allowMissingManifest", false );
    const sicnu::geo::io::ManifestVerifyReport report =
      sicnu::geo::io::verifyDataset( sicnu::geo::io::checkSourcePath( params::requireString( params, "input" ) ).openPath(),
                                     allowMissing );
    context.reportProgressForced( 1.0, "verification complete" );
    return report.toJson();
  } );
}

} // namespace sicnu::operators::io
