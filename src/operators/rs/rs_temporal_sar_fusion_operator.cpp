// src/operators/rs/rs_temporal_sar_fusion_operator.cpp
#include "rs_temporal_sar_fusion_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "operators/rs/rs_temporal_output.h"
#include "processing/algorithms/temporal/temporal_fusion.h"
#include "processing/framework/resource_estimation.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <gdal.h>

#include <QString>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace sicnu::operators::rs
{

using namespace params;

namespace
{
constexpr int kDefaultTileSize = 256;
constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
} // namespace

std::string RsTemporalSarFusionOperator::description() const
{
  return "Feature-level fusion of already-coregistered optical and SAR "
         "temporal feature rasters. Both inputs must share the SAME pixel "
         "grid (width, height, geotransform, CRS); the operator verifies the "
         "grid signature and concatenates bands as opt_<name> / sar_<name> "
         "with fusion provenance metadata. Registration, resampling and "
         "reprojection are NOT performed — a grid mismatch is a typed error, "
         "keeping 'coregister' and 'fuse' as separate, explicit steps.";
}

Json::Value RsTemporalSarFusionOperator::schema() const
{
  using namespace schema;
  Json::Value props( Json::objectValue );
  props["optical"] = makeRasterParam(
      "optical", "Optical temporal feature raster (e.g. rs:temporal_phenology "
                 "or rs:temporal_sen_trend output) — already coregistered to "
                 "the SAR grid" );
  props["sar"] = makeRasterParam(
      "sar", "SAR temporal feature raster (e.g. rs:sar_temporal_stats output) "
             "on the same pixel grid" );
  // Machine-readable contracts: both inputs MUST sit on the same pixel grid
  // (derives capability crs.requires_shared_grid / the rs:align fixer edge)
  // and carry distinct modalities for the optical+SAR family record.
  props["optical"]["x-rs-contract"]["dataKind"] = "raster";
  props["optical"]["x-rs-contract"]["gridRelation"] = "same-grid";
  props["optical"]["x-rs-contract"]["modality"] = "optical";
  props["sar"]["x-rs-contract"]["dataKind"] = "raster";
  props["sar"]["x-rs-contract"]["gridRelation"] = "same-grid";
  props["sar"]["x-rs-contract"]["modality"] = "sar";
  props["output"] = makeOutputParam( "output", "Fused feature stack GeoTIFF", "tif" );
  props["tile_size"] = makeIntegerParam( "tile_size", "Streaming tile size (pixels)",
                                         kDefaultTileSize );
  Json::Value gtolParam = makeNumberParam(
      "grid_tolerance",
      "Absolute per-coefficient geotransform tolerance for the shared-grid "
      "check (default 1e-9 — effectively exact equality)",
      1e-9 );
  setRange( gtolParam, 0.0, 1.0 );
  props["grid_tolerance"] = gtolParam;

  Json::Value outputs( Json::objectValue );
  outputs["output"] = makeOutputParam( "output", "Fused feature stack GeoTIFF", "tif" );
  outputs["bands"] = makeIntegerParam( "bands", "Output band count (optical + SAR)", 0 );
  outputs["opticalBands"] = makeIntegerParam( "opticalBands", "Bands taken from the optical input", 0 );
  outputs["sarBands"] = makeIntegerParam( "sarBands", "Bands taken from the SAR input", 0 );
  Json::Value memory = makeStringParam( "memory", "Streaming working-set summary (tile size and estimated bytes)" );
  memory["type"] = "object";
  outputs["memory"] = memory;
  Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
  root["required"] = makeRequired( { "optical", "sar", "output" } );
  return root;
}

Json::Value RsTemporalSarFusionOperator::metadata() const
{
  Json::Value meta( Json::objectValue );
  meta["group"] = group();
  meta["provider"] = "rs";
  Json::Value tags( Json::arrayValue );
  tags.append( "temporal" );
  tags.append( "sar" );
  tags.append( "fusion" );
  meta["tags"] = tags;
  meta["purpose"] = "Attach SAR temporal statistics/phenology-adjacent features "
                    "to an optical temporal feature stack on a verified shared "
                    "grid — the interface contract between the SAR feature "
                    "track and joint optical+SAR analysis";
  meta["prerequisites"] = "Both inputs ALREADY coregistered: identical width, "
                          "height, geotransform (within grid_tolerance) and CRS. "
                          "Run rs:sar_coregister or equivalent first — this "
                          "operator refuses mismatched grids instead of "
                          "realigning them";
  meta["memoryPolicy"] = memoryPolicyName( memoryPolicy() );
  meta["deterministic"] = true;
  meta["supportsCancellation"] = true;
  meta["largeRasterSafe"] = true;
  meta["costClass"] = "O(tile) streaming copy per band";
  meta["workflowHints"] = "Fuse rs:temporal_phenology metrics with "
                          "rs:sar_temporal_stats outputs to hand a joint "
                          "optical+SAR feature stack to downstream classifiers";
  meta["limitations"] = "Feature concatenation only — no band harmonisation, "
                        "weighting or dimensionality reduction; CRS equality "
                        "is textual (equivalent WKT dialects refuse fusion)";
  return meta;
}

Json::Value RsTemporalSarFusionOperator::executionEstimate() const
{
  // One read window + one write window per band pair.
  return sicnu::processing::makeStreamingEstimate( kDefaultTileSize, kDefaultTileSize, 1, 4, 2, 0,
                                                   2 * 1024 * 1024 );
}

Json::Value RsTemporalSarFusionOperator::estimateExecution( const Json::Value &params ) const
{
  const int tileSize = std::clamp( getInt( params, "tile_size", kDefaultTileSize ), 16, 4096 );
  return sicnu::processing::makeStreamingEstimate( tileSize, tileSize, 1, 4, 2, 0,
                                                   2 * 1024 * 1024 );
}

Json::Value RsTemporalSarFusionOperator::run( const Json::Value &params, RSOperatorContext &context )
{
  const std::string opticalPath = requireString( params, "optical" );
  const std::string sarPath = requireString( params, "sar" );
  const std::string outputPath = requireString( params, "output" );
  const int tileSize = std::clamp( getInt( params, "tile_size", kDefaultTileSize ), 16, 4096 );
  const double gtol = std::clamp( getDouble( params, "grid_tolerance", 1e-9 ), 0.0, 1.0 );

  context.reportProgress( 0.02, "Opening fusion inputs" );
  GdalDatasetWrapper optical;
  GdalDatasetWrapper sar;
  if ( !optical.open( QString::fromStdString( opticalPath ) ) )
    throw RSOperatorError( ErrorCode::FileNotFound,
                           "cannot open optical input: " + opticalPath );
  if ( !sar.open( QString::fromStdString( sarPath ) ) )
    throw RSOperatorError( ErrorCode::FileNotFound,
                           "cannot open SAR input: " + sarPath );

  // Shared-grid contract: identical dimensions, geotransform (±tol) and CRS.
  temporal::GridSignature optGrid;
  optGrid.width = optical.width();
  optGrid.height = optical.height();
  optGrid.geoTransform = optical.geoTransform();
  optGrid.projection = optical.projection().toStdString();
  temporal::GridSignature sarGrid;
  sarGrid.width = sar.width();
  sarGrid.height = sar.height();
  sarGrid.geoTransform = sar.geoTransform();
  sarGrid.projection = sar.projection().toStdString();

  const temporal::GridCompatibility compat =
    temporal::checkGridCompatibility( optGrid, sarGrid, gtol );
  if ( !compat.compatible )
  {
    std::string why;
    for ( const std::string &m : compat.mismatches )
      why += ( why.empty() ? "" : ", " ) + m;
    throw RSOperatorError(
        ErrorCode::InvalidInputData,
        "optical and SAR inputs do not share one pixel grid (" + why +
            "); coregister them first — this operator never realigns" );
  }

  const int optBands = optical.bandCount();
  const int sarBands = sar.bandCount();
  const int bandCount = optBands + sarBands;
  const int width = optical.width();
  const int height = optical.height();
  if ( bandCount <= 0 )
    throw RSOperatorError( ErrorCode::InvalidInputData,
                           "fusion needs at least one input band" );

  context.reportProgress( 0.05, "Creating fused output" );
  GdalDatasetWrapper out;
  QString outErr;
  temporal_output::TemporalOutputGuard guard;
  guard.manage( &out, QString::fromStdString( outputPath ) );
  if ( !out.create( QString::fromStdString( outputPath ), width, height, bandCount,
                    static_cast<int>( GDT_Float32 ), optical.geoTransform(),
                    optical.projection(), &outErr ) )
    throw RSOperatorError( ErrorCode::FileNotWritable,
                           "failed to create output: " + outErr.toStdString() );

  // Band descriptions: opt_<source name or bN> then sar_<...>.
  auto describeBands = []( GdalDatasetWrapper &src, GdalDatasetWrapper &dst,
                           int firstOutBand, const QString &prefix ) {
    for ( int s = 1; s <= src.bandCount(); ++s )
    {
      GDALRasterBandH sb = GDALGetRasterBand( static_cast<GDALDatasetH>( src.dataset() ), s );
      QString base = QString::fromUtf8( GDALGetDescription( sb ) );
      if ( base.isEmpty() )
        base = QStringLiteral( "b%1" ).arg( s );
      const int ob = firstOutBand + s - 1;
      dst.setBandNoDataValue( ob, std::numeric_limits<double>::quiet_NaN() );
      GDALSetDescription( GDALGetRasterBand( static_cast<GDALDatasetH>( dst.dataset() ), ob ),
                          ( prefix + base ).toUtf8().constData() );
    }
  };
  describeBands( optical, out, 1, QStringLiteral( "opt_" ) );
  describeBands( sar, out, 1 + optBands, QStringLiteral( "sar_" ) );

  // Bounded working set: one tile row-block per direction per band.
  const size_t tilePixels =
      static_cast<size_t>( std::min( tileSize, width ) ) *
      static_cast<size_t>( std::min( tileSize, height ) );
  std::vector<float> tile( tilePixels );

  const int tilesX = ( width + tileSize - 1 ) / tileSize;
  const int tilesY = ( height + tileSize - 1 ) / tileSize;
  const int totalSteps = bandCount * tilesX * tilesY;
  int stepsDone = 0;

  auto copyBand = [&]( GdalDatasetWrapper &src, int srcBand, int dstBand ) {
    // The output declares NaN NoData — remap the SOURCE band's sentinel
    // (e.g. -9999) so foreign NoData never lands as valid fused data.
    bool hasSrcNodata = false;
    const double srcNodata = src.bandNoDataValue( srcBand, &hasSrcNodata );
    const bool remapSentinel = hasSrcNodata && std::isfinite( srcNodata );
    for ( int ty = 0; ty < tilesY; ++ty )
      for ( int tx = 0; tx < tilesX; ++tx )
      {
        context.throwIfCancelled();
        const int x = tx * tileSize;
        const int y = ty * tileSize;
        const int w = std::min( tileSize, width - x );
        const int h = std::min( tileSize, height - y );
        if ( !src.readBandWindow( srcBand, x, y, w, h, tile.data() ) )
          throw RSOperatorError( ErrorCode::GdalError,
                                 "failed reading fusion input band" );
        if ( remapSentinel )
        {
          const size_t n = static_cast<size_t>( w ) * static_cast<size_t>( h );
          for ( size_t i = 0; i < n; ++i )
            if ( tile[i] == static_cast<float>( srcNodata ) )
              tile[i] = kNan;
        }
        if ( !out.writeBandWindow( dstBand, x, y, w, h, tile.data() ) )
          throw RSOperatorError( ErrorCode::GdalError,
                                 "failed writing fused band" );
        ++stepsDone;
      }
    context.reportProgress( 0.05 + 0.90 * ( static_cast<double>( stepsDone ) / totalSteps ),
                            "Fusing band " + std::to_string( dstBand ) + "/" +
                                std::to_string( bandCount ) );
  };

  for ( int s = 1; s <= optBands; ++s )
    copyBand( optical, s, s );
  for ( int s = 1; s <= sarBands; ++s )
    copyBand( sar, s, optBands + s );

  // Provenance: which inputs, which band mapping, which grid contract.
  temporal::FusionProvenance prov;
  prov.opticalPath = opticalPath;
  prov.sarPath = sarPath;
  prov.opticalBandCount = optBands;
  prov.sarBandCount = sarBands;
  prov.geotransformTolerance = gtol;
  for ( const auto &item : temporal::fusionProvenanceItems( prov ) )
    GDALSetMetadataItem( static_cast<GDALDatasetH>( out.dataset() ),
                         item.first.c_str(), item.second.c_str(), nullptr );

  QString closeErr;
  if ( !out.closeWithError( &closeErr ) )
    throw RSOperatorError( ErrorCode::FileNotWritable,
                           "output flush failed (disk full?): " + closeErr.toStdString() );
  guard.commit();

  Json::Value result( Json::objectValue );
  result["output"] = outputPath;
  result["bands"] = bandCount;
  result["opticalBands"] = optBands;
  result["sarBands"] = sarBands;
  Json::Value memory( Json::objectValue );
  memory["tileWidth"] = tileSize;
  memory["tileHeight"] = tileSize;
  memory["workingSetEstimateBytes"] = Json::Value::UInt64(
      static_cast<uint64_t>( tilePixels * sizeof( float ) ) );
  result["memory"] = memory;
  context.reportProgress( 1.0, "Temporal optical+SAR fusion complete" );
  return result;
}

} // namespace sicnu::operators::rs
