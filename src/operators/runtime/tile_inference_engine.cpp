// src/operators/runtime/tile_inference_engine.cpp
#include "operators/runtime/tile_inference_engine.h"

#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h"

#include <opencv2/imgproc.hpp>

#include <cstring>
#include <map>
#include <QFile>
#include <QString>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>

namespace sicnu::operators::runtime {

// sicnu::operators::ErrorCode (free enum in rs_operator_error.h, visible via
// the parent namespace).

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

/// Read a halo-extended window as BIP floats, padding fully out-of-raster
/// regions with NaN. readWindowBip clamps to the raster extent itself, but
/// requires non-negative offsets, so left/top overhang is shifted here.
bool readBipWindow( const GdalDatasetWrapper &ds, const std::vector<int> &bands,
                    int winX, int winY, int winW, int winH, float *buffer )
{
  // The window may extend past the raster left/top/inside edges (negative
  // winX/winY from halo, or a right/bottom overhang). Fill NaN, then place
  // the clamped read at its shifted offset so the engine sees the window's
  // true geometry (halo rows/cols are NaN where outside the raster).
  std::fill( buffer, buffer + static_cast<std::size_t>( winW ) * winH * bands.size(),
             std::numeric_limits<float>::quiet_NaN() );
  const int ix0 = std::max( 0, winX );
  const int iy0 = std::max( 0, winY );
  const int ix1 = std::min( ds.width(), winX + winW );
  const int iy1 = std::min( ds.height(), winY + winH );
  const int iw = ix1 - ix0;
  const int ih = iy1 - iy0;
  if ( iw <= 0 || ih <= 0 )
    return true; // fully outside — NaN padding stands
  // Read the clamped region into a temp and scatter it into the window at
  // the shifted offset — never via buffer itself which would require a risky
  // in-place shift + stale-region clear.
  const std::size_t bandCount = bands.size();
  std::vector<float> temp( static_cast<std::size_t>( iw ) * ih * bandCount );
  if ( !ds.readWindowBip( bands, ix0, iy0, iw, ih, temp.data() ) )
    return false;
  const int shiftCols = ix0 - winX;
  const int shiftRows = iy0 - winY;
  for ( int row = 0; row < ih; ++row )
  {
    const std::size_t dstBase =
      ( static_cast<std::size_t>( row + shiftRows ) * winW + shiftCols ) * bandCount;
    const std::size_t srcBase = static_cast<std::size_t>( row ) * iw * bandCount;
    std::copy( temp.begin() + srcBase,
               temp.begin() + srcBase + static_cast<std::size_t>( iw ) * bandCount,
               buffer + dstBase );
  }
  return true;
}

} // namespace

TileInferenceEngine::TileInferenceEngine( ModelInfo model, ModelRuntimePtr runtime )
    : m_model( std::move( model ) ), m_runtime( std::move( runtime ) )
{
}

int TileInferenceEngine::effectiveTileSize( const ModelInfo &model )
{
  int tile = model.tiling.tileSize;
  if ( tile <= 0 && model.input.width > 0 )
    tile = model.input.width; // fixed graph input ⇒ tiles at the input size
  if ( tile <= 0 )
    tile = kDefaultTileSize;
  return std::max( kMinTileSize, tile );
}

int TileInferenceEngine::effectiveHalo( const ModelInfo &model )
{
  return model.tiling.halo > 0 ? model.tiling.halo : model.tiling.overlap / 2;
}

std::string TileInferenceEngine::inputDTypeMismatch( const ModelInfo &model,
                                                     const std::vector<int> &bands,
                                                     const std::function<int( int )> &bandDataType )
{
  if ( model.input.dtype.empty() || bands.empty() )
    return {};
  static const std::map<std::string, int> kAccepted = {
    { "float32", GDT_Float32 }, { "float64", GDT_Float64 },
    { "float16", GDT_Float32 }, { "uint16", GDT_UInt16 },
    { "int16", GDT_Int16 },     { "uint8", GDT_Byte },
    { "int32", GDT_Int32 },     { "uint32", GDT_UInt32 },
  };
  const auto accepted = kAccepted.find( model.input.dtype );
  if ( accepted == kAccepted.end() )
    return "model manifest declares unsupported input dtype '" + model.input.dtype + "'";
  for ( int band : bands )
  {
    const int rasterType = bandDataType( band );
    if ( rasterType != accepted->second )
      return "model manifest requires input dtype '" + model.input.dtype
               + "' but the raster band " + std::to_string( band ) + " has GDAL type "
               + std::to_string( rasterType )
               + " (convert the raster or update the manifest)";
  }
  return {};
}

std::string TileInferenceEngine::missingOutputTensor( const ModelInfo &model,
                                                      const std::vector<std::string> &graphOutputNames )
{
  if ( model.output.tensorNames.empty() )
    return {};
  if ( graphOutputNames.empty() )
    return {}; // runtime cannot enumerate outputs — contract stays advisory
  for ( const auto &declared : model.output.tensorNames )
  {
    if ( std::find( graphOutputNames.begin(), graphOutputNames.end(), declared )
         == graphOutputNames.end() )
    {
      std::string available;
      for ( const auto &name : graphOutputNames )
        available += ( available.empty() ? "" : ", " ) + name;
      return "model manifest declares output tensor '" + declared
               + "' but the loaded graph provides only: " + available;
    }
  }
  return {};
}

