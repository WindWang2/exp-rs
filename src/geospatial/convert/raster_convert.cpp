/***************************************************************************
  geospatial/convert/raster_convert.cpp
  Geospatial I/O Foundation 4.0 — GDAL-backed conversion kernels.
  ---------------------------
  Begin                : 2026-09
  Copyright            : (C) 2026 SICNU GEO RS

  Raster conversions delegate to the authoritative GDAL utility kernels
  (GDALTranslate / GDALWarp); vector conversion streams through the
  foundation reader→writer contract so filters, declared CRS transforms and
  dataset-group atomicity behave identically everywhere. All outputs are
  staged beside the target, fsynced, validated and published.
 ***************************************************************************/

#include "geospatial/convert/raster_convert.h"

#include "geospatial/cog/cog_validator.h"
#include "geospatial/gdal_guard.h"
#include "geospatial/util/atomic_fs.h"
#include "geospatial/vector/vector_reader.h"
#include "geospatial/vector/vector_writer.h"

#include <cpl_conv.h>
#include <cpl_error.h>
#include <cpl_string.h>
#include <gdal.h>
#include <gdal_utils.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <optional>
#include <memory>
#include <utility>

namespace sicnu::geo
{
namespace
{

struct ProgressCtx
{
    ConvertProgress *progress = nullptr;
};

int CPL_STDCALL progressTrampoline( double fraction, const char *message, void *user )
{
  auto *ctx = static_cast<ProgressCtx *>( user );
  if ( ctx && ctx->progress )
  {
    if ( ctx->progress->cancelFlag && ctx->progress->cancelFlag->load() )
      return 0;
    if ( ctx->progress->report )
      ctx->progress->report( fraction, message ? message : "" );
  }
  return 1;
}

void throwGdalFailure( ErrorCode code, const std::string &message )
{
  const char *lastError = CPLGetLastErrorMsg();
  Json::Value details;
  if ( lastError && *lastError )
    details["gdal_error"] = lastError;
  throw GeoError( code, message, details );
}

std::vector<char *> toArgv( const std::vector<std::string> &args, std::vector<std::string> &storage )
{
  storage = args;
  std::vector<char *> argv;
  argv.reserve( storage.size() + 1 );
  for ( std::string &value : storage )
    argv.push_back( value.data() );
  argv.push_back( nullptr ); // GDAL option parsers expect a null-terminated argv
  return argv;
}

TranslateResult describeStaged( GDALDatasetH handle )
{
  TranslateResult result;
  result.width = GDALGetRasterXSize( handle );
  result.height = GDALGetRasterYSize( handle );
  result.bandCount = GDALGetRasterCount( handle );
  return result;
}

GDALDatasetH openRasterReadOnly( const std::string &path )
{
  ensureGdalRegistered();
  QuietCplErrors quiet;
  GDALDatasetH handle = GDALOpenEx( path.c_str(), GDAL_OF_READONLY | GDAL_OF_RASTER, nullptr, nullptr, nullptr );
  if ( !handle )
    throwGdalFailure( ErrorCode::OpenFailed, "Cannot open raster input: " + path );
  return handle;
}

void finishStaged( const std::string &stagedPath, const std::string &targetPath, TranslateResult &result )
{
  atomic_fs::fsyncFile( stagedPath );
  atomic_fs::publishStagedGroup( stagedPath, targetPath );
  result.output = targetPath;
}

} // namespace

Json::Value TranslateResult::toJson() const
{
  Json::Value json;
  json["output"] = output;
  json["width"] = width;
  json["height"] = height;
  json["band_count"] = bandCount;
  if ( warnings.isArray() && warnings.size() > 0 )
    json["warnings"] = warnings;
  return json;
}

TranslateResult translateRaster( const std::string &inputPath, const std::string &targetPath,
                                 const TranslateOptions &options, ConvertProgress *progress )
{
  if ( inputPath.empty() || targetPath.empty() )
    throw GeoError( ErrorCode::InvalidArgument, "translateRaster: empty input or target path" );

  GdalDatasetGuard source( openRasterReadOnly( inputPath ) );

  std::vector<std::string> args;
  args.emplace_back( "-of" );
  args.emplace_back( options.outputFormat );
  for ( const std::string &creationOption : options.creationOptions )
  {
    args.emplace_back( "-co" );
    args.emplace_back( creationOption );
  }
  if ( !options.resampling.empty() )
  {
    args.emplace_back( "-r" );
    args.emplace_back( options.resampling );
  }
  if ( options.targetWidth > 0 || options.targetHeight > 0 )
  {
    args.emplace_back( "-ts" );
    args.emplace_back( std::to_string( options.targetWidth ) );
    args.emplace_back( std::to_string( options.targetHeight ) );
  }
  if ( !options.exportCrs.empty() )
  {
    args.emplace_back( "-a_srs" );
    args.emplace_back( options.exportCrs );
  }
  for ( const int band : options.bands )
  {
    args.emplace_back( "-b" );
    args.emplace_back( std::to_string( band ) );
  }
  std::vector<std::string> storage;
  std::vector<char *> argv = toArgv( args, storage );
  GDALTranslateOptions *translateOptions = GDALTranslateOptionsNew( argv.data(), nullptr );
  if ( !translateOptions )
    throw GeoError( ErrorCode::InvalidArgument, "translateRaster: option construction failed" );
  ProgressCtx ctx{ progress };
  GDALTranslateOptionsSetProgress( translateOptions, &progressTrampoline, &ctx );

  const std::string stagedPath = atomic_fs::stagedPathFor( targetPath );
  try
  {
    int usageError = 0;
    GDALDatasetH stagedDataset = GDALTranslate( stagedPath.c_str(), source.get(), translateOptions, &usageError );
    GDALTranslateOptionsFree( translateOptions );
    if ( !stagedDataset )
    {
      const bool cancelled = progress && progress->cancelFlag && progress->cancelFlag->load();
      throwGdalFailure( cancelled ? ErrorCode::Cancelled : ErrorCode::WriteFailed,
                        "translateRaster: conversion failed" );
    }
    GdalDatasetGuard stagedGuard( stagedDataset );
    TranslateResult result = describeStaged( stagedDataset );
    stagedGuard.reset();
    source.reset();
    finishStaged( stagedPath, targetPath, result );
    return result;
  }
  catch ( ... )
  {
    atomic_fs::discardStaged( stagedPath );
    throw;
  }
}

TranslateResult warpRaster( const std::string &inputPath, const std::string &targetPath,
                            const WarpOptions &options, ConvertProgress *progress )
{
  if ( inputPath.empty() || targetPath.empty() )
    throw GeoError( ErrorCode::InvalidArgument, "warpRaster: empty input or target path" );
  // CRS policy: a warp without a declared target CRS is a caller bug.
  if ( options.targetCrs.empty() )
    throw GeoError( ErrorCode::MissingCrs,
                    "warpRaster: no target CRS declared; refusing to guess (declare target_crs explicitly)" );

  GdalDatasetGuard source( openRasterReadOnly( inputPath ) );

  std::vector<std::string> args;
  args.emplace_back( "-t_srs" );
  args.emplace_back( options.targetCrs );
  args.emplace_back( "-r" );
  args.emplace_back( options.resampling );
  args.emplace_back( "-of" );
  args.emplace_back( "GTiff" );
  if ( options.targetResolutionX > 0 && options.targetResolutionY > 0 )
  {
    args.emplace_back( "-tr" );
    args.emplace_back( std::to_string( options.targetResolutionX ) );
    args.emplace_back( std::to_string( options.targetResolutionY ) );
  }
  if ( options.targetAlignedPixels == "YES" )
    args.emplace_back( "-tap" );
  if ( options.targetBounds.size() == 4 )
  {
    args.emplace_back( "-te" );
    for ( const double bound : options.targetBounds )
      args.emplace_back( std::to_string( bound ) );
  }
  if ( options.warpMemoryLimitBytes > 0 )
  {
    args.emplace_back( "-wm" );
    args.emplace_back( std::to_string( options.warpMemoryLimitBytes / ( 1024 * 1024 ) ) + "m" );
  }
  if ( options.multithread )
    args.emplace_back( "-multi" );
  for ( const std::string &creationOption : options.creationOptions )
  {
    args.emplace_back( "-co" );
    args.emplace_back( creationOption );
  }
  std::vector<std::string> storage;
  std::vector<char *> argv = toArgv( args, storage );
  GDALWarpAppOptions *warpOptions = GDALWarpAppOptionsNew( argv.data(), nullptr );
  if ( !warpOptions )
    throw GeoError( ErrorCode::InvalidArgument, "warpRaster: option construction failed" );
  ProgressCtx ctx{ progress };
  GDALWarpAppOptionsSetProgress( warpOptions, &progressTrampoline, &ctx );

  const std::string stagedPath = atomic_fs::stagedPathFor( targetPath );
  try
  {
    int usageError = 0;
    GDALDatasetH sourceHandle = source.get();
    GDALDatasetH stagedDataset = GDALWarp( stagedPath.c_str(), nullptr, 1, &sourceHandle, warpOptions, &usageError );
    GDALWarpAppOptionsFree( warpOptions );
    if ( !stagedDataset )
    {
      const bool cancelled = progress && progress->cancelFlag && progress->cancelFlag->load();
      throwGdalFailure( cancelled ? ErrorCode::Cancelled : ErrorCode::WriteFailed,
                        "warpRaster: reprojection failed" );
    }
    GdalDatasetGuard stagedGuard( stagedDataset );
    TranslateResult result = describeStaged( stagedDataset );
    stagedGuard.reset();
    source.reset();
    finishStaged( stagedPath, targetPath, result );
    return result;
  }
  catch ( ... )
  {
    atomic_fs::discardStaged( stagedPath );
    throw;
  }
}

int buildOverviews( const std::string &path, const std::vector<int> &levels,
                    const std::string &resampling, ConvertProgress *progress )
{
  if ( path.empty() )
    throw GeoError( ErrorCode::InvalidArgument, "buildOverviews: empty path" );
  if ( levels.empty() )
    throw GeoError( ErrorCode::InvalidArgument, "buildOverviews: no levels requested" );

  ensureGdalRegistered();
  QuietCplErrors quiet;
  // Update mode: overviews are derived data; base pixels are untouched by the
  // algorithm contract and a crash only loses derived levels.
  GDALDatasetH handle = GDALOpenEx( path.c_str(), GDAL_OF_UPDATE | GDAL_OF_RASTER, nullptr, nullptr, nullptr );
  if ( !handle )
    throwGdalFailure( ErrorCode::OpenFailed, "buildOverviews: cannot open for update: " + path );
  GdalDatasetGuard guard( handle );

  std::vector<int> effectiveLevels = levels;
  std::sort( effectiveLevels.begin(), effectiveLevels.end() );
  ProgressCtx ctx{ progress };
  const CPLErr error = GDALBuildOverviews( handle, resampling.c_str(),
                                           static_cast<int>( effectiveLevels.size() ), effectiveLevels.data(),
                                           0, nullptr, &progressTrampoline, &ctx );
  if ( error != CE_None )
  {
    const bool cancelled = progress && progress->cancelFlag && progress->cancelFlag->load();
    throwGdalFailure( cancelled ? ErrorCode::Cancelled : ErrorCode::WriteFailed,
                      "buildOverviews: overview building failed" );
  }
  return static_cast<int>( effectiveLevels.size() );
}

TranslateResult makeCog( const std::string &inputPath, const std::string &targetPath,
                         CogPreset preset, const std::vector<std::string> &extraCreationOptions,
                         ConvertProgress *progress )
{
  if ( inputPath.empty() || targetPath.empty() )
    throw GeoError( ErrorCode::InvalidArgument, "makeCog: empty input or target path" );

  GdalDatasetGuard source( openRasterReadOnly( inputPath ) );

  // Preset policy is dtype-aware: the first band decides lossless/lossy fit.
  GDALRasterBandH firstBand = GDALGetRasterBand( source.get(), 1 );
  if ( !firstBand )
    throw GeoError( ErrorCode::InvalidArgument, "makeCog: input has no bands" );
  const char *dtypeName = GDALGetDataTypeName( GDALGetRasterDataType( firstBand ) );
  const CogPresetResult presetResult = cogPresetOptions( preset, dtypeName ? dtypeName : "" );

  std::vector<std::string> args;
  args.emplace_back( "-of" );
  args.emplace_back( "COG" );
  for ( const std::string &option : presetResult.creationOptions )
  {
    args.emplace_back( "-co" );
    args.emplace_back( option );
  }
  for ( const std::string &option : extraCreationOptions )
  {
    args.emplace_back( "-co" );
    args.emplace_back( option );
  }
  std::vector<std::string> storage;
  std::vector<char *> argv = toArgv( args, storage );
  GDALTranslateOptions *translateOptions = GDALTranslateOptionsNew( argv.data(), nullptr );
  if ( !translateOptions )
    throw GeoError( ErrorCode::InvalidArgument, "makeCog: option construction failed" );
  ProgressCtx ctx{ progress };
  GDALTranslateOptionsSetProgress( translateOptions, &progressTrampoline, &ctx );

  const std::string stagedPath = atomic_fs::stagedPathFor( targetPath );
  try
  {
    int usageError = 0;
    GDALDatasetH stagedDataset = GDALTranslate( stagedPath.c_str(), source.get(), translateOptions, &usageError );
    GDALTranslateOptionsFree( translateOptions );
    if ( !stagedDataset )
    {
      const bool cancelled = progress && progress->cancelFlag && progress->cancelFlag->load();
      throwGdalFailure( cancelled ? ErrorCode::Cancelled : ErrorCode::WriteFailed, "makeCog: COG creation failed" );
    }
    GdalDatasetGuard stagedGuard( stagedDataset );
    TranslateResult result = describeStaged( stagedDataset );
    stagedGuard.reset();
    source.reset();

    // Validate BEFORE publishing: a file that is not COG-conformant never
    // reaches the target path.
    const CogValidationReport report = validateCog( stagedPath );
    if ( !report.isCog )
    {
      Json::Value details;
      details["validation"] = report.checks;
      throw GeoError( ErrorCode::WriteFailed, "makeCog: staged output failed COG validation", details );
    }
    for ( const std::string &warning : presetResult.warnings )
      result.warnings.append( warning );
    finishStaged( stagedPath, targetPath, result );
    return result;
  }
  catch ( ... )
  {
    atomic_fs::discardStaged( stagedPath );
    throw;
  }
}

Json::Value vectorConvert( const std::string &inputPath, const std::string &targetPath,
                           const std::string &driverName, const std::string &layerName,
                           const std::string &targetCrs, const std::string &whereClause,
                           const std::vector<double> &clipBounds, ConvertProgress *progress )
{
  if ( inputPath.empty() || targetPath.empty() )
    throw GeoError( ErrorCode::InvalidArgument, "vectorConvert: empty input or target path" );
  if ( driverName.empty() )
    throw GeoError( ErrorCode::InvalidArgument, "vectorConvert: no output driver declared" );

  VectorReader reader = VectorReader::open( inputPath, layerName );

  // CRS transform: explicit target or nothing — the reader refuses to guess.
  std::optional<Crs> target;
  if ( !targetCrs.empty() )
    target = Crs::fromUserInput( targetCrs );
  if ( target )
    reader.setTargetCrs( *target );

  if ( !whereClause.empty() )
    reader.setAttributeFilter( whereClause );
  if ( clipBounds.size() == 4 )
  {
    // The clip box is expressed in the SOURCE layer CRS (documented in the
    // operator schema); filtering happens before any CRS transform.
    CrsBoundingBox box;
    box.minX = clipBounds[0];
    box.minY = clipBounds[1];
    box.maxX = clipBounds[2];
    box.maxY = clipBounds[3];
    reader.setSpatialFilter( box );
  }

  // Field schema flows through unchanged; the writer re-declares it.
  std::vector<VectorFieldSpec> fields;
  for ( const FieldInfo &field : reader.layerInfo().fields )
    fields.push_back( VectorFieldSpec{ field.name, field.typeName, field.width, field.precision } );

  VectorWriteOptions writeOptions;
  writeOptions.driver = driverName;
  writeOptions.overwrite = true; // conversion into an existing conversion target replaces it
  // Layer CRS flows through when present; a CRS-less layer stays CRS-less
  // unless an explicit target was declared (never guessed).
  Crs layerCrs;
  if ( reader.layerInfo().crs.valid )
    layerCrs = Crs::fromDatasetInfo( reader.layerInfo().crs );
  const Crs &outputCrs = target ? *target : layerCrs;
  VectorWriter writer = VectorWriter::create( targetPath, reader.layerInfo().name,
                                              reader.layerInfo().geometryTypeName, fields,
                                              outputCrs, writeOptions );

  Json::Value result;
  result["output"] = targetPath;
  Json::Int64 featureCount = 0;
  try
  {
    std::vector<VectorFeature> batch;
    while ( reader.nextBatch( batch, 2048 ) || !batch.empty() )
    {
      if ( progress && progress->cancelFlag && progress->cancelFlag->load() )
      {
        writer.cancel();
        throw GeoError( ErrorCode::Cancelled, "vectorConvert: cancelled by caller" );
      }
      for ( const VectorFeature &feature : batch )
      {
        writer.writeFeature( feature.attributes, feature.geometryWkt );
        ++featureCount;
      }
      if ( progress && progress->report )
        progress->report( -1.0, "converted " + std::to_string( featureCount ) + " features" );
      batch.clear();
    }
    writer.finalize();
  }
  catch ( ... )
  {
    writer.cancel();
    throw;
  }
  result["feature_count"] = featureCount;
  if ( target )
    result["target_crs"] = target->authid();
  return result;
}

} // namespace sicnu::geo
