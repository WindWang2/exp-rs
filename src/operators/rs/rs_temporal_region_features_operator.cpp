// src/operators/rs/rs_temporal_region_features_operator.cpp
#include "rs_temporal_region_features_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "operators/rs/rs_temporal_collection_input.h"
#include "processing/algorithms/temporal/temporal_change.h"
#include "processing/algorithms/temporal/temporal_fit.h"
#include "processing/algorithms/temporal/temporal_region_table.h"
#include "processing/algorithms/temporal/temporal_stats.h"
#include "processing/algorithms/temporal/temporal_stream.h"
#include "processing/framework/resource_estimation.h"

#include <QDateTime>
#include <QFile>
#include <QTextStream>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace sicnu::operators::rs
{

using namespace params;
using temporal::TemporalTileReader;

namespace
{
constexpr int kDefaultMaxRegions = 100000;
constexpr size_t kDefaultMedianBudgetFloats = 0; // features need no medians
constexpr size_t kMaxSeriesCells = 50ULL * 1000ULL * 1000ULL; // R×T cell guard
constexpr int kFeatureSchemaVersion = 1;
constexpr float kNanF = std::numeric_limits<float>::quiet_NaN();

struct CsvOutputGuard
{
  QString path;
  bool committed = false;
  ~CsvOutputGuard()
  {
    if ( !committed && !path.isEmpty() )
      QFile::remove( path );
  }
};

Json::Value loadRegionsFileJson( const std::string &path )
{
  QFile file( QString::fromStdString( path ) );
  if ( !file.open( QIODevice::ReadOnly ) )
    throw RSOperatorError( ErrorCode::FileNotFound,
                           "regions_file not readable: " + path );
  const QByteArray bytes = file.readAll();
  Json::CharReaderBuilder builder;
  std::string errs;
  Json::Value root;
  std::istringstream stream( bytes.toStdString() );
  if ( !Json::parseFromStream( builder, stream, &root, &errs ) )
    throw RSOperatorError( ErrorCode::InvalidParameter,
                           "regions_file is not valid JSON: " + errs );
  if ( root.isArray() )
    return root;
  if ( root.isObject() && root["regions"].isArray() )
    return root["regions"];
  throw RSOperatorError( ErrorCode::InvalidParameter,
                         "regions_file must hold a regions array (or {\"regions\": [...]})" );
}

/// Median of a finite-only double list (NaN when empty); copies input.
double finiteMedian( std::vector<double> values )
{
  std::vector<double> finite;
  finite.reserve( values.size() );
  for ( double v : values )
    if ( std::isfinite( v ) )
      finite.push_back( v );
  if ( finite.empty() )
    return std::numeric_limits<double>::quiet_NaN();
  std::sort( finite.begin(), finite.end() );
  const size_t n = finite.size();
  return n % 2 == 1 ? finite[n / 2] : 0.5 * ( finite[n / 2 - 1] + finite[n / 2] );
}
} // namespace

std::string RsTemporalRegionFeaturesOperator::description() const
{
  return "Compute a typed per-region temporal feature table for ML / agent "
         "consumption: series quality (valid_fraction), distribution "
         "(mean/stddev/min/max), robust trend (Sen slope + Mann-Kendall p, "
         "or OLS), anomalies (most recent z-score, max |z|), change (break "
         "count, first break day, max magnitude from the joint "
         "harmonic+trend segmentation; optional disturbance onset and "
         "recovery), and multi-cycle phenology (per-cycle median across "
         "years of SOS/POS/EOS/LOS/amplitude/integral). Output is one CSV "
         "row per region with a versioned JSON schema sidecar "
         "(exp_rs_temporal_region_features/1) so label tables join by "
         "region_id.";
}

Json::Value RsTemporalRegionFeaturesOperator::schema() const
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
                                         "...or workspace collection id / descriptor JSON path", "" );
  props["band"] = makeIntegerParam( "band", "Explicit analysis band (1-based)", 0 );
  props["band_role"] = makeEnumParam( "band_role", "Analysis band role",
                                      { "blue", "green", "red", "red_edge", "nir", "swir1",
                                        "swir2", "vv", "vh" },
                                      "" );
  props["duplicate_policy"] = makeEnumParam( "duplicate_policy", "Duplicate acquisition instants",
                                             { "keep_all", "reject" }, "keep_all" );
  props["apply_qa_masking"] = makeBooleanParam( "apply_qa_masking", "Exclude QA/cloud-masked samples", true );
  Json::Value regions = makeStringParam( "regions", "Regions array: [{\"id\", \"point\"|[x,y] or \"polygon\"|[[x,y]...]}]" );
  regions["type"] = "array";
  regions["items"] = Json::Value( Json::objectValue );
  props["regions"] = regions;
  props["regions_file"] = makeStringParam( "regions_file",
                                           "...or a JSON file holding the regions array", "" );
  Json::Value maxRegions = makeIntegerParam( "max_regions", "Maximum accepted regions (guard)", kDefaultMaxRegions );
  setRange( maxRegions, 1, 10000000 );
  props["max_regions"] = maxRegions;
  props["trend_method"] = makeEnumParam( "trend_method",
                                         "sen: robust median pairwise slope + MK test; "
                                         "ols: fast least-squares slope",
                                         { "sen", "ols" }, "sen" );
  props["change_harmonics"] = makeIntegerParam( "change_harmonics",
                                                "Harmonics for the joint change model (0 disables the change features)", 2 );
  props["direction"] = makeEnumParam( "direction",
                                      "Disturbance direction for onset/recovery features",
                                      { "none", "decrease", "increase" }, "none" );
  Json::Value cyclesParam = makeIntegerParam( "cycles", "Phenology cycles per year (1-2)", 1 );
  setRange( cyclesParam, 1, 2 );
  props["cycles"] = cyclesParam;
  Json::Value seasonStart = makeIntegerParam( "seasonStartDoy", "First cycle window start (day-of-year)", 1 );
  setRange( seasonStart, 1, 366 );
  props["seasonStartDoy"] = seasonStart;
  Json::Value seasonEnd = makeIntegerParam( "seasonEndDoy", "First cycle window end (day-of-year)", 366 );
  setRange( seasonEnd, 1, 366 );
  props["seasonEndDoy"] = seasonEnd;
  props["sidecar_path"] = makeStringParam( "sidecar_path",
                                           "Where to write the JSON schema sidecar (default: <output>.json)", "" );
  props["output"] = makeOutputParam( "output",
                                     "Region feature CSV (one row per region; missing = nan)", "csv" );

  Json::Value outputs( Json::objectValue );
  outputs["output"] = makeOutputParam( "output", "Region feature CSV", "csv" );
  outputs["sidecar"] = makeStringParam( "sidecar", "Schema sidecar path", "" );
  outputs["schema"] = makeStringParam( "schema", "Feature schema id", "" );
  outputs["regionCount"] = makeIntegerParam( "regionCount", "Regions in the table", 0 );
  outputs["featureCount"] = makeIntegerParam( "featureCount", "Feature columns after region_id", 0 );
  outputs["sceneCount"] = makeIntegerParam( "sceneCount", "Dates in the series", 0 );
  Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
  root["required"] = makeRequired( { "output" } );
  return root;
}