std::string TileInferenceEngine::outputTypeMismatch( int outputCvType, const std::string &tensorName )
{
  if ( outputCvType == CV_32F )
    return {};
  // Mnemonic for the common depths keeps the message readable; quantized
  // (8U/16U) heads are rejected until a dequantizing write path exists (#690).
  const char *depthName = nullptr;
  switch ( outputCvType )
  {
    case CV_8U: depthName = "CV_8U"; break;
    case CV_8S: depthName = "CV_8S"; break;
    case CV_16U: depthName = "CV_16U"; break;
    case CV_16S: depthName = "CV_16S"; break;
    case CV_32S: depthName = "CV_32S"; break;
    case CV_64F: depthName = "CV_64F"; break;
    case CV_16F: depthName = "CV_16F"; break;
    default: break; // the numeric type is authoritative for exotic combos
  }
  std::string actual = std::to_string( outputCvType );
  if ( depthName )
    actual += std::string( " (" ) + depthName + ")";
  return "model output tensor '" + ( tensorName.empty() ? std::string( "<default>" ) : tensorName )
           + "' has OpenCV type " + actual
           + " but the raster writer emits float32: expected CV_32F (type 5)";
}

std::string TileInferenceEngine::classesChannelMismatch( const ModelInfo &model, int outputChannels,
                                                         const std::string &tensorName )
{
  const std::size_t declared = model.output.classes.size();
  if ( declared == 0 || outputChannels == static_cast<int>( declared ) )
    return {};
  return "model output tensor '" + ( tensorName.empty() ? std::string( "<default>" ) : tensorName )
           + "' has " + std::to_string( outputChannels ) + " channel(s) but the manifest declares "
           + std::to_string( declared )
           + " classes (the raster head writes one channel per class)";
}

bool TileInferenceEngine::batchIsAllNoData( const std::vector<int> &validPixelCounts )
{
  if ( validPixelCounts.empty() )
    return false;
  for ( int valid : validPixelCounts )
  {
    if ( valid > 0 )
      return false;
  }
  return true;
}

TileInferenceStats TileInferenceEngine::run( const std::string &inputPath,
                                             const std::vector<int> &bands,
                                             const std::string &outputPath,
                                             RSOperatorContext &context )
{
  return run( inputPath, bands, outputPath, context, TileInferenceRunOptions{} );
}

