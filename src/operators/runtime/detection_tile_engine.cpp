// src/operators/runtime/detection_tile_engine.cpp
#include "operators/runtime/detection_tile_engine.h"

#include "operators/framework/rs_operator_error.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <gdal.h>
#include <ogr_api.h>
#include <ogr_srs_api.h>

#include <opencv2/imgproc.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace sicnu::operators::runtime {

namespace {

constexpr int kMinTileSize = 16;
constexpr int kDefaultTileSize = 512;

struct CoreTile
{
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
};

std::string vectorDriverName( const std::string &path )
{
  const QString suffix = QFileInfo( QString::fromStdString( path ) ).suffix().toLower();
  if ( suffix == QLatin1String( "gpkg" ) )
    return "GPKG";
  if ( suffix == QLatin1String( "geojson" ) )
    return "GeoJSON";
  return "ESRI Shapefile"; // .shp and the default
}

/// Removes a vector output including shapefile sidecars.
void removeVectorFiles( const QString &mainPath )
{
  QFileInfo fi( mainPath );
  QFile::remove( mainPath );
  if ( fi.suffix().toLower() == QLatin1String( "shp" ) )
  {
    const QString base = fi.path() + QLatin1Char( '/' ) + fi.completeBaseName();
    QFile::remove( base + QStringLiteral( ".dbf" ) );
    QFile::remove( base + QStringLiteral( ".shx" ) );
    QFile::remove( base + QStringLiteral( ".prj" ) );
    QFile::remove( base + QStringLiteral( ".cpg" ) );
  }
}

} // namespace

DetectionTileEngine::DetectionTileEngine( ModelInfo model, ModelRuntimePtr runtime )
    : m_model( std::move( model ) ), m_runtime( std::move( runtime ) )
{
}

std::string DetectionTileEngine::checkContract( const ModelInfo &model )
{
  if ( !model.output.detectionDeclared )
    return "model '" + model.name + "' has no output.detection contract — it cannot run as detection";
  if ( const std::string detectionError = model.output.detection.validate(); !detectionError.empty() )
    return detectionError;
  if ( model.preprocess.resize != "to_input" || model.input.width <= 0 || model.input.height <= 0 )
    return "detection models must declare preprocess.resize=to_input with input.width/height "
           "(the head decodes boxes in the fixed input frame)";
  return {};
}