Json::Value RsTemporalRegionFeaturesOperator::metadata() const
{
  Json::Value meta( Json::objectValue );
  meta["group"] = group();
  meta["provider"] = "rs";
  Json::Value tags( Json::arrayValue );
  tags.append( "temporal" );
  tags.append( "features" );
  tags.append( "machine-learning" );
  tags.append( "phenology" );
  meta["tags"] = tags;
  meta["purpose"] = "Turn temporal collections into classification-ready region feature tables "
                    "(crop-type features, parcel monitoring, change labels) instead of "
                    "terminal-only JSON";
  meta["prerequisites"] = "Common grid, acquisition times, regions in the collection CRS; "
                          ">= 3 dates for trend features";
  meta["memoryPolicy"] = memoryPolicyName( memoryPolicy() );
  meta["deterministic"] = true;
  meta["supportsCancellation"] = true;
  meta["largeRasterSafe"] = true;
  meta["costClass"] = "O(scenes × totalRegionWindowPixels) I/O + O(regions × (scenes² + model))";
  meta["workflowHints"] = "Join the CSV by region_id with label tables (dataset foundry "
                          "samples); the sidecar's featureNames array pins the column order "
                          "for reproducible training";
  meta["limitations"] = "Phenology medians need >= 3 valid samples per year × cycle window; "
                        "Sen trend is O(dates²) per region (guarded by the series length); "
                        "change features need >= 4 dates and ~2 years for stable harmonics";
  return meta;
}