TileInferenceStats TileInferenceEngine::run( const std::string &inputPath,
                                             const std::vector<int> &bands,
                                             const std::string &outputPath,
                                             RSOperatorContext &context,
                                             const TileInferenceRunOptions &options )
{
  if ( !m_runtime )
    throw RSOperatorError( ErrorCode::ComputationError, "tile inference engine has no runtime session" );

  // Manifest output.tensor_names contract (#705): every declared name must
  // exist in the loaded graph; the first declared name selects the head the
  // engine consumes. Skipped when the runtime cannot enumerate its outputs.
  // Platform 3.0 multi-head: when the runtime can enumerate its outputs, ALL
  // declared tensor names are consumed and their channels stacked in manifest
  // order. Without enumeration the contract stays advisory and only the first
  // declared name (or the default head) is used — the historical behavior.
  std::vector<std::string> headNames;
  if ( !m_model.output.tensorNames.empty() )
  {
    if ( const std::string missing = missingOutputTensor( m_model, m_runtime->outputTensorNames() );
         !missing.empty() )
      throw RSOperatorError( ErrorCode::InvalidInputData, missing );
    if ( m_runtime->outputTensorNames().empty() )
      headNames = { m_model.output.tensorNames.front() };
    else
      headNames = m_model.output.tensorNames;
  }
  else
  {
    headNames = { std::string() };
  }
  const std::string uncertainty = uncertaintyMethod( m_model );

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
                               "band " + std::to_string( b ) + " out of range (1.." + std::to_string( rasterBands ) + ")" );
    }
  }
  const int bandCount = static_cast<int>( bandList.size() );

  // Manifest dtype contract (#632/#705): input.dtype must match the raster's
  // actual GDAL type for EVERY band fed to the model (the engine always reads
  // float32; a mismatched dtype silently misnormalizes). Checking band 1
  // alone let mixed-type rasters pass validation and fail only mid-read.
  if ( const std::string dtypeError =
         inputDTypeMismatch( m_model, bandList,
                             [ &ds ]( int band ) { return ds.bandDataType( band ); } );
       !dtypeError.empty() )
    throw RSOperatorError( ErrorCode::InvalidInputData, dtypeError );

  // Manifest band_roles contract (#646): roles feed ranking, but at inference
  // the fed band count must match the declared roles (band i maps to role i,
  // and channel order is the file order - see the blob construction below).
  if ( !m_model.input.bandRoles.empty()
       && m_model.input.bandRoles.size() != static_cast<std::size_t>( bandCount ) )
    throw RSOperatorError( ErrorCode::InvalidParameter,
                           "model manifest declares "
                             + std::to_string( m_model.input.bandRoles.size() )
                             + " band roles but " + std::to_string( bandCount )
                             + " bands are fed (pass an explicit band list or fix the manifest)" );

  // Contract check that needs real data: per-channel mean/std must match the
  // bands actually fed to the model.
  const ModelPreprocessContract &pre = m_model.preprocess;
  const bool meanStd = pre.normalize == "mean_std";
  if ( meanStd && !pre.mean.empty() && pre.mean.size() != static_cast<std::size_t>( bandCount ) )
    throw RSOperatorError( ErrorCode::InvalidParameter,
                           "preprocess.mean declares " + std::to_string( pre.mean.size() )
                             + " channels but " + std::to_string( bandCount ) + " bands are fed" );
  if ( meanStd && !pre.stdv.empty() && pre.stdv.size() != static_cast<std::size_t>( bandCount ) )
    throw RSOperatorError( ErrorCode::InvalidParameter,
                           "preprocess.std declares " + std::to_string( pre.stdv.size() )
                             + " channels but " + std::to_string( bandCount ) + " bands are fed" );

  const int tileSize = std::min( effectiveTileSize( m_model ), std::max( rasterW, rasterH ) );
  const int halo = std::max( 0, effectiveHalo( m_model ) );
  int batchSize = effectiveBatchSize( m_model, ModelRuntimeRegistry::instance().hardware(),
                                      tileSize, bandCount );
  // batchCap is a CAP, never an upgrade: it can only lower the effective
  // batch (memory-pinned runs), keeping the manifest/budget verdict as the
  // upper bound that estimateExecution also admitted on.
  if ( options.batchSizeOverride > 0 )
    batchSize = std::min( batchSize, options.batchSizeOverride );
  batchSize = std::max( 1, batchSize );
  const bool resizeToInput = pre.resize == "to_input" && m_model.input.width > 0 && m_model.input.height > 0;
  const int modelW = resizeToInput ? m_model.input.width : 0;
  const int modelH = resizeToInput ? m_model.input.height : 0;
  const int interp = pre.interpolation == "nearest" ? cv::INTER_NEAREST : cv::INTER_LINEAR;

  // Core tile grid over the raster extent.
  std::vector<CoreTile> core;
  for ( int y = 0; y < rasterH; y += tileSize )
    for ( int x = 0; x < rasterW; x += tileSize )
      core.push_back( CoreTile{ x, y, std::min( tileSize, rasterW - x ), std::min( tileSize, rasterH - y ) } );
  const int totalTiles = static_cast<int>( core.size() );

  TileInferenceStats stats;
  stats.tileSize = tileSize;
  stats.halo = halo;
  stats.batchSize = batchSize;
  stats.tilesPlanned = totalTiles;
  stats.outWidth = rasterW;
  stats.outHeight = rasterH;

  // Reusable read buffer: one halo-extended window at a time.
  const int maxWin = tileSize + 2 * halo;
  std::vector<float> windowBuffer( static_cast<std::size_t>( maxWin ) * maxWin * bandCount );
  // Per-band declared NoData sentinels (the BIP read yields raw values; the
  // shared NaN convention is applied here, matching opencv_utils semantics).
  std::vector<float> bandSentinel( static_cast<std::size_t>( bandCount ), 0.0f );
  std::vector<bool> bandHasSentinel( static_cast<std::size_t>( bandCount ), false );
  for ( int i = 0; i < bandCount; ++i )
  {
    bool has = false;
    const double nd = ds.bandNoDataValue( bandList[static_cast<std::size_t>( i )], &has );
    if ( has && std::isfinite( nd ) )
    {
      bandSentinel[static_cast<std::size_t>( i )] = static_cast<float>( nd );
      bandHasSentinel[static_cast<std::size_t>( i )] = true;
    }
  }

  // Per-batch preprocessed tiles (HWC float32) + their core nodata masks,
  // the fed spatial sizes and the core tiles themselves, awaiting one forward
  // pass. An explicit batchCores vector eliminates any arithmetic derivation
  // of the core tile index in the tail batch.
  std::vector<cv::Mat> batchMats;
  std::vector<cv::Mat> batchMasks;
  std::vector<std::pair<int, int>> batchFedSize;
  std::vector<CoreTile> batchCores;
  std::vector<int> batchValidPixels;
  // Core tiles of batches whose every pixel is nodata: their forward pass is
  // skipped and NoData is written directly once the writer exists (#705).
  std::vector<CoreTile> deferredNoData;

  std::unique_ptr<GdalStreamingOutput> writer;
  // Any failure after the writer exists must not leave a truncated GeoTIFF at
  // the caller's output path looking like a result (#647): the catch below
  // abandons the writer so its destructor removes the partial file.
  auto abandonOnFailure = [ &writer ]() {
    if ( writer )
      writer->abandon();
  };
  const std::array<double, 6> geoTransform = ds.geoTransform();
  const QString projection = ds.projection();

  context.reportProgressForced( 0.0, "Tiled inference: " + std::to_string( totalTiles ) + " tiles" );

  int done = 0;    // tiles fully written (forwarded or NoData-flushed)
  int skipped = 0; // tiles deferred as all-nodata, not yet written

  // Writes NoData directly for tiles whose forward pass was skipped (#705):
  // every core pixel of these tiles is invalid, so every output channel is
  // the writer's NoData value — the same NaN the mask restore writes for
  // partially invalid tiles.
  auto flushDeferredNoData = [ & ]( int currentTileIndex )
  {
    if ( !writer || deferredNoData.empty() )
      return;
    for ( const CoreTile &dt : deferredNoData )
    {
      cv::Mat nanPlane( dt.h, dt.w, CV_32FC1, cv::Scalar( std::numeric_limits<float>::quiet_NaN() ) );
      for ( int c = 0; c < stats.outBands; ++c )
      {
        const GdalBlockStream::Tile writeTile{ dt.x, dt.y, dt.w, dt.h, 0, dt.w, dt.h,
                                               currentTileIndex, totalTiles };
        if ( !writer->writeTile( c + 1, writeTile, nanPlane.ptr<float>() ) )
          throw RSOperatorError( ErrorCode::FileNotWritable,
                                 "failed to write output tile at ("
                                   + std::to_string( dt.x ) + ", " + std::to_string( dt.y ) + ")" );
      }
      ++done;
      --skipped;
    }
    deferredNoData.clear();
  };

  // One forward pass + streaming write for the tiles accumulated in the
  // batch buffers. Runs for regular batches and for the all-nodata probe.
  auto flushBatch = [ & ]( int currentTileIndex )
  {
    // One forward pass per batch on the shared (cached) session.
    // Manual NCHW pack (#671): blobFromImage(s) asserts channels in
    // {1,3,4} — multispectral / SAR inference (2, >=5 channels) died with
    // a raw cv::Exception that also violates the engine's RSOperatorError
    // contract. The buffer already holds GDAL band order and manifest
    // band_roles expect channel i == file band i, so we keep that order
    // (swapRB=false equivalent) without delegating to the imread helper.
    // The band_roles arity contract is validated up front in run().
    cv::Mat blob;
    try
    {
      const int C = bandCount;
      const int B = static_cast<int>( batchMats.size() );
      const int H = batchMats.front().rows;
      const int W = batchMats.front().cols;
      // blobFromImages asserted same-size batches (a mixed batch threw);
      // edge tiles make that reachable whenever raster dims are not a
      // multiple of the tile size and resizeToInput is off — keep the check
      // LOUD instead of silently packing garbage.
      for ( const cv::Mat &m : batchMats )
      {
        if ( m.rows != H || m.cols != W )
          throw RSOperatorError( ErrorCode::InvalidInputData,
                                 "mixed tile sizes in one inference batch (" + std::to_string( m.cols ) + "x"
                                   + std::to_string( m.rows ) + " vs " + std::to_string( W ) + "x"
                                   + std::to_string( H ) + ") - enable resize:to_input or align the raster" );
      }
      int dims[4] = { B, C, H, W };
      blob = cv::Mat( 4, dims, CV_32F );
      blob.setTo( 0 );
      for ( int b = 0; b < B; ++b )
      {
        std::vector<cv::Mat> channels;
        cv::split( batchMats[static_cast<std::size_t>( b )], channels );
        for ( int c = 0; c < C; ++c )
        {
          const cv::Mat &ch = channels[static_cast<std::size_t>( c )];
          // ptr(b, c, 0) is the 3-arg overload (data + b*step0 + c*step1):
          // the const int* overload reads idx[dims] (a 4th, garbage element)
          // on a 4-D Mat — an OOB stack read whose damage depends on the
          // stack garbage.
          float *dst = blob.ptr<float>( b, c, 0 );
          for ( int y = 0; y < H; ++y )
          {
            std::memcpy( dst + static_cast<std::size_t>( y ) * W, ch.ptr<float>( y ),
                         static_cast<std::size_t>( W ) * sizeof( float ) );
          }
        }
      }
    }
    catch ( const RSOperatorError & )
    {
      throw;
    }
    catch ( const std::exception &e )
    {
      throw RSOperatorError( ErrorCode::OpenCvError,
                             std::string( "failed to build inference blob (bands fed: " )
                               + std::to_string( bandCount ) + "): " + e.what() );
    }
    catch ( ... )
    {
      throw RSOperatorError( ErrorCode::OpenCvError, "failed to build inference blob (unknown)" );
    }
    // TTA helper: average the head logits over horizontal/vertical flips so
    // the result stays on the original orientation (Platform 3.0 goal §10;
    // opt-in via run options — compute doubles, honest default off).
    auto forwardHead = [ & ]( const std::string &headName ) -> cv::Mat {
      auto forwardOnce = [ & ]( const cv::Mat &b ) -> cv::Mat {
        try
        {
          return headName.empty() ? m_runtime->infer( b ) : m_runtime->infer( b, headName );
        }
        catch ( const RSOperatorError & )
        {
          throw;
        }
        catch ( const std::exception &e )
        {
          throw RSOperatorError( ErrorCode::ComputationError,
                                 std::string( "inference forward pass failed: " ) + e.what() );
        }
      };
      cv::Mat output = forwardOnce( blob );
      if ( options.tta == TtaMode::None )
        return output;

      // cv::flipND needs OpenCV 4.5+; CI images may lack it. For NCHW 4-D
      // tensors, flip H (axis 2) / W (axis 3) via per-(n,c) 2-D cv::flip.
      auto flipNCHWAxis = []( const cv::Mat &src, int axis ) {
        CV_Assert( src.dims == 4 && ( axis == 2 || axis == 3 ) );
        const int sizes[4] = { src.size[0], src.size[1], src.size[2], src.size[3] };
        cv::Mat dst( 4, sizes, src.type() );
        const int N = sizes[0], C = sizes[1], H = sizes[2], W = sizes[3];
        for ( int n = 0; n < N; ++n )
        {
          for ( int c = 0; c < C; ++c )
          {
            // ptr(n,c) yields the contiguous HxW plane for continuous NCHW.
            cv::Mat srcPlane( H, W, src.type(), const_cast<uchar *>( src.ptr( n, c ) ) );
            cv::Mat dstPlane( H, W, dst.type(), dst.ptr( n, c ) );
            cv::flip( srcPlane, dstPlane, axis == 3 ? 1 : 0 );
          }
        }
        return dst;
      };
      // Every forward returns a freshly detached Mat (the runtime clones its
      // output), so the accumulator is safe from aliasing.
      auto flipH = [ & ]( const cv::Mat &b ) { return flipNCHWAxis( b, 3 ); };
      auto flipV = [ & ]( const cv::Mat &b ) { return flipNCHWAxis( b, 2 ); };
      auto forwardUnflippedH = [ & ]( ) {
        return flipNCHWAxis( forwardOnce( flipH( blob ) ), 3 );
      };
      auto forwardUnflippedHV = [ & ]( ) {
        return flipNCHWAxis( flipNCHWAxis( forwardOnce( flipH( flipV( blob ) ) ), 2 ), 3 );
      };
      cv::Mat acc = output.clone();
      acc += forwardUnflippedH();
      if ( options.tta == TtaMode::HVFlip )
      {
        auto forwardUnflippedV = [ & ]( ) {
          return flipNCHWAxis( forwardOnce( flipV( blob ) ), 2 );
        };
        acc += forwardUnflippedV();
        acc += forwardUnflippedHV();
        acc *= 0.25;
      }
      else
      {
        acc *= 0.5;
      }
      return acc;
    };

    // Forward every declared head (multi-head = one forward per head; the
    // shared session serializes them). Heads are validated with the same
    // contracts as the historical single-head path.
    std::vector<cv::Mat> headOutputs( headNames.size() );
    for ( std::size_t h = 0; h < headNames.size(); ++h )
    {
      const std::string &headName = headNames[h];
      cv::Mat output = forwardHead( headName );
      if ( output.dims != 4 || output.size[0] != static_cast<int>( batchMats.size() ) )
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "model output is not a 4-D NCHW batch matching the input tiles (got dims="
                                 + std::to_string( output.dims ) + ", N=" + std::to_string( output.size[0] ) + ")" );
      if ( const std::string typeError = outputTypeMismatch( output.type(), headName );
           !typeError.empty() )
        throw RSOperatorError( ErrorCode::InvalidInputData, typeError );
      if ( output.size[1] <= 0 )
        throw RSOperatorError( ErrorCode::ComputationError, "model output has no channels" );
      if ( const std::string classesError = classesChannelMismatch( m_model, output.size[1], headName );
           !classesError.empty() )
        throw RSOperatorError( ErrorCode::InvalidInputData, classesError );
      headOutputs[h] = output;
    }

    // Band layout: every head's channels in declaration order, plus one
    // uncertainty band after the first head with >= 2 channels when the
    // manifest declares output.uncertainty (classification/segmentation
    // semantics).
    const bool addUncertainty = !uncertainty.empty();
    int totalBands = 0;
    std::vector<int> headChannelList( headOutputs.size() );
    int uncertaintyHeadIndex = -1;
    for ( std::size_t h = 0; h < headOutputs.size(); ++h )
    {
      headChannelList[h] = headOutputs[h].size[1];
      totalBands += headChannelList[h];
      if ( addUncertainty && uncertaintyHeadIndex < 0 && headChannelList[h] >= 2 )
        uncertaintyHeadIndex = static_cast<int>( h );
    }
    const int uncertaintyBandOffset = uncertaintyHeadIndex >= 0 ? totalBands : -1;
    if ( uncertaintyHeadIndex >= 0 )
      ++totalBands;

    if ( !writer )
    {
      writer = std::make_unique<GdalStreamingOutput>( QString::fromStdString( outputPath ),
                                                      rasterW, rasterH, totalBands, /*GDT_Float32*/ 6,
                                                      geoTransform, projection );
      if ( !writer->isOpen() )
        throw RSOperatorError( ErrorCode::FileNotWritable, "failed to create output raster: " + outputPath );
      writer->setNoDataValue( std::numeric_limits<double>::quiet_NaN() );
      stats.outBands = totalBands;
      stats.headChannels = headChannelList;
      if ( uncertaintyHeadIndex >= 0 )
        stats.headChannels[static_cast<std::size_t>( uncertaintyHeadIndex )] += 1;
      // Record the head layout so downstream consumers can split the stack.
      {
        QString layout;
        for ( std::size_t h = 0; h < headNames.size(); ++h )
        {
          if ( h )
            layout += QLatin1Char( ',' );
          const QString name = headNames[h].empty() ? QStringLiteral( "default" )
                                                    : QString::fromStdString( headNames[h] );
          layout += QString( "%1:%2" ).arg( name ).arg( headChannelList[h] );
        }
        if ( uncertaintyHeadIndex >= 0 )
          layout += QString( ",uncertainty:%1" ).arg( QString::fromStdString( uncertainty ) );
        writer->setMetadataItem( QStringLiteral( "SICNU_OUTPUT_HEADS" ), layout );
      }
    }
    else if ( totalBands != stats.outBands )
    {
      throw RSOperatorError( ErrorCode::ComputationError,
                             "model output channel count changed mid-run (" + std::to_string( stats.outBands )
                               + " → " + std::to_string( totalBands ) + ")" );
    }

    // The writer exists now, so NoData tiles deferred by earlier all-nodata
    // batches can go straight to disk.
    flushDeferredNoData( currentTileIndex );

    int bandOffset = 0;
    for ( std::size_t h = 0; h < headOutputs.size(); ++h )
    {
      const cv::Mat &output = headOutputs[h];
      const std::string &headName = headNames[h];
      const int outChannels = output.size[1];
      const int outH = output.size[2];
      const int outW = output.size[3];
      const bool isUncertaintyHead = ( uncertaintyHeadIndex == static_cast<int>( h ) );
      const cv::Mat flat = output.reshape( 1, std::vector<int>{ static_cast<int>( batchMats.size() ) * outChannels,
                                                               outH * outW } );
      for ( std::size_t bi = 0; bi < batchMats.size(); ++bi )
      {
        const CoreTile &bt = batchCores[bi];
        const int fedW = batchFedSize[bi].first;
        const int fedH = batchFedSize[bi].second;
        // Stitched (core-size) planes of this tile for the uncertainty pass.
        std::vector<cv::Mat> headPlanes;
        if ( isUncertaintyHead && !uncertainty.empty() )
          headPlanes.reserve( static_cast<std::size_t>( outChannels ) );
        for ( int c = 0; c < outChannels; ++c )
        {
          cv::Mat plane = flat.row( static_cast<int>( bi ) * outChannels + c )
                            .reshape( 1, outH )
                            .clone(); // (outH, outW) CV_32F
          // Map the output plane back onto the core tile. Grid-preserving
          // models (out == fed spatial size): CROP the halo margin away — the
          // core pixels are [halo, halo+core) so overlapping windows never
          // shift or duplicate output. Models that change the spatial dims
          // (strided heads): resample back to the core size.
          if ( outW == fedW && outH == fedH )
          {
            if ( resizeToInput )
            {
              // The window was resampled to the fixed model input before the
              // forward pass; scale the core rect accordingly.
              const double sxF = static_cast<double>( outW ) / std::max( 1, bt.w + 2 * halo );
              const double syF = static_cast<double>( outH ) / std::max( 1, bt.h + 2 * halo );
              int cx = static_cast<int>( std::lround( halo * sxF ) );
              int cy = static_cast<int>( std::lround( halo * syF ) );
              int cw = std::max( 1, static_cast<int>( std::lround( bt.w * sxF ) ) );
              int ch = std::max( 1, static_cast<int>( std::lround( bt.h * syF ) ) );
              cx = std::clamp( cx, 0, std::max( 0, outW - 1 ) );
              cy = std::clamp( cy, 0, std::max( 0, outH - 1 ) );
              cw = std::min( cw, outW - cx );
              ch = std::min( ch, outH - cy );
              plane = plane( cv::Range( cy, cy + ch ), cv::Range( cx, cx + cw ) ).clone();
              if ( plane.cols != bt.w || plane.rows != bt.h )
                cv::resize( plane, plane, cv::Size( bt.w, bt.h ), 0, 0, interp );
            }
            else if ( halo > 0 && outW == fedW && outH == fedH )
            {
              // Grid-preserving with halo: crop the halo border away.
              plane = plane( cv::Range( halo, halo + bt.h ), cv::Range( halo, halo + bt.w ) ).clone();
            }
          }
          else if ( outW != bt.w || outH != bt.h )
          {
            cv::resize( plane, plane, cv::Size( bt.w, bt.h ), 0, 0, interp );
          }
          // Uncertainty statistics need the PRE-threshold class planes:
          // entropy/margin over binarized {0,1} planes would be meaningless.
          cv::Mat thresholded;
          if ( isUncertaintyHead )
            headPlanes.push_back( plane );
          if ( m_model.postprocess.maskThreshold >= 0.0 )
          {
            const float thr = static_cast<float>( m_model.postprocess.maskThreshold );
            cv::Mat mask = plane >= thr; // NaN ≥ thr is false → 0, restored below
            mask.convertTo( thresholded, CV_32F, 1.0 / 255.0 );
            plane = thresholded;
          }
          // Restore nodata on core pixels whose every input band was invalid.
          const cv::Mat &tileMask = batchMasks[bi];
          for ( int row = 0; row < bt.h; ++row )
          {
            const uchar *maskRow = tileMask.ptr<uchar>( row );
            float *outRow = plane.ptr<float>( row );
            for ( int col = 0; col < bt.w; ++col )
            {
              if ( maskRow[col] )
                outRow[col] = std::numeric_limits<float>::quiet_NaN();
            }
          }
          const GdalBlockStream::Tile writeTile{ bt.x, bt.y, bt.w, bt.h, 0, bt.w, bt.h,
                                                 currentTileIndex, totalTiles };
          if ( !writer->writeTile( bandOffset + c + 1, writeTile, plane.ptr<float>() ) )
            throw RSOperatorError( ErrorCode::FileNotWritable, "failed to write output tile at ("
                                   + std::to_string( bt.x ) + ", " + std::to_string( bt.y ) + ")" );
        }
        if ( isUncertaintyHead && uncertaintyBandOffset >= 0 )
        {
          cv::Mat unc = headUncertainty( headPlanes, uncertainty );
          const GdalBlockStream::Tile writeTile{ bt.x, bt.y, bt.w, bt.h, 0, bt.w, bt.h,
                                                 currentTileIndex, totalTiles };
          if ( !writer->writeTile( uncertaintyBandOffset + 1, writeTile, unc.ptr<float>() ) )
            throw RSOperatorError( ErrorCode::FileNotWritable, "failed to write uncertainty tile" );
        }
        // Tiles are counted once (on the first head); heads share one tile.
        if ( h == 0 )
          ++done;
      }
      bandOffset += outChannels;
    }
    batchMats.clear();
    batchMasks.clear();
    batchFedSize.clear();
    batchCores.clear();
    batchValidPixels.clear();
  };

  try
  {
  for ( int tileIndex = 0; tileIndex < totalTiles; ++tileIndex )
  {
    context.throwIfCancelled();

    const CoreTile &t = core[static_cast<std::size_t>( tileIndex ) ];
    const int winX = t.x - halo;
    const int winY = t.y - halo;
    const int winW = t.w + 2 * halo;
    const int winH = t.h + 2 * halo;
    if ( !readBipWindow( ds, bandList, winX, winY, winW, winH, windowBuffer.data() ) )
      throw RSOperatorError( ErrorCode::GdalError, "failed to read tile window at (" + std::to_string( t.x )
                             + ", " + std::to_string( t.y ) + ")" );

    // Declared sentinels → NaN so the mask/zero pipeline below sees them.
    {
      const std::size_t totalFloats = static_cast<std::size_t>( winH ) * winW * bandCount;
      for ( std::size_t i = 0; i < totalFloats; ++i )
      {
        const std::size_t b = i % static_cast<std::size_t>( bandCount );
        if ( bandHasSentinel[b] && windowBuffer[i] == bandSentinel[b] )
          windowBuffer[i] = std::numeric_limits<float>::quiet_NaN();
      }
    }

    // Wrap the window as HWC float and preprocess in place. Pixels where EVERY
    // band is non-finite are marked invalid: the model sees 0 (nodata_policy
    // "zero") and the output pixel is restored to NaN afterwards.
    cv::Mat hwc( winH, winW, CV_32FC( bandCount ), windowBuffer.data() );
    cv::Mat invalidMask( t.h, t.w, CV_8UC1, cv::Scalar( 0 ) );
    int validPixels = 0; // core pixels with at least one finite band (#705)
    {
      const float *src = windowBuffer.data();
      const std::size_t windowStride = static_cast<std::size_t>( winW ) * bandCount;
      for ( int row = 0; row < t.h; ++row )
      {
        // Core region row inside the window starts at halo pixels in.
        const float *winRow = src + static_cast<std::size_t>( row + halo ) * windowStride
                              + static_cast<std::size_t>( halo ) * bandCount;
        uchar *maskRow = invalidMask.ptr<uchar>( row );
        for ( int col = 0; col < t.w; ++col )
        {
          const float *px = winRow + static_cast<std::size_t>( col ) * bandCount;
          bool allInvalid = true;
          for ( int c = 0; c < bandCount; ++c )
          {
            if ( std::isfinite( px[c] ) )
            {
              allInvalid = false;
              break;
            }
          }
          maskRow[col] = allInvalid ? 1 : 0;
          if ( !allInvalid )
            ++validPixels;
        }
      }
      // nodata_policy "zero" (default; the only supported policy - others
      // are rejected at manifest parse, #646): non-finite samples become 0.
      const std::size_t totalFloats = static_cast<std::size_t>( winH ) * winW * bandCount;
      for ( std::size_t i = 0; i < totalFloats; ++i )
      {
        if ( !std::isfinite( windowBuffer[i] ) )
          windowBuffer[i] = 0.0f;
      }
      // Normalize in place (HWC): linear x*scale, mean_std (x-mean)/std*scale.
      const double *meanArr = meanStd && !pre.mean.empty() ? pre.mean.data() : nullptr;
      const double *stdArr = meanStd && !pre.stdv.empty() ? pre.stdv.data() : nullptr;
      const double scale = pre.scale;
      // Scale applies only to linear/mean_std normalization (#646): with
      // normalize "none" the pixels must reach the model unscaled, matching
      // the model_catalog.h contract ("applied last (linear & mean_std)").
      if ( meanStd || ( pre.normalize == "linear" && scale != 1.0 ) )
      {
        float *data = windowBuffer.data();
        for ( std::size_t i = 0; i < totalFloats; ++i )
        {
          const std::size_t c = i % static_cast<std::size_t>( bandCount );
          double v = data[i];
          if ( meanStd )
          {
            if ( meanArr )
              v -= meanArr[c];
            if ( stdArr && stdArr[c] > 0.0 )
              v /= stdArr[c];
          }
          v *= scale;
          data[i] = static_cast<float>( v );
        }
      }
    }

    cv::Mat tileMat = hwc.clone(); // detached from the reused window buffer
    batchCores.push_back( t );
    if ( resizeToInput && ( winW != modelW || winH != modelH ) )
    {
      // cv::resize is limited to few channels; resample per band and merge.
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
    batchMasks.push_back( invalidMask.clone() ); // detached from the per-tile scratch mask
    batchFedSize.emplace_back( resizeToInput ? modelW : winW, resizeToInput ? modelH : winH );
    batchValidPixels.push_back( validPixels );

    const bool batchFull = static_cast<int>( batchMats.size() ) >= batchSize || tileIndex == totalTiles - 1;
    if ( !batchFull )
      continue;

    // #705: a batch whose tiles hold ZERO valid pixels would run full forward
    // passes only for every output pixel to be overwritten by the NoData
    // restore — skip the forward and queue direct NoData writes instead
    // (flushed as soon as the streaming writer exists).
    if ( batchIsAllNoData( batchValidPixels ) )
    {
      skipped += static_cast<int>( batchCores.size() );
      stats.tilesSkippedNoData += static_cast<int>( batchCores.size() );
      deferredNoData.insert( deferredNoData.end(), batchCores.begin(), batchCores.end() );
      batchMats.clear();
      batchMasks.clear();
      batchFedSize.clear();
      batchCores.clear();
      batchValidPixels.clear();
      context.reportProgress( static_cast<double>( done + skipped ) / static_cast<double>( totalTiles ),
                              "Tiled inference: " + std::to_string( done + skipped ) + "/"
                                + std::to_string( totalTiles ) );
      continue;
    }

    flushBatch( tileIndex );
    context.reportProgress( static_cast<double>( done + skipped ) / static_cast<double>( totalTiles ),
                            "Tiled inference: " + std::to_string( done + skipped ) + "/"
                              + std::to_string( totalTiles ) );
  }

  // Entire raster is NoData: no regular batch ever ran, so the writer (and
  // with it the model's true output channel count) does not exist yet. One
  // zero-tile probe forward establishes the output shape — its result is
  // NaN-restored like any other invalid tile. The probe tile is popped from
  // the deferred list and written by the normal flushBatch path.
  if ( !deferredNoData.empty() && !writer )
  {
    const CoreTile probeTile = deferredNoData.front();
    deferredNoData.erase( deferredNoData.begin() );
    const int probeFedW = resizeToInput ? modelW : probeTile.w + 2 * halo;
    const int probeFedH = resizeToInput ? modelH : probeTile.h + 2 * halo;
    batchMats.push_back( cv::Mat( probeFedH, probeFedW, CV_32FC( bandCount ), cv::Scalar( 0.0 ) ) );
    batchMasks.push_back( cv::Mat( probeTile.h, probeTile.w, CV_8UC1, cv::Scalar( 1 ) ) );
    batchFedSize.emplace_back( probeFedW, probeFedH );
    batchCores.push_back( probeTile );
    batchValidPixels.push_back( 0 );
    flushBatch( totalTiles - 1 );
  }
  if ( !deferredNoData.empty() )
    flushDeferredNoData( totalTiles - 1 );

  }
  catch ( ... )
  {
    abandonOnFailure();
    throw;
  }

  QString writeError;
  if ( !writer || !writer->closeWithError( &writeError ) )
  {
    if ( writer )
      writer->removeOutput();
    throw RSOperatorError( ErrorCode::FileNotWritable,
                           "failed to finalize output raster: " + writeError.toStdString() );
  }
  context.reportProgressForced( 1.0, "Tiled inference complete" );
  stats.tilesProcessed = done;
  return stats;
}