DetectionTileStats DetectionTileEngine::run( const std::string &inputPath,
                                             const std::vector<int> &bands,
                                             const std::string &outputPath,
                                             RSOperatorContext &context,
                                             const TileInferenceRunOptions &options )
{
  if ( !m_runtime )
    throw RSOperatorError( ErrorCode::ComputationError, "detection engine has no runtime session" );
  if ( const std::string contractError = checkContract( m_model ); !contractError.empty() )
    throw RSOperatorError( ErrorCode::InvalidInputData, contractError );
  const ModelDetectionContract &det = m_model.output.detection;

  GdalDatasetWrapper ds;
  if ( !ds.open( QString::fromStdString( inputPath ) ) )
    throw RSOperatorError( ErrorCode::GdalError, "failed to open input raster: " + inputPath );
  const int rasterW = ds.width();
  const int rasterH = ds.height();
  const int rasterBands = ds.bandCount();
  if ( rasterW <= 0 || rasterH <= 0 || rasterBands <= 0 )
    throw RSOperatorError( ErrorCode::InvalidInputData, "input raster is empty: " + inputPath );

  std::vector<int> bandList = bands;
  if ( bandList.empty() )
  {
    bandList.resize( rasterBands );
    for ( int i = 0; i < rasterBands; ++i )
      bandList[static_cast<std::size_t>( i )] = i + 1;
  }
  else
  {
    for ( int b : bandList )
    {
      if ( b < 1 || b > rasterBands )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "band " + std::to_string( b ) + " out of range (1.."
                                 + std::to_string( rasterBands ) + ")" );
    }
  }
  const int bandCount = static_cast<int>( bandList.size() );
  if ( const std::string dtypeError =
         TileInferenceEngine::inputDTypeMismatch( m_model, bandList,
                                                  [ &ds ]( int band ) { return ds.bandDataType( band ); } );
       !dtypeError.empty() )
    throw RSOperatorError( ErrorCode::InvalidInputData, dtypeError );
  if ( !m_model.input.bandRoles.empty()
       && m_model.input.bandRoles.size() != static_cast<std::size_t>( bandCount ) )
    throw RSOperatorError( ErrorCode::InvalidParameter,
                           "model manifest declares " + std::to_string( m_model.input.bandRoles.size() )
                             + " band roles but " + std::to_string( bandCount ) + " bands are fed" );

  const ModelPreprocessContract &pre = m_model.preprocess;
  const bool meanStd = pre.normalize == "mean_std";
  if ( meanStd && !pre.mean.empty() && pre.mean.size() != static_cast<std::size_t>( bandCount ) )
    throw RSOperatorError( ErrorCode::InvalidParameter, "preprocess.mean channel count mismatch" );
  if ( meanStd && !pre.stdv.empty() && pre.stdv.size() != static_cast<std::size_t>( bandCount ) )
    throw RSOperatorError( ErrorCode::InvalidParameter, "preprocess.std channel count mismatch" );

  const int tileSize = std::min( TileInferenceEngine::effectiveTileSize( m_model ),
                                 std::max( rasterW, rasterH ) );
  // Detection tiles carry CONTEXT (halo) so objects at tile seams are seen
  // whole; overlap duplicates are resolved by the center-in-core rule below.
  const int halo = std::max( 0, TileInferenceEngine::effectiveHalo( m_model ) );
  int batchSize = TileInferenceEngine::effectiveBatchSize(
    m_model, ModelRuntimeRegistry::instance().hardware(), tileSize, bandCount );
  if ( options.batchSizeOverride > 0 )
    batchSize = std::min( batchSize, options.batchSizeOverride );
  batchSize = std::max( 1, batchSize );

  const int modelW = m_model.input.width;
  const int modelH = m_model.input.height;
  const int interp = pre.interpolation == "nearest" ? cv::INTER_NEAREST : cv::INTER_LINEAR;

  std::vector<CoreTile> core;
  for ( int y = 0; y < rasterH; y += tileSize )
    for ( int x = 0; x < rasterW; x += tileSize )
      core.push_back( CoreTile{ x, y, std::min( tileSize, rasterW - x ),
                                std::min( tileSize, rasterH - y ) } );

  DetectionTileStats stats;
  stats.tileSize = tileSize;
  stats.contextHalo = halo;
  stats.batchSize = batchSize;
  stats.tilesPlanned = static_cast<int>( core.size() );
  stats.rasterWidth = rasterW;
  stats.rasterHeight = rasterH;

  std::vector<DetectionBox> detections;
  const std::array<double, 6> geoTransform = ds.geoTransform();

  const int maxWin = tileSize + 2 * halo;
  std::vector<float> windowBuffer( static_cast<std::size_t>( maxWin ) * maxWin * bandCount );
  std::vector<cv::Mat> batchMats;
  std::vector<std::array<int, 4>> batchWindows; // winX, winY, winW, winH per pending tile
  std::vector<CoreTile> batchCores;

  context.reportProgressForced( 0.0, "Tiled detection: " + std::to_string( stats.tilesPlanned ) + " tiles" );

  // Flush pending tiles through one (or more, on OOM splits) forward passes.
  auto flushBatch = [ & ]()
  {
    cv::Mat blob;
    const int B = static_cast<int>( batchMats.size() );
    const int H = batchMats.front().rows;
    const int W = batchMats.front().cols;
    int dims[4] = { B, bandCount, H, W };
    blob = cv::Mat( 4, dims, CV_32F );
    blob.setTo( 0 );
    for ( int b = 0; b < B; ++b )
    {
      std::vector<cv::Mat> channels;
      cv::split( batchMats[static_cast<std::size_t>( b )], channels );
      for ( int c = 0; c < bandCount; ++c )
      {
        float *dst = blob.ptr<float>( b, c, 0 );
        const cv::Mat &ch = channels[static_cast<std::size_t>( c )];
        for ( int y = 0; y < H; ++y )
          std::memcpy( dst + static_cast<std::size_t>( y ) * W, ch.ptr<float>( y ),
                       static_cast<std::size_t>( W ) * sizeof( float ) );
      }
    }

    cv::Mat output;
    try
    {
      output = m_runtime->infer( blob );
    }
    catch ( const RSOperatorError & )
    {
      throw;
    }
    catch ( const std::exception &e )
    {
      throw RSOperatorError( ErrorCode::ComputationError,
                             std::string( "detection forward pass failed: " ) + e.what() );
    }
    if ( output.dims != 3 )
      throw RSOperatorError( ErrorCode::InvalidInputData,
                             "detection head output must be a 3-D tensor (got dims="
                               + std::to_string( output.dims ) + ")" );

    // Per-sample slice: the decode consumes a (1, C, N) tensor per tile.
    for ( int bi = 0; bi < B; ++bi )
    {
      const auto &win = batchWindows[static_cast<std::size_t>( bi )];
      const int sampleSizes[3] = { 1, output.size[1], output.size[2] };
      cv::Mat sample( 3, sampleSizes, output.type(),
                      output.ptr( bi ) ); // header onto sample bi's plane
      // The window was resized to the fixed model input:
      // fedPx * (windowPx/modelPx) maps the boxes back to raster pixels.
      const double scaleX = static_cast<double>( win[2] ) / std::max( 1, output.size[2] );
      const double scaleY = static_cast<double>( win[3] ) / std::max( 1, output.size[3] );
      const std::string decodeError =
        decodeDetections( sample, det, win[0], win[1], scaleX, scaleY, rasterW, rasterH, detections );
      if ( !decodeError.empty() )
        throw RSOperatorError( ErrorCode::ComputationError, decodeError );
    }
    stats.tilesProcessed += B;
    batchMats.clear();
    batchWindows.clear();
    batchCores.clear();
  };

  for ( int tileIndex = 0; tileIndex < stats.tilesPlanned; ++tileIndex )
  {
    context.throwIfCancelled();

      const CoreTile &t = core[static_cast<std::size_t>( tileIndex )];
      const int winX = t.x - halo;
      const int winY = t.y - halo;
      const int winW = t.w + 2 * halo;
      const int winH = t.h + 2 * halo;

      // Read the halo-extended window (NaN padding outside the raster),
      // mirroring the raster tiler's clamped-read pattern.
      std::fill( windowBuffer.begin(), windowBuffer.end(),
                 std::numeric_limits<float>::quiet_NaN() );
      {
        const int ix0 = std::max( 0, winX );
        const int iy0 = std::max( 0, winY );
        const int ix1 = std::min( rasterW, winX + winW );
        const int iy1 = std::min( rasterH, winY + winH );
        const int iw = ix1 - ix0;
        const int ih = iy1 - iy0;
        if ( iw > 0 && ih > 0 )
        {
          std::vector<float> temp( static_cast<std::size_t>( iw ) * ih * bandCount );
          if ( !ds.readWindowBip( bandList, ix0, iy0, iw, ih, temp.data() ) )
            throw RSOperatorError( ErrorCode::GdalError,
                                   "failed to read tile window at (" + std::to_string( t.x ) + ", "
                                     + std::to_string( t.y ) + ")" );
          const int shiftCols = ix0 - winX;
          const int shiftRows = iy0 - winY;
          for ( int row = 0; row < ih; ++row )
          {
            const std::size_t dstBase =
              ( static_cast<std::size_t>( row + shiftRows ) * winW + shiftCols ) * bandCount;
            const std::size_t srcBase = static_cast<std::size_t>( row ) * iw * bandCount;
            std::copy( temp.begin() + srcBase,
                       temp.begin() + srcBase + static_cast<std::size_t>( iw ) * bandCount,
                       windowBuffer.begin() + static_cast<std::ptrdiff_t>( dstBase ) );
          }
        }
      }

      // nodata_policy "zero": NaN (outside raster / declared sentinels) never
      // reaches the model; normalize in place afterwards.
      for ( float &v : windowBuffer )
      {
        if ( !std::isfinite( v ) )
          v = 0.0f;
      }
      if ( meanStd || ( pre.normalize == "linear" && pre.scale != 1.0 ) )
      {
        for ( std::size_t i = 0; i < windowBuffer.size(); ++i )
        {
          const std::size_t c = i % static_cast<std::size_t>( bandCount );
          double v = windowBuffer[i];
          if ( meanStd )
          {
            if ( !pre.mean.empty() )
              v -= pre.mean[c];
            if ( !pre.stdv.empty() && pre.stdv[c] > 0.0 )
              v /= pre.stdv[c];
          }
          v *= pre.scale;
          windowBuffer[i] = static_cast<float>( v );
        }
      }

      cv::Mat hwc( winH, winW, CV_32FC( bandCount ), windowBuffer.data() );
      cv::Mat tileMat = hwc.clone();
      if ( winW != modelW || winH != modelH )
      {
        std::vector<cv::Mat> channels;
        cv::split( tileMat, channels );
        for ( auto &ch : channels )
        {
          cv::Mat resized;
          cv::resize( ch, resized, cv::Size( modelW, modelH ), 0, 0, interp );
          ch = resized;
        }
        cv::merge( channels, tileMat );
      }
      batchMats.push_back( std::move( tileMat ) );
      batchWindows.push_back( { winX, winY, winW, winH } );
      batchCores.push_back( t );

      const bool batchFull = static_cast<int>( batchMats.size() ) >= batchSize
                             || tileIndex == stats.tilesPlanned - 1;
      if ( !batchFull )
        continue;

      // OOM ladder (Platform 4.0, goal §3): an over-budget batch is retried
      // tile-by-tile — box semantics never change with batching. At batch=1
      // an OOM is final with its diagnostic attached.
      try
      {
        flushBatch();
      }
      catch ( const RSOperatorError &e )
      {
        if ( classifyInferenceError( e.what() ) != InferenceFailureKind::OutOfMemory
             || batchMats.size() <= 1 )
          throw;
        ++stats.batchReductions;
        const std::vector<cv::Mat> pending = std::move( batchMats );
        const std::vector<std::array<int, 4>> pendingWins = std::move( batchWindows );
        const std::vector<CoreTile> pendingCores = std::move( batchCores );
        batchMats.clear();
        batchWindows.clear();
        batchCores.clear();
        for ( std::size_t i = 0; i < pending.size(); ++i )
        {
          batchMats.assign( 1, pending[i] );
          batchWindows.assign( 1, pendingWins[i] );
          batchCores.assign( 1, pendingCores[i] );
          try
          {
            flushBatch();
          }
          catch ( const RSOperatorError &inner )
          {
            if ( classifyInferenceError( inner.what() ) == InferenceFailureKind::OutOfMemory )
              throw RSOperatorError(
                ErrorCode::ComputationError,
                std::string( "detection inference ran out of memory even at batch=1: free memory "
                             "or use a smaller model — the engine never alters spatial resolution "
                             "or model semantics. Original error: " )
                  + inner.what() );
            throw;
          }
        }
      }
      context.reportProgress( static_cast<double>( stats.tilesProcessed )
                                / static_cast<double>( stats.tilesPlanned ),
                              "Tiled detection: " + std::to_string( stats.tilesProcessed ) + "/"
                                + std::to_string( stats.tilesPlanned ) );
  }

  // Whole-raster dedup: exact-duplicate collapse + NMS = the tile-overlap
  // resolution. Bounded accumulation guard before the write.
  stats.rawDetections = static_cast<int>( detections.size() );
  if ( stats.rawDetections > det.maxDetections )
    throw RSOperatorError(
      ErrorCode::InvalidInputData,
      "detection accumulated " + std::to_string( stats.rawDetections )
        + " boxes which exceeds output.detection.max_detections ("
        + std::to_string( det.maxDetections ) + ") — raise the cap or lower the conf_threshold" );
  dedupDetections( detections, det.nmsIou );
  stats.detectionsKept = static_cast<int>( detections.size() );

  context.throwIfCancelled();
  context.reportProgress( 0.95, "Writing detection vector: " + std::to_string( stats.detectionsKept )
                                  + " detections" );

  // --- Atomic vector publish: same-dir temp + rename (repo .tmp~ convention).
  const QFileInfo outFi( QString::fromStdString( outputPath ) );
  const QString workPath = outFi.absolutePath() + QLatin1Char( '/' ) + outFi.completeBaseName()
                           + QStringLiteral( ".tmp~." ) + outFi.suffix();
  QDir().mkpath( outFi.absolutePath() );
  removeVectorFiles( workPath );

  GDALAllRegister();
  OGRRegisterAll();
  GDALDriverH driver = GDALGetDriverByName( vectorDriverName( outputPath ).c_str() );
  if ( !driver )
  {
    removeVectorFiles( workPath );
    throw RSOperatorError( ErrorCode::GdalError, "vector driver not available" );
  }
  GDALDatasetH outDs = GDALCreate( driver, workPath.toUtf8().constData(), 0, 0, 0, GDT_Unknown, nullptr );
  if ( !outDs )
  {
    removeVectorFiles( workPath );
    throw RSOperatorError( ErrorCode::FileNotWritable,
                           "failed to create detection output: " + workPath.toStdString() );
  }
  OGRSpatialReferenceH srs = nullptr;
  if ( !ds.projection().isEmpty() )
  {
    srs = OSRNewSpatialReference( nullptr );
    if ( OSRSetFromUserInput( srs, ds.projection().toUtf8().constData() ) != OGRERR_NONE )
    {
      OSRDestroySpatialReference( srs );
      srs = nullptr;
    }
  }
  OGRLayerH layer = GDALDatasetCreateLayer( outDs, "detections", srs, wkbPolygon, nullptr );
  if ( srs )
    OSRDestroySpatialReference( srs );
  if ( !layer )
  {
    GDALClose( outDs );
    removeVectorFiles( workPath );
    throw RSOperatorError( ErrorCode::GdalError, "failed to create detections layer" );
  }
  const char *fieldNames[] = { "class", "confidence", "tile_x", "tile_y" };
  const OGRFieldType fieldTypes[] = { OFTString, OFTReal, OFTInteger, OFTInteger };
  for ( int i = 0; i < 4; ++i )
  {
    OGRFieldDefnH field = OGR_Fld_Create( fieldNames[i], fieldTypes[i] );
    if ( OGR_L_CreateField( layer, field, TRUE ) != OGRERR_NONE )
    {
      OGR_Fld_Destroy( field );
      GDALClose( outDs );
      removeVectorFiles( workPath );
      throw RSOperatorError( ErrorCode::GdalError,
                             std::string( "failed to create field: " ) + fieldNames[i] );
    }
    OGR_Fld_Destroy( field );
  }

  for ( const DetectionBox &box : detections )
  {
    // Raster pixel corners → map coordinates (GDAL geotransform, corner origin).
    const double gx1 = geoTransform[0] + box.x * geoTransform[1] + box.y * geoTransform[2];
    const double gy1 = geoTransform[3] + box.x * geoTransform[4] + box.y * geoTransform[5];
    const double gx2 = geoTransform[0] + ( box.x + box.w ) * geoTransform[1]
                       + ( box.y + box.h ) * geoTransform[2];
    const double gy2 = geoTransform[3] + ( box.x + box.w ) * geoTransform[4]
                       + ( box.y + box.h ) * geoTransform[5];

    const std::size_t classIndex = static_cast<std::size_t>(
      std::min( box.classId, static_cast<int>( det.classes.size() ) - 1 ) );
    const std::string className = det.classes[classIndex];

    OGRFeatureH feature = OGR_F_Create( OGR_L_GetLayerDefn( layer ) );
    OGR_F_SetFieldString( feature, OGR_F_GetFieldIndex( feature, "class" ), className.c_str() );
    OGR_F_SetFieldDouble( feature, OGR_F_GetFieldIndex( feature, "confidence" ), box.confidence );
    OGR_F_SetFieldInteger( feature, OGR_F_GetFieldIndex( feature, "tile_x" ),
                           static_cast<int>( box.x ) );
    OGR_F_SetFieldInteger( feature, OGR_F_GetFieldIndex( feature, "tile_y" ),
                           static_cast<int>( box.y ) );

    static const int ringCount = 5;
    double xs[5] = { gx1, gx2, gx2, gx1, gx1 };
    double ys[5] = { gy1, gy1, gy2, gy2, gy1 };
    OGRGeometryH ring = OGR_G_CreateGeometry( wkbLinearRing );
    for ( int i = 0; i < ringCount; ++i )
      OGR_G_AddPoint_2D( ring, xs[i], ys[i] );
    OGRGeometryH polygon = OGR_G_CreateGeometry( wkbPolygon );
    OGR_G_AddGeometryDirectly( polygon, ring );
    OGR_F_SetGeometryDirectly( feature, polygon );

    if ( OGR_L_CreateFeature( layer, feature ) != OGRERR_NONE )
    {
      OGR_F_Destroy( feature );
      GDALClose( outDs );
      removeVectorFiles( workPath );
      throw RSOperatorError( ErrorCode::GdalError, "failed to write detection feature" );
    }
    OGR_F_Destroy( feature );
  }
  GDALClose( outDs );

  // Publish: replace any previous output, then rename temp → final.
  removeVectorFiles( QString::fromStdString( outputPath ) );
  if ( !QFile::rename( workPath, QString::fromStdString( outputPath ) ) )
  {
    removeVectorFiles( workPath );
    throw RSOperatorError( ErrorCode::FileNotWritable,
                           "failed to publish detection output to: " + outputPath );
  }
  if ( outFi.suffix().toLower() == QLatin1String( "shp" ) )
  {
    const QString workBase = QFileInfo( workPath ).path() + QLatin1Char( '/' )
                             + QFileInfo( workPath ).completeBaseName();
    const QString outBase = outFi.path() + QLatin1Char( '/' ) + outFi.completeBaseName();
    for ( const char *ext : { ".dbf", ".shx", ".prj", ".cpg" } )
      QFile::rename( workBase + QString::fromLatin1( ext ), outBase + QString::fromLatin1( ext ) );
  }

  context.reportProgressForced( 1.0, "Tiled detection complete" );
  return stats;
}

} // namespace sicnu::operators::runtime
