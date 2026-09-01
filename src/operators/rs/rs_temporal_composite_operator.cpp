// src/operators/rs/rs_temporal_composite_operator.cpp
#include "rs_temporal_composite_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "operators/rs/rs_temporal_collection_input.h"
#include "operators/rs/rs_temporal_output.h"
#include "processing/algorithms/temporal/temporal_stream.h"
#include "processing/framework/resource_estimation.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QDate>
#include <QDateTime>
#include <QFile>
#include <QTimeZone>

#include <gdal.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <vector>

namespace sicnu::operators::rs
{

using namespace params;
using temporal::AcquisitionTime;
using temporal::TemporalTileReader;

namespace
{
constexpr int kDefaultTileSize = 256;
constexpr std::uint64_t kMedianMemoryBudgetBytes = 256ull * 1024 * 1024;
constexpr float kNan = std::numeric_limits<float>::quiet_NaN();

QString seasonKey( const QString &isoDate )
{
  const QDate d = QDate::fromString( isoDate, Qt::ISODate );
  if ( !d.isValid() )
    return isoDate;
  int year = d.year();
  const int m = d.month();
  int season;
  if ( m == 12 || m <= 2 )
  {
    season = 1; // DJF — December belongs to the winter anchored at next year's January
    if ( m == 12 )
      ++year;
  }
  else if ( m <= 5 )
    season = 2; // MAM
  else if ( m <= 8 )
    season = 3; // JJA
  else
    season = 4; // SON
  return QStringLiteral( "%1-S%2" ).arg( year ).arg( season );
}

/// Groups scene indices (chronological input) into period buckets with
/// chronologically-sorted keys.
std::map<QString, std::vector<int>> groupScenes( const temporal::TemporalCollection &collection,
                                                 const QString &period, int periodDays )
{
  std::map<QString, std::vector<int>> groups;
  const auto &scenes = collection.scenes();
  for ( int i = 0; i < scenes.size(); ++i )
  {
    const auto &s = scenes.at( i );
    if ( !s.time.valid )
      continue;
    const QString date = s.time.dateString();
    QString key;
    if ( period == QLatin1String( "month" ) )
      key = date.left( 7 ).remove( QLatin1Char( '-' ) );
    else if ( period == QLatin1String( "quarter" ) )
    {
      const QDate d = QDate::fromString( date, Qt::ISODate );
      key = d.isValid() ? QStringLiteral( "%1Q%2" ).arg( d.year() ).arg( ( d.month() - 1 ) / 3 + 1 )
                        : date;
    }
    else if ( period == QLatin1String( "season" ) )
      key = seasonKey( date );
    else if ( period == QLatin1String( "year" ) )
      key = date.left( 4 );
    else if ( period == QLatin1String( "custom" ) )
    {
      const qint64 bucket = periodDays > 0
                                ? s.time.epochMillis / ( 86400000LL * periodDays )
                                : 0;
      key = QStringLiteral( "%1" ).arg( bucket, 6, 10, QChar( QLatin1Char( '0' ) ) );
    }
    else
      key = QStringLiteral( "all" );
    groups[key].push_back( i );
  }
  return groups;
}

/// Chronological label for a group's file name suffix.
QString groupLabel( const temporal::TemporalCollection &collection,
                    const std::vector<int> &indices, const QString &period )
{
  if ( period == QLatin1String( "all" ) )
    return QString();
  const QString date = collection.scenes().at( indices.front() ).time.dateString();
  return QString( date ).remove( QLatin1Char( '-' ) );
}

int medianTileSide( int requestedTileSize, int maxGroupSize )
{
  if ( maxGroupSize < 1 )
    maxGroupSize = 1;
  const std::uint64_t maxPixels =
      kMedianMemoryBudgetBytes / ( 4 * static_cast<std::uint64_t>( maxGroupSize ) );
  std::uint64_t side = static_cast<std::uint64_t>( std::sqrt( static_cast<double>( maxPixels ) ) );
  side = std::min<std::uint64_t>( side, static_cast<std::uint64_t>( requestedTileSize ) );
  return static_cast<int>( std::max<std::uint64_t>( side, 16 ) );
}

bool medianBudgetInfeasible( int groupSize )
{
  return static_cast<std::uint64_t>( groupSize ) * 16 * 16 * 4 > kMedianMemoryBudgetBytes;
}

QString outputForPeriod( const std::string &outputPath, const QString &key, const QString &label )
{
  if ( label.isEmpty() )
    return QString::fromStdString( outputPath );
  QString base = QString::fromStdString( outputPath );
  if ( base.endsWith( QLatin1String( ".tif" ), Qt::CaseInsensitive ) )
    base.chop( 4 );
  return base + QLatin1Char( '_' ) + label + QStringLiteral( ".tif" );
}

} // namespace

std::string RsTemporalCompositeOperator::description() const
{
  return "Temporal composites (best-pixel, mean or median) over a multi-date "
         "collection, optionally grouped by month / quarter / season / year / "
         "custom period. Every output carries a valid-observation-count band "
         "and the chosen observation's quality score.";
}

Json::Value RsTemporalCompositeOperator::schema() const
{
  using namespace schema;
  Json::Value props( Json::objectValue );
  Json::Value scenes = makeStringParam( "scenes",
                                        "Scenes: array of {path, time?, bands?, quality_band?, "
                                        "mask_band?} or bare path strings" );
  scenes["type"] = "array";
  scenes["items"] = Json::Value( Json::objectValue );
  scenes["items"]["type"] = "string";
  props["scenes"] = scenes;
  props["collection"] = makeStringParam( "collection",
                                         "...or path to a temporal collection descriptor JSON", "" );
  props["band"] = makeIntegerParam( "band", "Explicit analysis band (1-based)", 0 );
  props["band_role"] = makeEnumParam( "band_role", "Analysis band role",
                                      { "blue", "green", "red", "red_edge", "nir", "swir1", "swir2" },
                                      "" );
  props["method"] = makeEnumParam( "method",
                                   "best_pixel: valid observation with the highest quality score "
                                   "(ties: closest to target_date, then earliest scene). "
                                   "mean/median: per-pixel statistic of valid observations",
                                   { "best_pixel", "mean", "median" }, "best_pixel" );
  props["quality_band"] = makeIntegerParam(
    "quality_band", "Global quality-score band (1-based, higher = better; use positive "
                    "scores — samples without a finite score rank lowest); per-scene "
                    "'quality_band' entries override. 0 = equal scores (tie-break by "
                    "target-date proximity)",
    0 );
  props["period"] = makeEnumParam( "period",
                                   "Group scenes into one composite per period (one file per "
                                   "period, labelled by start date)",
                                   { "all", "month", "quarter", "season", "year", "custom" },
                                   "all" );
  props["period_days"] = makeIntegerParam(
      "period_days", "Custom period length in days (buckets anchored at Unix-epoch "
                     "multiples of N days — deterministic; e.g. 8-day MODIS-style)", 0 );
  props["target_date"] = makeStringParam(
    "target_date", "ISO date for best-pixel temporal-closeness tie-breaks "
                   "(default: period midpoint)" );
  props["duplicate_policy"] = makeEnumParam( "duplicate_policy",
                                             "Duplicate acquisition instants",
                                             { "keep_all", "reject" }, "keep_all" );
  props["apply_qa_masking"] = makeBooleanParam( "apply_qa_masking", "Exclude QA/cloud-masked samples", true );
  props["tile_size"] = makeIntegerParam( "tile_size", "Streaming tile size (pixels)", kDefaultTileSize );
  props["output"] = makeOutputParam( "output", "Composite GeoTIFF (bands: composite, valid_count, quality)", "tif" );

  Json::Value outputs( Json::objectValue );
  outputs["output"] = makeOutputParam( "output", "Composite GeoTIFF (first period for grouped runs)", "tif" );
  outputs["sceneCount"] = makeIntegerParam( "sceneCount", "Scenes composited", 0 );
  outputs["periodCount"] = makeIntegerParam( "periodCount", "Composites produced", 0 );
  Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
  root["required"] = makeRequired( { "output" } );
  return root;
}

Json::Value RsTemporalCompositeOperator::metadata() const
{
  Json::Value meta( Json::objectValue );
  meta["group"] = group();
  meta["provider"] = "rs";
  Json::Value tags( Json::arrayValue );
  tags.append( "temporal" );
  tags.append( "composite" );
  tags.append( "best-pixel" );
  meta["tags"] = tags;
  meta["purpose"] = "Cloud-free / period composites from a multi-date collection with explicit "
                    "quality-score selection and observation counts";
  meta["prerequisites"] = "Common grid, acquisition times, consistent radiometric state "
                          "(temporal preflight)";
  meta["memoryPolicy"] = memoryPolicyName( memoryPolicy() );
  meta["deterministic"] = true;
  meta["supportsCancellation"] = true;
  meta["largeRasterSafe"] = true;
  meta["costClass"] = "O(tile × scenes)";
  meta["workflowHints"] = "Pairs with rs:qa_mask / rs:apply_mask when scenes need explicit "
                          "cloud masking; period grouping yields one file per period";
  meta["limitations"] = "Grouped runs write one GeoTIFF per period (suffix = start date); only "
                        "the first is returned as 'output'; the quality band is read with the "
                        "scene's declared scale/offset like any other band";
  return meta;
}

Json::Value RsTemporalCompositeOperator::executionEstimate() const
{
  return sicnu::processing::makeStreamingEstimate( kDefaultTileSize, kDefaultTileSize, 1, 4, 8, 0, 2 * 1024 * 1024 );
}

Json::Value RsTemporalCompositeOperator::estimateExecution( const Json::Value &params ) const
{
  const int tileSize = std::clamp( getInt( params, "tile_size", kDefaultTileSize ), 16, 4096 );
  const QString method = QString::fromStdString( getEnum( params, "method", { "best_pixel", "mean", "median" }, "best_pixel" ) );
  int scenes = 1;
  if ( params.isMember( "scenes" ) && params["scenes"].isArray() )
    scenes = std::max<int>( 1, static_cast<int>( params["scenes"].size() ) );
  int side = tileSize;
  std::uint64_t buffers = 8;
  if ( method == QLatin1String( "median" ) )
  {
    side = medianTileSide( tileSize, scenes );
    buffers = static_cast<std::uint64_t>( scenes );
  }
  return sicnu::processing::makeStreamingEstimate( side, side, 1, 4, buffers, 0, 2 * 1024 * 1024 );
}

Json::Value RsTemporalCompositeOperator::run( const Json::Value &params, RSOperatorContext &context )
{
  const std::string outputPath = requireString( params, "output" );
  const QString method = QString::fromStdString(
      getEnum( params, "method", { "best_pixel", "mean", "median" }, "best_pixel" ) );
  const QString period = QString::fromStdString(
      getEnum( params, "period", { "all", "month", "quarter", "season", "year", "custom" }, "all" ) );
  const int periodDays = getInt( params, "period_days", 0 );
  if ( period == QLatin1String( "custom" ) && periodDays <= 0 )
    throw RSOperatorError( ErrorCode::InvalidParameter,
                           "period='custom' requires period_days > 0" );
  const bool applyQaMasking = getBool( params, "apply_qa_masking", true );
  const int requestedTile = std::clamp( getInt( params, "tile_size", kDefaultTileSize ), 16, 4096 );
  const QString bandRole = QString::fromStdString( getString( params, "band_role", "" ) );
  const int bandOverride = getInt( params, "band", 0 );
  const int globalQualityBand = getInt( params, "quality_band", 0 );
  AcquisitionTime targetTime;
  if ( params.isMember( "target_date" ) && params["target_date"].isString() )
  {
    targetTime = temporal::parseAcquisitionTime(
      QString::fromStdString( params["target_date"].asString() ) );
    if ( !targetTime.valid )
      throw RSOperatorError( ErrorCode::InvalidParameter, "invalid target_date" );
  }

  auto prepared = temporal_input::prepareTemporalRun( params, context, {}, bandRole, bandOverride );
  const int sceneCount = prepared.collection.sceneCount();

  const auto groups = groupScenes( prepared.collection, period, periodDays );
  if ( groups.empty() )
    throw RSOperatorError( ErrorCode::InvalidInputData,
                           "no scene carries a valid acquisition time to group by" );

  int maxGroupSize = 0;
  for ( const auto &kv : groups )
    maxGroupSize = std::max( maxGroupSize, static_cast<int>( kv.second.size() ) );
  if ( method == QLatin1String( "median" ) && medianBudgetInfeasible( maxGroupSize ) )
    throw RSOperatorError( ErrorCode::InvalidParameter,
                           "median composite exceeds the median memory budget for groups this "
                           "large; reduce the group size or use method=mean" );

  const int tileSide = method == QLatin1String( "median" ) ? medianTileSide( requestedTile, maxGroupSize )
                                                           : requestedTile;
  temporal::TemporalStreamOptions streamOptions;
  streamOptions.tileWidth = tileSide;
  streamOptions.tileHeight = tileSide;
  streamOptions.applyQaMasking = applyQaMasking;

  QString readerError;
  TemporalTileReader reader( prepared.collection, prepared.preflight, streamOptions, &readerError );
  if ( !readerError.isEmpty() )
    throw RSOperatorError( ErrorCode::GdalError, readerError.toStdString() );

  // Analysis + quality bands per scene.
  std::vector<int> analysisBands( sceneCount, 1 );
  std::vector<int> qualityBands( sceneCount, 0 );
  bool anyFallback = false;
  for ( int s = 0; s < sceneCount; ++s )
  {
    bool fallback = false;
    const int band = reader.bandForRole( s, bandRole, bandOverride, &fallback );
    // documented default: explicit band > role > positional fallback > band 1
    analysisBands[s] = band > 0 ? band : 1;
    anyFallback = anyFallback || fallback;
    qualityBands[s] = prepared.collection.scenes().at( s ).qualityBand > 0
                          ? prepared.collection.scenes().at( s ).qualityBand
                          : globalQualityBand;
  }
  if ( anyFallback )
    context.logWarning( "Analysis band resolved by positional fallback for at least one scene; "
                        "pass 'band' or 'bands' to pin it." );

  const int width = reader.width();
  const int height = reader.height();
  const size_t tilePixels = static_cast<size_t>( tileSide ) * tileSide;
  const int tiles = reader.totalTileCount();

  std::vector<float> tile( tilePixels );
  std::vector<float> qualityTile;
  if ( method == QLatin1String( "best_pixel" ) )
    qualityTile.resize( tilePixels );

  Json::Value outputsList( Json::arrayValue );
  QString firstOutput;
  int groupDone = 0;
  double totalValidFraction = 0.0;

  for ( const auto &kv : groups )
  {
    // On failure mid-way, earlier period files stay valid (§49) — make that
    // visible in the error instead of implying nothing was produced.
    try
    {
    const QString key = kv.first;
    const std::vector<int> &indices = kv.second;
    const QString label = groupLabel( prepared.collection, indices, period );
    const QString groupPath = outputForPeriod( outputPath, key, label );

    // Default tie-break target: midpoint of the group's time range.
    AcquisitionTime groupTarget = targetTime;
    if ( !groupTarget.valid )
    {
      const auto &firstScene = prepared.collection.scenes().at( indices.front() );
      const auto &lastScene = prepared.collection.scenes().at( indices.back() );
      AcquisitionTime mid;
      mid.epochMillis = ( firstScene.time.epochMillis + lastScene.time.epochMillis ) / 2;
      mid.precision = temporal::TimePrecision::Date;
      mid.valid = true;
      mid.iso = QDateTime::fromMSecsSinceEpoch( mid.epochMillis, QTimeZone::utc() )
                    .toString( Qt::ISODate );
      groupTarget = mid;
    }

    // Tie-break reference: days-since-epoch of the target instant.
    const double targetDay = groupTarget.daysSince( prepared.collection.scenes().front().time );

    context.reportProgressForced( 0.05 + 0.9 * ( static_cast<double>( groupDone ) / groups.size() ),
                                  "Composite " + key.toStdString() );

    GdalDatasetWrapper out;
    QString outErr;
    temporal_output::TemporalOutputGuard guard;
    guard.manage( &out, groupPath );
    if ( !out.create( groupPath, width, height, 3, static_cast<int>( GDT_Float32 ),
                      reader.geoTransform(), reader.projection(), &outErr ) )
      throw RSOperatorError( ErrorCode::FileNotWritable,
                             "failed to create output: " + outErr.toStdString() );
    for ( int b = 1; b <= 3; ++b )
      out.setBandNoDataValue( b, std::numeric_limits<double>::quiet_NaN() );
    GDALSetDescription( GDALGetRasterBand( static_cast<GDALDatasetH>( out.dataset() ), 1 ),
                        "composite" );
    GDALSetDescription( GDALGetRasterBand( static_cast<GDALDatasetH>( out.dataset() ), 2 ),
                        "valid_count" );
    GDALSetDescription( GDALGetRasterBand( static_cast<GDALDatasetH>( out.dataset() ), 3 ),
                        "quality" );

    // Per-tile state, method-specific.
    std::vector<std::uint32_t> count( tilePixels );
    std::vector<float> countF( tilePixels );
    std::vector<float> outValue( tilePixels );
    std::vector<float> outQuality( tilePixels );
    // best_pixel extra state
    std::vector<double> bestAbsT( tilePixels );
    // mean state
    std::vector<double> mean( tilePixels ), m2( tilePixels );
    // median state
    std::vector<float> medianValues;
    if ( method == QLatin1String( "median" ) )
      medianValues.resize( tilePixels * static_cast<size_t>( indices.size() ) );

    std::uint64_t groupValid = 0;

    for ( int t = 0; t < tiles; ++t )
    {
      int x = 0, y = 0, w = 0, h = 0;
      reader.tileRect( t, &x, &y, &w, &h );
      const size_t pixels = static_cast<size_t>( w ) * h;
      std::fill( count.begin(), count.begin() + pixels, 0 );
      std::fill( outValue.begin(), outValue.begin() + pixels, kNan );
      std::fill( outQuality.begin(), outQuality.begin() + pixels, kNan );
      std::fill( bestAbsT.begin(), bestAbsT.begin() + pixels,
                 std::numeric_limits<double>::infinity() );
      std::fill( mean.begin(), mean.begin() + pixels, 0.0 );
      std::fill( m2.begin(), m2.begin() + pixels, 0.0 );

      for ( int gi = 0; gi < static_cast<int>( indices.size() ); ++gi )
      {
        const int s = indices[gi];
        if ( !reader.readSceneBandTile( s, analysisBands[s], t, tile.data() ) )
          throw RSOperatorError( ErrorCode::GdalError,
                                 "failed reading scene " +
                                     prepared.collection.scenes().at( s ).path.toStdString() );
        const bool hasQuality =
            method == QLatin1String( "best_pixel" ) && qualityBands[s] > 0;
        if ( hasQuality &&
             !reader.readSceneBandTile( s, qualityBands[s], t, qualityTile.data() ) )
          throw RSOperatorError( ErrorCode::GdalError,
                                 "failed reading quality band of " +
                                     prepared.collection.scenes().at( s ).path.toStdString() );
        const double tDays = reader.sceneDayOffset( s );

        for ( size_t i = 0; i < pixels; ++i )
        {
          const float v = tile[i];
          if ( !std::isfinite( v ) )
            continue;
          ++count[i];
          if ( method == QLatin1String( "best_pixel" ) )
          {
            // A missing/NaN quality sample must never outrank a measured
            // score: neutral 0.0 (real quality bands should use positive
            // scores; see schema note).
            float score = 0.0f;
            if ( hasQuality && std::isfinite( qualityTile[i] ) )
              score = qualityTile[i];
            const double dist = std::abs( tDays - targetDay );
            // Deterministic contract: strictly higher score wins; equal score
            // -> closer to the target date; both equal -> earlier scene stays.
            const bool better = !std::isfinite( outQuality[i] ) || score > outQuality[i] ||
                                ( score == outQuality[i] && dist < bestAbsT[i] );
            if ( better )
            {
              outValue[i] = v;
              outQuality[i] = score;
              bestAbsT[i] = dist;
            }
          }
          else if ( method == QLatin1String( "mean" ) )
          {
            const double c = count[i];
            const double delta = v - mean[i];
            mean[i] += delta / c;
            m2[i] += delta * ( v - mean[i] );
          }
          else // median
          {
            medianValues[i * indices.size() + ( count[i] - 1 )] = v;
          }
        }
        context.throwIfCancelled();
      }

      if ( method == QLatin1String( "mean" ) )
      {
        for ( size_t i = 0; i < pixels; ++i )
          if ( count[i] > 0 )
            outValue[i] = static_cast<float>( mean[i] );
      }
      else if ( method == QLatin1String( "median" ) )
      {
        for ( size_t i = 0; i < pixels; ++i )
        {
          const std::uint32_t cnt = count[i];
          if ( cnt == 0 )
            continue;
          float *begin = &medianValues[i * indices.size()];
          std::nth_element( begin, begin + ( cnt - 1 ) / 2, begin + cnt );
          if ( cnt % 2 == 1 )
            outValue[i] = begin[( cnt - 1 ) / 2];
          else
          {
            const float lower = *std::max_element( begin, begin + cnt / 2 );
            const float upper = *std::min_element( begin + cnt / 2, begin + cnt );
            outValue[i] = 0.5f * ( lower + upper );
          }
        }
      }

      for ( size_t i = 0; i < pixels; ++i )
        countF[i] = static_cast<float>( count[i] );
      if ( !out.writeBandWindow( 1, x, y, w, h, outValue.data() ) ||
           !out.writeBandWindow( 2, x, y, w, h, countF.data() ) ||
           !out.writeBandWindow( 3, x, y, w, h, outQuality.data() ) )
        throw RSOperatorError( ErrorCode::GdalError, "failed writing composite tile" );

      for ( size_t i = 0; i < pixels; ++i )
        groupValid += count[i];
    }

    temporal_output::writeTemporalDatasetMetadata(
      out, prepared.collection, "rs:temporal_composite",
      QStringLiteral( "method=%1 period=%2 scenes_in_group=%3 target=%4" )
          .arg( method, period )
          .arg( indices.size() )
          .arg( groupTarget.iso ) );
    QString closeErr;
    if ( !out.closeWithError( &closeErr ) )
      throw RSOperatorError( ErrorCode::FileNotWritable,
                             "output flush failed (disk full?): " + closeErr.toStdString() );
    guard.commit();

    Json::Value entry( Json::objectValue );
    entry["period"] = key.toStdString();
    entry["output"] = groupPath.toStdString();
    entry["sceneCount"] = static_cast<Json::Int>( indices.size() );
    if ( !label.isEmpty() )
      entry["label"] = label.toStdString();
    outputsList.append( entry );
    if ( firstOutput.isEmpty() )
      firstOutput = groupPath;
    const double groupSamples =
        static_cast<double>( width ) * height * static_cast<double>( indices.size() );
    totalValidFraction += groupSamples > 0 ? static_cast<double>( groupValid ) / groupSamples : 0.0;
    ++groupDone;
    }
    catch ( const RSOperatorError & )
    {
      if ( outputsList.size() > 0 )
        context.logWarning( std::to_string( outputsList.size() ) +
                            " period output(s) already committed before the failure" );
      throw;
    }
  }

  Json::Value result( Json::objectValue );
  result["output"] = firstOutput.toStdString();
  result["outputs"] = outputsList;
  result["method"] = method.toStdString();
  result["period"] = period.toStdString();
  result["periodCount"] = static_cast<Json::Int>( groups.size() );
  result["sceneCount"] = sceneCount;
  result["validFraction"] = totalValidFraction / groups.size();
  if ( !prepared.collection.timeRangeStartIso().isEmpty() )
  {
    result["timeStart"] = prepared.collection.timeRangeStartIso().toStdString();
    result["timeEnd"] = prepared.collection.timeRangeEndIso().toStdString();
  }
  Json::Value memory( Json::objectValue );
  memory["tileWidth"] = tileSide;
  memory["tileHeight"] = tileSide;
  memory["maxGroupSize"] = maxGroupSize;
  result["memory"] = memory;
  context.reportProgress( 1.0, "Temporal composite complete" );
  return result;
}

} // namespace sicnu::operators::rs