int TileInferenceEngine::effectiveBatchSize( const ModelInfo &model,
                                             const ModelHardwareCapabilities &hw,
                                             int tilePx, int fedChannels )
{
  // Manifest request first; the budget can only lower it.
  int batch = std::max( 1, model.tiling.batchSize );
  const int tile = std::max( kMinTileSize, tilePx );
  // Per-sample working set: input blob + output planes + halo overhead.
  const unsigned long long perSampleBytes =
    4ULL * static_cast<unsigned long long>( tile ) * tile
      * static_cast<unsigned long long>( std::max( 1, fedChannels ) ) * 4ULL;
  // GPU VRAM budget (when reported via SICNU_MODEL_VRAM_MB) is the only
  // enforced ceiling here; CPU-side RAM admission is owned by TaskCenter's
  // resource budget fed by estimateExecution — this function deliberately
  // does not guess a RAM share.
  unsigned long long budgetBytes = 0;
  if ( hw.vramBudgetMb > 0 )
    budgetBytes = static_cast<unsigned long long>( hw.vramBudgetMb ) * 1024ULL * 1024ULL;
  if ( budgetBytes > 0 && perSampleBytes > 0 )
  {
    const int budgetBatch = static_cast<int>( std::max<unsigned long long>(
      1, budgetBytes / std::max<unsigned long long>( perSampleBytes, 1 ) ) );
    batch = std::min( batch, budgetBatch );
  }
  return std::max( 1, batch );
}