Json::Value RsTemporalRegionFeaturesOperator::executionEstimate() const
{
  return sicnu::processing::makeStreamingEstimate( kDefaultTileSize, kDefaultTileSize, 1, 4,
                                                   2 + kTypicalSceneEstimate, 0,
                                                   2 * 1024 * 1024 );
}

Json::Value RsTemporalRegionFeaturesOperator::estimateExecution( const Json::Value &params ) const
{
  const int tileSize = std::clamp( getInt( params, "tile_size", kDefaultTileSize ), 16, 4096 );
  int scenes = kTypicalSceneEstimate;
  if ( params.isMember( "scenes" ) && params["scenes"].isArray() )
    scenes = std::max<int>( 1, static_cast<int>( params["scenes"].size() ) );
  return sicnu::processing::makeStreamingEstimate( tileSize, tileSize, 1, 4,
                                                   2 + static_cast<std::uint64_t>( scenes ), 0,
                                                   2 * 1024 * 1024 );
}

Json::Value RsTemporalRegionFeaturesOperator::run( const Json::Value &params, RSOperatorContext &context )
{
  const std::string outputPath = requireString( params, "output" );
  const bool applyQaMasking = getBool( params, "apply_qa_masking", true );
  const QString bandRole = QString::fromStdString( getString( params, "band_role", "" ) );
  const int bandOverride = getInt( params, "band", 0 );
  const int maxRegions = std::clamp( getInt( params, "max_regions", kDefaultMaxRegions ),
                                     1, 10000000 );
  const QString trendToken = QString::fromStdString(
      getEnum( params, "trend_method", { "sen", "ols" }, "sen" ) );
  const bool useSen = trendToken == QLatin1String( "sen" );
  const int changeHarmonics = std::clamp( getInt( params, "change_harmonics", 2 ), 0, 3 );
  const QString directionToken = QString::fromStdString(
      getEnum( params, "direction", { "none", "decrease", "increase" }, "none" ) );
  const bool decrease = directionToken == QLatin1String( "decrease" );
  const bool increase = directionToken == QLatin1String( "increase" );
  const int cycles = std::clamp( getInt( params, "cycles", 1 ), 1, 2 );
  const int seasonStartDoy = std::clamp( getInt( params, "seasonStartDoy", 1 ), 1, 366 );
  const int seasonEndDoy = std::clamp( getInt( params, "seasonEndDoy", 366 ), 1, 366 );
  const QString sidecarParam = QString::fromStdString( getString( params, "sidecar_path", "" ) );

  // Regions: inline array or file (same contract as extract_regions).
  Json::Value regionsJson;
  if ( params.isMember( "regions" ) && params["regions"].isArray() )
  {
    if ( params.isMember( "regions_file" ) && params["regions_file"].isString() &&
         !std::string( params["regions_file"].asString() ).empty() )
      throw RSOperatorError( ErrorCode::InvalidParameter,
                             "provide either 'regions' or 'regions_file', not both" );
    regionsJson = params["regions"];
  }
  else if ( params.isMember( "regions_file" ) && params["regions_file"].isString() )
  {
    regionsJson = loadRegionsFileJson( params["regions_file"].asString() );
  }
  else
  {
    throw RSOperatorError( ErrorCode::MissingRequiredParameter,
                           "provide 'regions' (array) or 'regions_file' (JSON path)" );
  }
  QVector<temporal::RegionRef> regions;
  QString regionsError;
  if ( !temporal::parseRegionsJson( regionsJson, maxRegions, &regions, &regionsError ) )
    throw RSOperatorError( ErrorCode::InvalidParameter, regionsError.toStdString() );

  auto prepared = temporal_input::prepareTemporalRun( params, context, {}, bandRole, bandOverride );
  const int sceneCount = prepared.collection.sceneCount();
  if ( sceneCount < 3 )
    throw RSOperatorError( ErrorCode::InvalidInputData,
                           "region features need at least 3 dates (got " +
                               std::to_string( sceneCount ) + ")" );

  temporal::TemporalStreamOptions streamOptions;
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

  // Time axes: day offsets, doy, calendar year per scene date.
  std::vector<double> tDays( sceneCount );
  std::vector<int> doyOf( sceneCount, 0 );
  std::vector<int> yearOf( sceneCount, 0 );
  for ( int s = 0; s < sceneCount; ++s )
  {
    tDays[s] = reader.sceneDayOffset( s );
    const auto &scene = reader.scene( s );
    if ( scene.time.valid )
    {
      const QDate d = QDateTime::fromMSecsSinceEpoch( scene.time.epochMillis,
                                                      QTimeZone::utc() ).date();
      doyOf[s] = d.dayOfYear();
      yearOf[s] = d.year();
    }
  }

  // Region geometries + per-region window counts (same seam as
  // extract_regions).
  const int regionCount = regions.size();
  std::vector<temporal::RegionGeometry> geometries;
  geometries.reserve( static_cast<size_t>( regionCount ) );
  std::vector<size_t> insideCounts( static_cast<size_t>( regionCount ), 0 );
  for ( int r = 0; r < regionCount; ++r )
  {
    temporal::RegionGeometry geom;
    QString geomError;
    if ( !temporal::buildRegionGeometry( regions[r], r, reader.geoTransform(),
                                         reader.width(), reader.height(), &geom, &geomError ) )
      throw RSOperatorError( ErrorCode::InvalidInputData, geomError.toStdString() );
    insideCounts[static_cast<size_t>( r )] = geom.insideCount();
    geometries.push_back( std::move( geom ) );
  }

  // Region × date series guard (declared bound; refuse instead of swapping).
  const std::uint64_t seriesCells =
      static_cast<std::uint64_t>( regionCount ) * static_cast<std::uint64_t>( sceneCount );
  if ( seriesCells > kMaxSeriesCells )
    throw RSOperatorError(
        ErrorCode::InvalidParameter,
        "regions × dates = " + std::to_string( seriesCells ) +
            " exceeds the " + std::to_string( kMaxSeriesCells ) +
            "-cell feature guard; split the region set" );

  // Extract the region × date mean series (date-outermost, like
  // extract_regions).
  temporal::RegionDateReducer reducer( regionCount, kDefaultMedianBudgetFloats, insideCounts );
  std::vector<float> meanSeries( static_cast<size_t>( seriesCells ), kNanF );
  std::vector<float> window;
  window.reserve( 1024 );
  for ( int s = 0; s < sceneCount; ++s )
  {
    reducer.beginDate();
    for ( int r = 0; r < regionCount; ++r )
    {
      const auto &geom = geometries[static_cast<size_t>( r )];
      const int samples = static_cast<int>( geom.insideCount() );
      if ( static_cast<int>( window.size() ) < samples )
        window.resize( static_cast<size_t>( samples ) );
      if ( !reader.readSceneBandWindow( s, analysisBands[s], geom.xOff, geom.yOff,
                                        geom.w, geom.h, window.data() ) )
        throw RSOperatorError( ErrorCode::GdalError,
                               "failed reading scene " +
                                   prepared.collection.scenes().at( s ).path.toStdString() +
                                   " for region '" + regions[r].id.toStdString() + "'" );
      if ( geom.isPoint )
        reducer.addSample( r, window[0] );
      else
        for ( const int off : geom.insideOffsets )
          reducer.addSample( r, window[static_cast<size_t>( off )] );
      if ( ( r & 1023 ) == 1023 )
        context.throwIfCancelled();
    }
    reducer.endDate();
    for ( int r = 0; r < regionCount; ++r )
    {
      const auto &st = reducer.stats( r );
      if ( st.validCount > 0 )
        meanSeries[static_cast<uint64_t>( r ) * sceneCount + s] = static_cast<float>( st.mean );
    }
    context.throwIfCancelled();
    context.reportProgress( 0.05 + 0.55 * ( static_cast<double>( s + 1 ) / sceneCount ),
                            "Region series " + std::to_string( s + 1 ) + "/" +
                                std::to_string( sceneCount ) );
  }

  // Feature schema (fixed order; the sidecar pins it).
  std::vector<QString> featureNames;
  auto addFeature = [&]( const QString &name ) { featureNames.push_back( name ); };
  addFeature( "valid_fraction" );
  addFeature( "mean" );
  addFeature( "stddev" );
  addFeature( "min" );
  addFeature( "max" );
  addFeature( "trend_slope_per_day" );
  addFeature( "trend_p_value" );
  addFeature( "recent_z" );
  addFeature( "max_abs_z" );
  if ( changeHarmonics > 0 )
  {
    addFeature( "break_count" );
    addFeature( "first_break_day" );
    addFeature( "max_break_magnitude" );
    if ( decrease || increase )
    {
      addFeature( "onset_day" );
      addFeature( "recovery_days" );
    }
  }
  for ( int c = 1; c <= cycles; ++c )
  {
    addFeature( QStringLiteral( "c%1_sos_median" ).arg( c ) );
    addFeature( QStringLiteral( "c%1_pos_median" ).arg( c ) );
    addFeature( QStringLiteral( "c%1_eos_median" ).arg( c ) );
    addFeature( QStringLiteral( "c%1_los_median" ).arg( c ) );
    addFeature( QStringLiteral( "c%1_amplitude_median" ).arg( c ) );
    addFeature( QStringLiteral( "c%1_integral_median" ).arg( c ) );
  }
  addFeature( "years_covered" );
  const int featureCount = static_cast<int>( featureNames.size() );

  // Phenology windows.
  std::vector<temporal::SeasonWindow> windows{ { seasonStartDoy, seasonEndDoy } };
  if ( cycles == 2 )
    windows.push_back( temporal::complementSeasonWindow( windows.front() ) );

  CsvOutputGuard csvGuard;
  csvGuard.path = QString::fromStdString( outputPath );
  QFile outFile( csvGuard.path );
  if ( !outFile.open( QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text ) )
    throw RSOperatorError( ErrorCode::FileNotWritable,
                           "cannot open output CSV: " + outputPath );
  QTextStream ts( &outFile );
  ts.setCodec( "UTF-8" );
  ts << "region_id";
  for ( const auto &name : featureNames )
    ts << ',' << name;
  ts << '\n';

  auto writeValue = [&]( double v ) {
    ts << ',' << ( std::isfinite( v ) ? QString::number( v, 'g', 9 ) : QStringLiteral( "nan" ) );
  };

  for ( int r = 0; r < regionCount; ++r )
  {
    // Gather the region's mean series.
    std::vector<float> series( static_cast<size_t>( sceneCount ) );
    for ( int s = 0; s < sceneCount; ++s )
      series[static_cast<size_t>( s )] = meanSeries[static_cast<uint64_t>( r ) * sceneCount + s];

    std::vector<double> row( static_cast<size_t>( featureCount ),
                             std::numeric_limits<double>::quiet_NaN() );
    int fi = 0;

    // Quality + distribution.
    int validCount = 0;
    double sum = 0.0;
    double minV = std::numeric_limits<double>::infinity();
    double maxV = -std::numeric_limits<double>::infinity();
    for ( float v : series )
    {
      if ( !std::isfinite( v ) )
        continue;
      ++validCount;
      sum += v;
      minV = std::min( minV, static_cast<double>( v ) );
      maxV = std::max( maxV, static_cast<double>( v ) );
    }
    row[fi++] = static_cast<double>( validCount ) / static_cast<double>( sceneCount );
    if ( validCount > 0 )
    {
      const double meanV = sum / validCount;
      double m2 = 0.0;
      for ( float v : series )
        if ( std::isfinite( v ) )
          m2 += ( v - meanV ) * ( v - meanV );
      row[fi++] = meanV;
      row[fi++] = std::sqrt( m2 / validCount );
      row[fi++] = minV;
      row[fi++] = maxV;
    }
    else
    {
      fi += 4;
    }

    // Trend (real day offsets; NaN-guarded by the kernels' contracts).
    if ( validCount >= 3 )
    {
      if ( useSen )
      {
        const temporal::SenTrendResult sen = temporal::mannKendallSenSlope( series, tDays );
        row[fi++] = sen.slope;
        row[fi++] = sen.pValue;
      }
      else
      {
        temporal::stats::OnlineRegression reg;
        for ( int s = 0; s < sceneCount; ++s )
          if ( std::isfinite( series[static_cast<size_t>( s )] ) )
            reg.add( tDays[static_cast<size_t>( s )], series[static_cast<size_t>( s )] );
        row[fi++] = reg.slope();
        row[fi++] = std::numeric_limits<double>::quiet_NaN(); // OLS has no MK p
      }
    }
    else
    {
      fi += 2;
    }

    // Anomaly: last valid z against the full-series baseline (sample stddev);
    // max |z| over the series.
    if ( validCount >= 2 )
    {
      double meanV = 0.0;
      for ( float v : series )
        if ( std::isfinite( v ) )
          meanV += v;
      meanV /= validCount;
      double m2 = 0.0;
      for ( float v : series )
        if ( std::isfinite( v ) )
          m2 += ( v - meanV ) * ( v - meanV );
      const double sigma = std::sqrt( m2 / ( validCount - 1 ) );
      double recentZ = std::numeric_limits<double>::quiet_NaN();
      for ( int s = sceneCount - 1; s >= 0; --s )
      {
        if ( std::isfinite( series[static_cast<size_t>( s )] ) )
        {
          recentZ = sigma > 0.0
                      ? ( series[static_cast<size_t>( s )] - meanV ) / sigma
                      : std::numeric_limits<double>::quiet_NaN();
          break;
        }
      }
      double maxAbsZ = 0.0;
      if ( sigma > 0.0 )
      {
        for ( float v : series )
          if ( std::isfinite( v ) )
            maxAbsZ = std::max( maxAbsZ, std::abs( ( v - meanV ) / sigma ) );
      }
      else
      {
        maxAbsZ = std::numeric_limits<double>::quiet_NaN();
      }
      row[fi++] = recentZ;
      row[fi++] = maxAbsZ;
    }
    else
    {
      fi += 2;
    }

    // Change features (joint harmonic+trend segmentation on the mean series).
    if ( changeHarmonics > 0 )
    {
      if ( validCount >= 4 )
      {
        const temporal::SeasonalTrendBreaksResult fit = temporal::fitSeasonalTrendBreaks(
            series, tDays, changeHarmonics, 2, 3, 0.1, false );
        row[fi++] = static_cast<double>( fit.breaks.size() );
        row[fi++] = fit.breaks.empty()
                      ? std::numeric_limits<double>::quiet_NaN()
                      : fit.breaks.front().breakDays;
        double maxMag = std::numeric_limits<double>::quiet_NaN();
        for ( const auto &ev : fit.breaks )
          if ( std::isfinite( ev.magnitude ) )
            maxMag = std::isfinite( maxMag ) ? std::max( maxMag, ev.magnitude )
                                             : ev.magnitude;
        row[fi++] = maxMag;
        if ( decrease || increase )
        {
          double recoveryDays = 0.0;
          const double onset = temporal::disturbanceOnset(
              fit.fitted, tDays, fit.breaks, decrease, 0.0, &recoveryDays, 0.0 );
          row[fi++] = onset >= 0.0 ? onset
                                   : std::numeric_limits<double>::quiet_NaN();
          row[fi++] = std::isfinite( recoveryDays )
                        ? recoveryDays
                        : std::numeric_limits<double>::quiet_NaN();
        }
      }
      else
      {
        fi += ( decrease || increase ) ? 5 : 3;
      }
    }

    // Phenology: per-year per-cycle metrics, median across years.
    {
      const std::vector<temporal::SeasonYearMetrics> seasonMetrics =
          temporal::phenologyCyclesPerYear( series, tDays, doyOf, yearOf, windows, 0.2 );
      for ( int c = 1; c <= cycles; ++c )
      {
        std::vector<double> sos, pos, eos, los, amp, integral;
        for ( const auto &entry : seasonMetrics )
        {
          if ( entry.cycleIndex != c - 1 || !entry.metrics.valid )
            continue;
          sos.push_back( entry.metrics.sos );
          pos.push_back( entry.metrics.pos );
          eos.push_back( entry.metrics.eos );
          los.push_back( entry.metrics.los );
          amp.push_back( entry.metrics.amplitude );
          integral.push_back( entry.metrics.integral );
        }
        row[fi++] = finiteMedian( std::move( sos ) );
        row[fi++] = finiteMedian( std::move( pos ) );
        row[fi++] = finiteMedian( std::move( eos ) );
        row[fi++] = finiteMedian( std::move( los ) );
        row[fi++] = finiteMedian( std::move( amp ) );
        row[fi++] = finiteMedian( std::move( integral ) );
      }
      int yearsCovered = 0;
      int lastYear = std::numeric_limits<int>::min();
      for ( const auto &entry : seasonMetrics )
      {
        if ( entry.year != lastYear )
        {
          ++yearsCovered;
          lastYear = entry.year;
        }
      }
      row[fi++] = static_cast<double>( yearsCovered );
    }

    ts << regions[r].id;
    for ( const double v : row )
      writeValue( v );
    ts << '\n';
    if ( ( r & 1023 ) == 1023 )
    {
      context.throwIfCancelled();
      context.reportProgress( 0.60 + 0.38 * ( static_cast<double>( r ) / regionCount ),
                              "Region features " + std::to_string( r ) + "/" +
                                  std::to_string( regionCount ) );
    }
  }
  ts.flush();
  if ( ts.status() != QTextStream::Ok )
    throw RSOperatorError( ErrorCode::FileNotWritable, "CSV write failed (disk full?)" );
  outFile.close();
  csvGuard.committed = true;

  // Schema sidecar.
  const QString sidecarPath = sidecarParam.isEmpty()
                                ? QString::fromStdString( outputPath ) + ".json"
                                : sidecarParam;
  Json::Value sidecar( Json::objectValue );
  sidecar["schema"] = "exp_rs_temporal_region_features/" +
                      std::to_string( kFeatureSchemaVersion );
  sidecar["regionIdColumn"] = "region_id";
  sidecar["missingToken"] = "nan";
  Json::Value namesJson( Json::arrayValue );
  for ( const auto &name : featureNames )
    namesJson.append( name.toStdString() );
  sidecar["featureNames"] = namesJson;
  sidecar["regionCount"] = regionCount;
  sidecar["sceneCount"] = sceneCount;
  sidecar["timeStart"] = prepared.collection.timeRangeStartIso().toStdString();
  sidecar["timeEnd"] = prepared.collection.timeRangeEndIso().toStdString();
  Json::Value provenance( Json::objectValue );
  provenance["operator"] = "rs:temporal_region_features";
  provenance["trendMethod"] = trendToken.toStdString();
  provenance["changeHarmonics"] = changeHarmonics;
  provenance["direction"] = directionToken.toStdString();
  provenance["cycles"] = cycles;
  Json::Value window1( Json::arrayValue );
  window1.append( seasonStartDoy );
  window1.append( seasonEndDoy );
  provenance["seasonWindowDoy"] = window1;
  sidecar["provenance"] = provenance;
  {
    QFile sidecarFile( sidecarPath );
    if ( !sidecarFile.open( QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text ) )
      throw RSOperatorError( ErrorCode::FileNotWritable,
                             "cannot open sidecar: " + sidecarPath.toStdString() );
    const Json::Value written = sidecar;
    sidecarFile.write( Json::writeString( Json::StreamWriterBuilder(), written ).c_str() );
    sidecarFile.close();
  }

  Json::Value result( Json::objectValue );
  result["output"] = outputPath;
  result["sidecar"] = sidecarPath.toStdString();
  result["schema"] = sidecar["schema"];
  result["regionCount"] = regionCount;
  result["featureCount"] = featureCount;
  result["sceneCount"] = sceneCount;
  context.reportProgress( 1.0, "Temporal region features complete" );
  return result;
}

} // namespace sicnu::operators::rs
