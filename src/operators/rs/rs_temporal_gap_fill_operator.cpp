// src/operators/rs/rs_temporal_gap_fill_operator.cpp
#include "rs_temporal_gap_fill_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "operators/rs/rs_temporal_collection_input.h"
#include "operators/rs/rs_temporal_output.h"
#include "processing/algorithms/temporal/temporal_stream.h"
#include "processing/algorithms/temporal/temporal_gapfill.h"
#include "processing/algorithms/temporal/temporal_irregular.h"
#include "processing/framework/resource_estimation.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <gdal.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace sicnu::operators::rs
{

using namespace params;
using temporal::GapFillMethod;
using temporal::TemporalTileReader;

namespace
{
constexpr int kDefaultTileSize = 256;
constexpr double kDefaultMaxGapDays = 90.0;
constexpr int kTypicalSceneEstimate = 8;
constexpr float kNan = std::numeric_limits<float>::quiet_NaN();

// The gap-fill method enum lives in the kernel (temporal/temporal_gapfill.h).

/// "YYYY-MM-DD" tag for band names; falls back to the chronological index
/// when a scene carries no parsable date (preflight normally rejects those).
QString sceneTag( const temporal::TemporalSceneRef &scene, int index )
{
  const QString d = scene.time.dateString();
  return d.isEmpty() ? QStringLiteral( "scene%1" ).arg( index + 1 ) : d;
}
} // namespace

std::string RsTemporalGapFillOperator::description() const
{
  return "Time-aware interpolation of missing (masked/NaN) samples in a "
         "per-pixel time series: linear in acquisition days or nearest valid "
         "neighbour, never bridging gaps wider than max_gap_days and never "
         "extrapolating past the ends of the series. Valid samples copy "
         "through unchanged; a final filled_count band documents how many "
         "samples were synthesised per pixel.";
}

Json::Value RsTemporalGapFillOperator::schema() const
{
  using namespace schema;
  Json::Value props( Json::objectValue );
  Json::Value scenes = makeStringParam( "scenes",
                                        "Scenes: array of {path, time?, bands?} or bare paths" );
  scenes["type"] = "array";
  scenes["items"] = Json::Value( Json::objectValue );
  scenes["items"]["type"] = "string";
  props["scenes"] = scenes;
  props["collection"] = makeStringParam( "collection",
                                         "...or path to a temporal collection descriptor JSON", "" );
  props["band"] = makeIntegerParam( "band", "Explicit analysis band (1-based)", 0 );
  props["band_role"] = makeEnumParam( "band_role", "Analysis band role",
                                      { "blue", "green", "red", "red_edge", "nir", "swir1",
                                        "swir2", "vv", "vh" },
                                      "" );
  props["duplicate_policy"] = makeEnumParam( "duplicate_policy", "Duplicate acquisition instants",
                                             { "keep_all", "reject" }, "keep_all" );
  props["apply_qa_masking"] = makeBooleanParam( "apply_qa_masking", "Exclude QA/cloud-masked samples", true );
  props["method"] = makeEnumParam( "method",
                                   "linear: distance-weighted between the bracketing valid "
                                   "samples (needs both sides); nearest: closest valid sample "
                                   "(ties -> earlier scene)",
                                   { "linear", "nearest" }, "linear" );
  Json::Value maxGap = makeNumberParam(
      "max_gap_days", "Widest gap bridged, in days (gaps wider than this stay NaN)",
      kDefaultMaxGapDays );
  setRange( maxGap, 0.0, 3650.0 );
  props["max_gap_days"] = maxGap;
  props["tile_size"] = makeIntegerParam( "tile_size", "Streaming tile size (pixels)", kDefaultTileSize );
  props["provenance_output"] = makeStringParam(
      "provenance_output",
      "Optional path for a per-date provenance GeoTIFF (UInt8, one prov_<date> "
      "band per scene): 0=unavailable (NaN in and out), 1=observed, "
      "2=interpolated (Temporal Phenology 12.0 QA contract)", "" );
  props["output"] = makeOutputParam(
      "output",
      "Gap-filled GeoTIFF (one band per scene date: filled_<date>, plus filled_count)",
      "tif" );

  Json::Value outputs( Json::objectValue );
  outputs["output"] = makeOutputParam( "output", "Gap-filled GeoTIFF", "tif" );
  outputs["sceneCount"] = makeIntegerParam( "sceneCount", "Scenes in the series", 0 );
  outputs["provenanceOutput"] = makeStringParam( "provenanceOutput",
      "Provenance raster path actually written (empty when not requested)", "" );
  outputs["filledFraction"] = makeNumberParam( "filledFraction",
                                               "Filled positions / fillable positions", 0.0 );
  outputs["bands"] = makeIntegerParam( "bands", "Output band count (= sceneCount + 1)", 0 );
  outputs["timeStart"] = makeStringParam( "timeStart", "First acquisition date in the series (ISO)", "" );
  outputs["timeEnd"] = makeStringParam( "timeEnd", "Last acquisition date in the series (ISO)", "" );
  Json::Value memory = makeStringParam( "memory", "Streaming working-set summary (tile size and estimated bytes)" );
  memory["type"] = "object";
  outputs["memory"] = memory;
  Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
  root["required"] = makeRequired( { "output" } );
  return root;
}

Json::Value RsTemporalGapFillOperator::metadata() const
{
  Json::Value meta( Json::objectValue );
  meta["group"] = group();
  meta["provider"] = "rs";
  Json::Value tags( Json::arrayValue );
  tags.append( "temporal" );
  tags.append( "gap-filling" );
  tags.append( "interpolation" );
  meta["tags"] = tags;
  meta["purpose"] = "Reconstruct cloud/QA-masked samples so downstream fits (trend, smoothing, "
                    "phenology) keep their sample budget";
  meta["prerequisites"] = "Common grid, acquisition times, consistent radiometric state "
                          "(temporal preflight); at least 2 scenes";
  meta["memoryPolicy"] = memoryPolicyName( memoryPolicy() );
  meta["deterministic"] = true;
  meta["supportsCancellation"] = true;
  meta["largeRasterSafe"] = true;
  meta["costClass"] = "O(tile × scenes × T)";
  meta["workflowHints"] = "Run on the QA-masked series (rs:temporal_composite leaves NaN for "
                          "cloudy) before rs:temporal_smooth / rs:temporal_trend; check the "
                          "filled_count band before trusting downstream statistics";
  meta["limitations"] = "Linear mode needs a valid sample on BOTH sides of a gap (no "
                        "extrapolation); gaps wider than max_gap_days stay NaN; filled values "
                        "are synthetic — the filledFraction result and the filled_count band "
                        "quantify how much of the output is interpolated";
  return meta;
}

Json::Value RsTemporalGapFillOperator::executionEstimate() const
{
  // 1 read tile + one column-major series tile per scene + filled_count tile
  // (typical input).
  return sicnu::processing::makeStreamingEstimate( kDefaultTileSize, kDefaultTileSize, 1, 4,
                                                   2 + kTypicalSceneEstimate, 0,
                                                   2 * 1024 * 1024 );
}

Json::Value RsTemporalGapFillOperator::estimateExecution( const Json::Value &params ) const
{
  const int tileSize = std::clamp( getInt( params, "tile_size", kDefaultTileSize ), 16, 4096 );
  int scenes = kTypicalSceneEstimate;
  if ( params.isMember( "scenes" ) && params["scenes"].isArray() )
    scenes = std::max<int>( 1, static_cast<int>( params["scenes"].size() ) );
  // 1 read tile + one series tile per scene date + filled_count tile; the
  // optional provenance raster doubles the per-scene output tiles.
  const bool withProvenance =
    params.isMember( "provenance_output" ) && params["provenance_output"].isString() &&
    !params["provenance_output"].asString().empty();
  return sicnu::processing::makeStreamingEstimate(
      tileSize, tileSize, 1, 4,
      2 + static_cast<std::uint64_t>( scenes ) * ( withProvenance ? 2 : 1 ), 0,
      2 * 1024 * 1024 );
}

Json::Value RsTemporalGapFillOperator::run( const Json::Value &params, RSOperatorContext &context )
{
  const std::string outputPath = requireString( params, "output" );
  const std::string provenancePath = getString( params, "provenance_output", "" );
  const QString methodToken =
      QString::fromStdString( getEnum( params, "method", { "linear", "nearest" }, "linear" ) );
  const GapFillMethod method = methodToken == QLatin1String( "nearest" )
                                   ? GapFillMethod::Nearest
                                   : GapFillMethod::Linear;
  const double maxGapDays = getDouble( params, "max_gap_days", kDefaultMaxGapDays );
  if ( !( maxGapDays >= 0.0 ) )
    throw RSOperatorError( ErrorCode::InvalidParameter, "max_gap_days must be >= 0" );

  const bool applyQaMasking = getBool( params, "apply_qa_masking", true );
  const int tileSize = std::clamp( getInt( params, "tile_size", kDefaultTileSize ), 16, 4096 );
  const QString bandRole = QString::fromStdString( getString( params, "band_role", "" ) );
  const int bandOverride = getInt( params, "band", 0 );

  auto prepared = temporal_input::prepareTemporalRun( params, context, {}, bandRole, bandOverride );
  const int sceneCount = prepared.collection.sceneCount();
  if ( sceneCount < 2 )
    throw RSOperatorError( ErrorCode::InvalidInputData,
                           "gap filling needs at least 2 scenes (got " +
                               std::to_string( sceneCount ) + ")" );

  temporal::TemporalStreamOptions streamOptions;
  streamOptions.tileWidth = tileSize;
  streamOptions.tileHeight = tileSize;
  streamOptions.applyQaMasking = applyQaMasking;

  QString readerError;
  TemporalTileReader reader( prepared.collection, prepared.preflight, streamOptions, &readerError );
  if ( !readerError.isEmpty() )
    throw RSOperatorError( ErrorCode::GdalError, readerError.toStdString() );

  std::vector<int> analysisBands( sceneCount, 1 );
  bool anyFallback = false;
  for ( int s = 0; s < sceneCount; ++s )
  {
    bool fallback = false;
    const int band = reader.bandForRole( s, bandRole, bandOverride, &fallback );
    // documented default: explicit band > role > positional fallback > band 1
    // An EXPLICIT band_role that resolves nowhere is a caller error: silently
    // falling back to band 1 would analyze a different band than requested
    // (e.g. "vh" advertised but absent → VV analyzed instead).
    if ( band <= 0 && !bandRole.isEmpty() && bandOverride <= 0 )
      throw RSOperatorError( ErrorCode::InvalidParameter,
                             "band_role '" + bandRole.toStdString() +
                                 "' cannot be resolved in scene " +
                                 prepared.collection.scenes().at( s ).path.toStdString() +
                                 "; pass an explicit band or fix the scene metadata" );
    analysisBands[s] = band > 0 ? band : 1;
    anyFallback = anyFallback || fallback;
  }
  if ( anyFallback )
    context.logWarning( "Analysis band resolved by positional fallback for at least one scene; "
                        "pass 'band' or 'bands' to pin it." );

  // Real time interval: days since the collection reference epoch (scene 0 of
  // the sorted collection) — never array indices.
  std::vector<double> tDays( sceneCount );
  for ( int s = 0; s < sceneCount; ++s )
    tDays[s] = reader.sceneDayOffset( s );

  const int width = reader.width();
  const int height = reader.height();
  const int bandCount = sceneCount + 1;

  context.reportProgress( 0.05, "Creating gap-fill output" );
  std::vector<QString> bandNames( static_cast<size_t>( bandCount ) );
  for ( int s = 0; s < sceneCount; ++s )
    bandNames[static_cast<size_t>( s )] = QStringLiteral( "filled_%1" ).arg(
        sceneTag( prepared.collection.scenes().at( s ), s ) );
  bandNames[static_cast<size_t>( sceneCount )] = QStringLiteral( "filled_count" );

  GdalDatasetWrapper out;
  QString outErr;
  temporal_output::TemporalOutputGuard guard;
  guard.manage( &out, QString::fromStdString( outputPath ) );
  if ( !out.create( QString::fromStdString( outputPath ), width, height, bandCount,
                    static_cast<int>( GDT_Float32 ), reader.geoTransform(), reader.projection(),
                    &outErr ) )
    throw RSOperatorError( ErrorCode::FileNotWritable,
                           "failed to create output: " + outErr.toStdString() );
  for ( int b = 1; b <= bandCount; ++b )
  {
    out.setBandNoDataValue( b, std::numeric_limits<double>::quiet_NaN() );
    GDALSetDescription( GDALGetRasterBand( static_cast<GDALDatasetH>( out.dataset() ), b ),
                        bandNames[static_cast<size_t>( b - 1 )].toUtf8().constData() );
  }

  // Optional per-date provenance raster (WP2 QA contract): UInt8 codes
  // sicnu::temporal::SampleProvenance — 0 unavailable, 1 observed,
  // 2 interpolated. Same grid/scene order as the filled bands.
  const bool writeProvenance = !provenancePath.empty();
  GdalDatasetWrapper prov;
  temporal_output::TemporalOutputGuard provGuard;
  if ( writeProvenance )
  {
    provGuard.manage( &prov, QString::fromStdString( provenancePath ) );
    if ( !prov.create( QString::fromStdString( provenancePath ), width, height,
                       sceneCount, static_cast<int>( GDT_Byte ),
                       reader.geoTransform(), reader.projection(), &outErr ) )
      throw RSOperatorError( ErrorCode::FileNotWritable,
                             "failed to create provenance output: " +
                                 outErr.toStdString() );
    for ( int s = 1; s <= sceneCount; ++s )
    {
      prov.setBandNoDataValue( s, 255.0 );
      GDALSetDescription(
          GDALGetRasterBand( static_cast<GDALDatasetH>( prov.dataset() ), s ),
          QStringLiteral( "prov_%1" )
              .arg( sceneTag( prepared.collection.scenes().at( s - 1 ), s - 1 ) )
              .toUtf8()
              .constData() );
    }
    GDALSetMetadataItem( static_cast<GDALDatasetH>( prov.dataset() ),
                         "SICNU_PROVENANCE_CODES",
                         "0=unavailable;1=observed;2=interpolated", nullptr );
  }

  const int tiles = reader.totalTileCount();
  // Guard against a nominal tile_size parameter pretending the working set is
  // bounded: allocations size the LARGEST CLAMPED tile rect (edge tiles are
  // smaller), and a series gathering that would exceed ~2 GB is rejected up
  // front with guidance instead of being attempted (OOM guard).
  const size_t maxTilePixels =
      static_cast<size_t>( std::min( tileSize, width ) ) *
      static_cast<size_t>( std::min( tileSize, height ) );
  constexpr size_t kMaxSeriesBytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;
  if ( static_cast<size_t>( sceneCount ) * maxTilePixels * sizeof( float ) >
       kMaxSeriesBytes )
    throw RSOperatorError(
        ErrorCode::InvalidParameter,
        "tile_size " + std::to_string( tileSize ) + " x " +
            std::to_string( sceneCount ) +
            " scenes exceeds the 2 GiB series-gathering budget; reduce tile_size "
        "(or scene count)" );
  const size_t tilePixels = maxTilePixels;

  // Column-major per-pixel series: series[s * tilePixels + i] — filled values
  // are written back in place, so no second T-tile buffer is needed.
  std::vector<float> series( tilePixels * static_cast<size_t>( sceneCount ) );
  std::vector<float> tile( tilePixels );
  std::vector<float> filledCount( tilePixels );
  std::vector<float> pixelSeries( static_cast<size_t>( sceneCount ) );
  // Provenance path state: the pre-fill copy and the per-scene code tile.
  std::vector<float> origSeries( writeProvenance ? sceneCount : 0 );
  std::vector<float> provBuf( writeProvenance
                                  ? tilePixels * static_cast<size_t>( sceneCount )
                                  : 0 );

  std::uint64_t filledPositions = 0;
  std::uint64_t fillablePositions = 0;
  int tileDone = 0;

  for ( int t = 0; t < tiles; ++t )
  {
    int x = 0, y = 0, w = 0, h = 0;
    reader.tileRect( t, &x, &y, &w, &h );
    const size_t pixels = static_cast<size_t>( w ) * h;

    for ( int s = 0; s < sceneCount; ++s )
    {
      if ( !reader.readSceneBandTile( s, analysisBands[s], t, tile.data() ) )
        throw RSOperatorError( ErrorCode::GdalError,
                               "failed reading scene " +
                                   prepared.collection.scenes().at( s ).path.toStdString() );
      std::copy( tile.begin(), tile.begin() + static_cast<std::ptrdiff_t>( pixels ),
                 series.begin() + static_cast<std::ptrdiff_t>( s * tilePixels ) );
      context.throwIfCancelled();
      }

    std::uint64_t tileFilled = 0;
    std::uint64_t tileFillable = 0;
    for ( size_t i = 0; i < pixels; ++i )
    {
      for ( int s = 0; s < sceneCount; ++s )
        pixelSeries[static_cast<size_t>( s )] = series[s * tilePixels + i];

      // The interpolation math is the shared kernel (known-answer tested);
      // the operator only gathers, calls, and writes back.
      if ( writeProvenance )
        origSeries = pixelSeries;
      temporal::GapFillCounts counts;
      temporal::gapFillSeries( pixelSeries.data(), sceneCount, tDays.data(),
                               method, maxGapDays, pixelSeries.data(), &counts );
      for ( int s = 0; s < sceneCount; ++s )
      {
        series[s * tilePixels + i] = pixelSeries[static_cast<size_t>( s )];
        if ( writeProvenance )
        {
          // Same mapping as temporal::gapFillProvenance, inlined into the
          // hot loop to keep it allocation-free.
          const float inV = origSeries[static_cast<size_t>( s )];
          const float outV = pixelSeries[static_cast<size_t>( s )];
          provBuf[s * tilePixels + i] = static_cast<float>(
              std::isfinite( inV ) && std::isfinite( outV )
                  ? temporal::SampleProvenance::Observed
              : !std::isfinite( inV ) && std::isfinite( outV )
                  ? temporal::SampleProvenance::Interpolated
                  : temporal::SampleProvenance::Unavailable );
        }
      }

      filledCount[i] = static_cast<float>( counts.filled );
      tileFilled += static_cast<std::uint64_t>( counts.filled );
      tileFillable += static_cast<std::uint64_t>( counts.fillable );
    }
    filledPositions += tileFilled;
    fillablePositions += tileFillable;

    for ( int s = 0; s < sceneCount; ++s )
    {
      if ( !out.writeBandWindow( s + 1, x, y, w, h, series.data() + s * tilePixels ) )
        throw RSOperatorError( ErrorCode::GdalError, "failed writing filled band" );
    }
    if ( !out.writeBandWindow( bandCount, x, y, w, h, filledCount.data() ) )
      throw RSOperatorError( ErrorCode::GdalError, "failed writing filled_count band" );
    if ( writeProvenance )
    {
      for ( int s = 0; s < sceneCount; ++s )
      {
        if ( !prov.writeBandWindow( s + 1, x, y, w, h,
                                    provBuf.data() + s * tilePixels ) )
          throw RSOperatorError( ErrorCode::GdalError,
                                 "failed writing provenance band" );
      }
    }

    ++tileDone;
    context.reportProgress( 0.05 + 0.93 * ( static_cast<double>( tileDone ) / tiles ),
                            "Gap-fill tiles " + std::to_string( tileDone ) + "/" +
                                std::to_string( tiles ) );
  }

  for ( int s = 0; s < sceneCount; ++s )
  {
    temporal_output::writeBandAcquisitionMetadata( out, s + 1,
                                                   prepared.collection.scenes().at( s ),
                                                   QStringLiteral( "filled" ) );
    // writeBandAcquisitionMetadata appends the date to the description; pin
    // the exact "filled_<date>" band name on top.
    GDALSetDescription( GDALGetRasterBand( static_cast<GDALDatasetH>( out.dataset() ), s + 1 ),
                        bandNames[static_cast<size_t>( s )].toUtf8().constData() );
  }
  temporal_output::writeTemporalDatasetMetadata(
    out, prepared.collection, "rs:temporal_gap_fill",
    QStringLiteral( "method=%1 max_gap_days=%2" ).arg( methodToken ).arg( maxGapDays ) );
  if ( !prepared.preflight.commonRadiometricState.isEmpty() )
    GDALSetMetadataItem( static_cast<GDALDatasetH>( out.dataset() ), "SICNU_RADIOMETRIC_STATE",
                         prepared.preflight.commonRadiometricState.toUtf8().constData(), nullptr );

  if ( writeProvenance )
  {
    temporal_output::writeTemporalDatasetMetadata(
        prov, prepared.collection, "rs:temporal_gap_fill.provenance",
        QStringLiteral( "method=%1 max_gap_days=%2 codes=0:unavailable,1:observed,"
                        "2:interpolated" )
            .arg( methodToken )
            .arg( maxGapDays ) );
  }

  // Atomic dual-output publication: both datasets must flush successfully
  // before EITHER guard commits, so a provenance-side failure cannot leak a
  // committed primary output from a failed run.
  QString closeErr;
  if ( !out.closeWithError( &closeErr ) )
    throw RSOperatorError( ErrorCode::FileNotWritable,
                           "output flush failed (disk full?): " + closeErr.toStdString() );
  if ( writeProvenance && !prov.closeWithError( &closeErr ) )
    throw RSOperatorError( ErrorCode::FileNotWritable,
                           "provenance flush failed (disk full?): " +
                               closeErr.toStdString() );
  guard.commit();
  if ( writeProvenance )
    provGuard.commit();

  Json::Value result( Json::objectValue );
  result["output"] = outputPath;
  result["provenanceOutput"] = writeProvenance ? provenancePath : "";
  result["sceneCount"] = sceneCount;
  result["filledFraction"] =
      fillablePositions > 0
          ? static_cast<double>( filledPositions ) / static_cast<double>( fillablePositions )
          : 0.0;
  result["bands"] = bandCount;
  if ( !prepared.collection.timeRangeStartIso().isEmpty() )
  {
    result["timeStart"] = prepared.collection.timeRangeStartIso().toStdString();
    result["timeEnd"] = prepared.collection.timeRangeEndIso().toStdString();
  }
  Json::Value memory( Json::objectValue );
  memory["tileWidth"] = tileSize;
  memory["tileHeight"] = tileSize;
  // 1 read tile + one column-major series tile per scene date + filled_count
  // (+ one provenance tile per scene when provenance_output is set).
  memory["workingSetEstimateBytes"] = Json::Value::UInt64(
    TemporalTileReader::estimateWorkingSetBytes(
        tileSize, tileSize,
        2 + static_cast<std::uint64_t>( sceneCount ) *
                ( writeProvenance ? 2 : 1 ),
        0 ) );
  result["memory"] = memory;
  context.reportProgress( 1.0, "Temporal gap fill complete" );
  return result;
}

} // namespace sicnu::operators::rs