std::string TileInferenceEngine::uncertaintyMethod( const ModelInfo &model )
{
  const std::string &u = model.output.uncertainty;
  if ( u.empty() || u == "none" )
    return {};
  if ( u == "entropy" || u == "margin" )
    return u;
  return {}; // unknown tokens are rejected at manifest parse
}

cv::Mat TileInferenceEngine::headUncertainty( const std::vector<cv::Mat> &classPlanes,
                                              const std::string &method )
{
  const int channels = static_cast<int>( classPlanes.size() );
  if ( channels < 2 || classPlanes.front().empty() )
    return cv::Mat();
  const int rows = classPlanes.front().rows;
  const int cols = classPlanes.front().cols;
  cv::Mat result( rows, cols, CV_32F );

  if ( method == "margin" )
  {
    // Probabilities via softmax first, then top1 − top2 gap.
    for ( int r = 0; r < rows; ++r )
    {
      float *outRow = result.ptr<float>( r );
      for ( int c = 0; c < cols; ++c )
      {
        float maxLogit = -std::numeric_limits<float>::infinity();
        for ( const cv::Mat &plane : classPlanes )
          maxLogit = std::max( maxLogit, plane.at<float>( r, c ) );
        double sum = 0.0;
        double top1 = 0.0;
        double top2 = 0.0;
        for ( const cv::Mat &plane : classPlanes )
        {
          const double p = std::exp( static_cast<double>( plane.at<float>( r, c ) ) - maxLogit );
          sum += p;
        }
        for ( const cv::Mat &plane : classPlanes )
        {
          const double p = std::exp( static_cast<double>( plane.at<float>( r, c ) ) - maxLogit ) / sum;
          if ( p > top1 )
          {
            top2 = top1;
            top1 = p;
          }
          else if ( p > top2 )
          {
            top2 = p;
          }
        }
        outRow[c] = static_cast<float>( top1 - top2 );
      }
    }
    return result;
  }

  // Default: softmax entropy in [0, ln C].
  const double logC = std::log( static_cast<double>( channels ) );
  for ( int r = 0; r < rows; ++r )
  {
    float *outRow = result.ptr<float>( r );
    for ( int c = 0; c < cols; ++c )
    {
      float maxLogit = -std::numeric_limits<float>::infinity();
      for ( const cv::Mat &plane : classPlanes )
        maxLogit = std::max( maxLogit, plane.at<float>( r, c ) );
      double sum = 0.0;
      for ( const cv::Mat &plane : classPlanes )
        sum += std::exp( static_cast<double>( plane.at<float>( r, c ) ) - maxLogit );
      double entropy = 0.0;
      for ( const cv::Mat &plane : classPlanes )
      {
        const double p = std::exp( static_cast<double>( plane.at<float>( r, c ) ) - maxLogit ) / sum;
        if ( p > 0.0 )
          entropy -= p * std::log( p );
      }
      outRow[c] = static_cast<float>( entropy / logC ); // normalized [0, 1]
    }
  }
  return result;
}

} // namespace sicnu::operators::runtime
