// src/operators/runtime/tile_inference_engine.cpp
#include "operators/runtime/tile_inference_engine.h"

#include "processing/gdal/gdal_dataset_wrapper.h"
#include "processing/gdal/gdal_multiband_block_stream.h"

#include "rs_classification_utils.h"

#include <opencv2/imgproc.hpp>

#include <array>
#include <cstring>
#include <map>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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

  // Platform 7.0 preprocess additions are executed by the multimodal engine;
  // the single-input engine refuses them loudly instead of running identity.
  if ( m_model.preprocess.pad > 0 || !std::isnan( m_model.preprocess.clampMin )
       || !std::isnan( m_model.preprocess.clampMax ) )
    throw RSOperatorError( ErrorCode::InvalidInputData,
                           "preprocess.pad / clamp_min / clamp_max are executed by the "
                             "multi-input engine (runMultiInput); this single-input model must "
                             "drop them from the manifest to run here" );

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

  // Platform 4.0 derived output modes. Conflicts fail loudly BEFORE any
  // compute: derived modes collapse head 0's class planes, so multi-head
  // models and uncertainty bands (which need the full class stack on disk)
  // refuse them.
  const RasterOutputMode mode = options.outputMode != RasterOutputMode::Probability
                                  ? options.outputMode
                                  : rasterOutputMode( m_model );
  int writeBands = 0;      // resolved when the writer is created
  int writeType = 6;       // GDT_Float32; Labels/Mask write Byte/UInt16
  float writeNoData = std::numeric_limits<float>::quiet_NaN();
  if ( mode != RasterOutputMode::Probability )
  {
    if ( !uncertainty.empty() )
      throw RSOperatorError( ErrorCode::InvalidInputData,
                             "output.uncertainty requires the full probability stack — "
                             "remove output.format or output.uncertainty" );
    if ( m_model.output.tensorNames.size() > 1 )
      throw RSOperatorError( ErrorCode::InvalidInputData,
                             "derived output.format applies to single-head models; this "
                             "manifest declares " + std::to_string( m_model.output.tensorNames.size() )
                               + " tensor heads" );
    if ( mode == RasterOutputMode::Labels && m_model.postprocess.maskThreshold >= 0.0 )
      throw RSOperatorError( ErrorCode::InvalidInputData,
                             "postprocess.mask_threshold is meaningless with output.format=labels "
                             "(argmax never thresholds) — remove one of the two" );
    switch ( mode )
    {
      case RasterOutputMode::Labels:
      {
        const int classCount = static_cast<int>( m_model.output.classes.size() );
        writeBands = 1;
        writeType = classCount <= 255 ? /*GDT_Byte*/ 1 : /*GDT_UInt16*/ 2;
        writeNoData = classCount <= 255 ? 255.0f : 65535.0f;
        break;
      }
      case RasterOutputMode::Mask:
        writeBands = 1;
        writeType = /*GDT_Byte*/ 1;
        writeNoData = 255.0f;
        break;
      case RasterOutputMode::Confidence:
        writeBands = 1;
        writeType = /*GDT_Float32*/ 6;
        writeNoData = std::numeric_limits<float>::quiet_NaN();
        break;
      case RasterOutputMode::Probability:
        break; // unreachable: handled above
    }
  }

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
  // Staging path for the atomic publish (assigned when the writer is created;
  // empty until then — the publish step runs only on full success).
  QString stagePath;
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
      // NoData fill uses the WRITER's sentinel (NaN for float stacks, 255/65535
      // for derived label/mask rasters — a NaN bit-cast into Byte is garbage).
      cv::Mat nodataPlane( dt.h, dt.w, CV_32FC1, cv::Scalar( writeNoData ) );
      for ( int c = 0; c < stats.outBands; ++c )
      {
        const GdalBlockStream::Tile writeTile{ dt.x, dt.y, dt.w, dt.h, 0, dt.w, dt.h,
                                               currentTileIndex, totalTiles };
        if ( !writer->writeTile( c + 1, writeTile, nodataPlane.ptr<float>() ) )
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
      // Atomic publication (Platform 4.0): the streaming writer stages into a
      // same-directory temp file; on success it is renamed onto the caller's
      // path, so the output path only ever holds a COMPLETE raster — a crash
      // or failure leaves no truncated file that could look like a result.
      const QFileInfo outFi( QString::fromStdString( outputPath ) );
      stagePath = QString::fromStdString( outputPath ) + QStringLiteral( ".tmp~" );
      QDir().mkpath( outFi.absolutePath() );
      QFile::remove( stagePath );
      const int writerBands = mode == RasterOutputMode::Probability ? totalBands : writeBands;
      writer = std::make_unique<GdalStreamingOutput>( stagePath,
                                                      rasterW, rasterH, writerBands, writeType,
                                                      geoTransform, projection );
      if ( !writer->isOpen() )
        throw RSOperatorError( ErrorCode::FileNotWritable, "failed to create output raster: " + outputPath );
      writer->setNoDataValue( static_cast<double>( writeNoData ) );
      stats.outBands = writerBands;
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
        if ( mode == RasterOutputMode::Labels )
        {
          layout = QStringLiteral( "labels:1" );
          // Deterministic palette (analysis-layer convention, ADR 0061) so
          // label rasters render meaningfully without a lookup sidecar.
          QString palette;
          QString names;
          for ( int classId = 0; classId < static_cast<int>( m_model.output.classes.size() ); ++classId )
          {
            const QColor color = rsSynthesizedClassColor( classId );
            if ( classId )
            {
              palette += QLatin1Char( ';' );
              names += QLatin1Char( ';' );
            }
            palette += QString( "%1:%2,%3,%4" ).arg( classId ).arg( color.red() )
                         .arg( color.green() ).arg( color.blue() );
            names += QString::fromStdString( m_model.output.classes[static_cast<std::size_t>( classId )] );
          }
          writer->setMetadataItem( QStringLiteral( "SICNU_CLASS_PALETTE" ), palette );
          writer->setMetadataItem( QStringLiteral( "SICNU_CLASS_NAMES" ), names );
        }
        writer->setMetadataItem( QStringLiteral( "SICNU_OUTPUT_HEADS" ), layout );
      }
    }
    else if ( mode == RasterOutputMode::Probability && totalBands != stats.outBands )
    {
      throw RSOperatorError( ErrorCode::ComputationError,
                             "model output channel count changed mid-run (" + std::to_string( stats.outBands )
                               + " → " + std::to_string( totalBands) + ")" );
    }

    // The writer exists now, so NoData tiles deferred by earlier all-nodata
    // batches can go straight to disk.
    flushDeferredNoData( currentTileIndex );

    // Per-tile class planes of head 0, collected for the derived output
    // modes (labels/mask/confidence) and written as ONE band per tile.
    std::vector<std::vector<cv::Mat>> derivedPlanes(
      batchMats.size() );

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
          if ( mode == RasterOutputMode::Probability && m_model.postprocess.maskThreshold >= 0.0 )
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
          if ( mode == RasterOutputMode::Probability )
          {
            const GdalBlockStream::Tile writeTile{ bt.x, bt.y, bt.w, bt.h, 0, bt.w, bt.h,
                                                   currentTileIndex, totalTiles };
            if ( !writer->writeTile( bandOffset + c + 1, writeTile, plane.ptr<float>() ) )
              throw RSOperatorError( ErrorCode::FileNotWritable, "failed to write output tile at ("
                                     + std::to_string( bt.x ) + ", " + std::to_string( bt.y ) + ")" );
          }
          else if ( h == 0 )
          {
            // Derived modes keep the class planes in memory; ONE derived band
            // is written after the channel loop below.
            derivedPlanes[bi].push_back( std::move( plane ) );
          }
        }
        if ( mode != RasterOutputMode::Probability && h == 0 )
        {
          // Argmax/threshold derivation (Platform 4.0): collapse the class
          // planes of head 0 into one band. Invalid (NaN-restored) pixels
          // stay at the writer's NoData sentinel.
          const std::vector<cv::Mat> &planes = derivedPlanes[bi];
          const int channels = static_cast<int>( planes.size() );
          cv::Mat derived( bt.h, bt.w, CV_32FC1, cv::Scalar( writeNoData ) );
          const float maskThr = m_model.postprocess.maskThreshold >= 0.0
                                  ? static_cast<float>( m_model.postprocess.maskThreshold )
                                  : 0.5f;
          for ( int row = 0; row < bt.h; ++row )
          {
            float *outRow = derived.ptr<float>( row );
            for ( int col = 0; col < bt.w; ++col )
            {
              bool invalid = false;
              int best = 0;
              float bestv = -std::numeric_limits<float>::infinity();
              for ( int c = 0; c < channels; ++c )
              {
                const float v = planes[static_cast<std::size_t>( c )].ptr<float>( row )[col];
                if ( !std::isfinite( v ) )
                {
                  invalid = true;
                  break;
                }
                if ( v > bestv )
                {
                  bestv = v;
                  best = c;
                }
              }
              if ( invalid )
                continue; // stays NoData
              switch ( mode )
              {
                case RasterOutputMode::Labels:
                  outRow[col] = static_cast<float>( best );
                  break;
                case RasterOutputMode::Confidence:
                  outRow[col] = bestv;
                  break;
                case RasterOutputMode::Mask:
                  outRow[col] = channels == 1 ? ( bestv >= maskThr ? 1.0f : 0.0f )
                                              : ( best != 0 ? 1.0f : 0.0f );
                  break;
                case RasterOutputMode::Probability:
                  break; // unreachable
              }
            }
          }
          const GdalBlockStream::Tile writeTile{ bt.x, bt.y, bt.w, bt.h, 0, bt.w, bt.h,
                                                 currentTileIndex, totalTiles };
          if ( !writer->writeTile( 1, writeTile, derived.ptr<float>() ) )
            throw RSOperatorError( ErrorCode::FileNotWritable, "failed to write output tile at ("
                                     + std::to_string( bt.x ) + ", " + std::to_string( bt.y ) + ")" );
          derivedPlanes[bi].clear();
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

    // OOM ladder (Platform 4.0, goal §3): an over-budget batch is retried
    // tile-by-tile — per-tile semantics never change, so the scientific
    // contract holds (only the batching shrinks). At batch=1 an OOM is final
    // and carries the diagnostic. The engine NEVER responds to OOM by
    // shrinking tiles, changing resolution, or altering model semantics.
    try
    {
      flushBatch( tileIndex );
    }
    catch ( const RSOperatorError &e )
    {
      if ( classifyInferenceError( e.what() ) != InferenceFailureKind::OutOfMemory )
        throw;
      if ( batchMats.size() <= 1 )
        throw RSOperatorError(
          ErrorCode::ComputationError,
          std::string( "inference ran out of memory even at batch=1 (tile " )
            + std::to_string( tileSize )
            + " px): free memory or use a smaller model - the engine never alters spatial "
              "resolution or model semantics to fit memory. Original error: "
            + e.what() );
      ++stats.batchReductions;
      // Serial retry: hold the pending batch, flush one tile per forward.
      const std::vector<cv::Mat> pending = std::move( batchMats );
      const std::vector<cv::Mat> pendingMasks = std::move( batchMasks );
      const std::vector<std::pair<int, int>> pendingFed = std::move( batchFedSize );
      const std::vector<CoreTile> pendingCores = std::move( batchCores );
      const std::vector<int> pendingValid = std::move( batchValidPixels );
      batchMats.clear();
      batchMasks.clear();
      batchFedSize.clear();
      batchCores.clear();
      batchValidPixels.clear();
      for ( std::size_t i = 0; i < pending.size(); ++i )
      {
        batchMats.assign( 1, pending[i] );
        batchMasks.assign( 1, pendingMasks[i] );
        batchFedSize.assign( 1, pendingFed[i] );
        batchCores.assign( 1, pendingCores[i] );
        batchValidPixels.assign( 1, pendingValid[i] );
        try
        {
          flushBatch( tileIndex );
        }
        catch ( const RSOperatorError &inner )
        {
          if ( classifyInferenceError( inner.what() ) == InferenceFailureKind::OutOfMemory )
            throw RSOperatorError(
              ErrorCode::ComputationError,
              "inference ran out of memory even at batch=1 (tile " + std::to_string( tileSize )
                + " px): free memory or use a smaller model — the engine never alters spatial "
                  "resolution or model semantics to fit memory. Original error: "
                + inner.what() );
          throw;
        }
      }
    }
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
  // Atomic publish: only a fully written, closed raster is renamed onto the
  // caller's path (same directory — same volume). Windows rename does not
  // overwrite, so the previous result moves to a .prev~ backup first; a
  // failure at ANY step restores it — the caller's output path ends up with
  // either the NEW raster or the OLD one, never nothing and never a torn one.
  const QString finalPath = QString::fromStdString( outputPath );
  const QString backupPath = finalPath + QStringLiteral( ".prev~" );
  QFile::remove( backupPath );
  const bool hadExisting = QFile::exists( finalPath );
  if ( hadExisting && !QFile::rename( finalPath, backupPath ) )
  {
    QFile::remove( stagePath );
    throw RSOperatorError( ErrorCode::FileNotWritable,
                           "failed to back up the previous output before publishing: " + outputPath );
  }
  if ( !QFile::rename( stagePath, finalPath ) )
  {
    QFile::remove( stagePath );
    if ( hadExisting )
      QFile::rename( backupPath, finalPath ); // best-effort restore of the old result
    throw RSOperatorError( ErrorCode::FileNotWritable,
                           "failed to publish output raster to: " + outputPath );
  }
  QFile::remove( backupPath );
  context.reportProgressForced( 1.0, "Tiled inference complete" );
  stats.tilesProcessed = done;
  return stats;
}

// --- Platform 7.0: multimodal / temporal tiled inference --------------------

std::string TileInferenceEngine::gridMismatch( const std::string &primaryPath, int primaryW,
                                               int primaryH, const double *primaryGeoTransform,
                                               const std::string &otherPath, int otherW, int otherH,
                                               const double *otherGeoTransform )
{
  if ( primaryW != otherW || primaryH != otherH )
    return "input grids are not co-registered: '" + primaryPath + "' is "
             + std::to_string( primaryW ) + "x" + std::to_string( primaryH ) + " but '" + otherPath
             + "' is " + std::to_string( otherW ) + "x" + std::to_string( otherH )
             + " — align the rasters first (the runtime never warps implicitly)";
  if ( primaryGeoTransform && otherGeoTransform )
  {
    for ( int i = 0; i < 6; ++i )
    {
      if ( std::abs( primaryGeoTransform[i] - otherGeoTransform[i] ) > 1e-6 )
        return "input grids are not co-registered: '" + primaryPath + "' and '" + otherPath
                 + "' carry different geotransforms — align the rasters first (warp/resample "
                   "through the geospatial raster_convert seam), the runtime refuses misaligned "
                   "feeds instead of guessing";
    }
  }
  return {};
}

TileInferenceStats TileInferenceEngine::runMultiInput( const std::vector<NamedRasterFeed> &feeds,
                                                       const std::string &outputPath,
                                                       RSOperatorContext &context,
                                                       const TileInferenceRunOptions &options )
{
  if ( !m_runtime )
    throw RSOperatorError( ErrorCode::ComputationError, "tile inference engine has no runtime session" );
  if ( feeds.empty() )
    throw RSOperatorError( ErrorCode::InvalidParameter, "multi-input inference needs at least one feed" );

  // Head resolution: identical contract to run() (manifest tensor_names vs
  // the graph's own outputs; advisory when the runtime cannot enumerate).
  std::vector<std::string> headNames;
  if ( !m_model.output.tensorNames.empty() )
  {
    if ( const std::string missing = missingOutputTensor( m_model, m_runtime->outputTensorNames() );
         !missing.empty() )
      throw RSOperatorError( ErrorCode::InvalidInputData, missing );
    headNames = m_runtime->outputTensorNames().empty() ? std::vector<std::string>{ m_model.output.tensorNames.front() }
                                                       : m_model.output.tensorNames;
  }
  else
    headNames = { std::string() };
  const std::string uncertainty = uncertaintyMethod( m_model );

  // The multimodal path writes the full probability stack: derived formats
  // collapse head 0's class planes and are a single-head single-input
  // convenience — refusing here is the typed, honest answer.
  if ( rasterOutputMode( m_model ) != RasterOutputMode::Probability
       || options.outputMode != RasterOutputMode::Probability )
    throw RSOperatorError( ErrorCode::InvalidInputData,
                           "multi-input models write the probability stack only — derived "
                             "output.format (labels/mask/confidence) is a single-head convenience; "
                             "derive from the stack afterwards or run the single-input engine" );
  if ( m_model.preprocess.resize == "to_input" )
    throw RSOperatorError( ErrorCode::InvalidInputData,
                           "preprocess.resize=to_input is not supported for multi-input models — "
                             "declare aligned grids per input (alignment=reference) instead" );

  // Map feeds onto the manifest input contracts: by NAME (the binding
  // document) or positionally when the feed omits its name. Duplicate names
  // refuse — ambiguity is never resolved silently.
  if ( feeds.size() != m_model.inputs.size() )
    throw RSOperatorError( ErrorCode::InvalidParameter,
                           "model declares " + std::to_string( m_model.inputs.size() )
                             + " inputs but " + std::to_string( feeds.size() )
                             + " feeds were given" );
  std::vector<std::size_t> feedContract( feeds.size() );
  for ( std::size_t f = 0; f < feeds.size(); ++f )
  {
    if ( feeds[f].name.empty() )
    {
      feedContract[f] = f; // positional
      continue;
    }
    bool matched = false;
    for ( std::size_t c = 0; c < m_model.inputs.size(); ++c )
    {
      if ( m_model.inputs[c].name == feeds[f].name )
      {
        if ( std::find( feedContract.begin(), feedContract.begin() + f, c ) != feedContract.begin() + f )
          throw RSOperatorError( ErrorCode::InvalidParameter,
                                 "two feeds share the input name '" + feeds[f].name + "'" );
        feedContract[f] = c;
        matched = true;
        break;
      }
    }
    if ( !matched )
      throw RSOperatorError( ErrorCode::InvalidInputData,
                             "feed '" + feeds[f].name
                               + "' matches no declared model input (declared: "
                               + ( m_model.inputs.empty() || m_model.inputs[0].name.empty()
                                     ? std::string( "<unnamed>" )
                                     : m_model.inputs[0].name )
                               + "...)" );
  }

  const ModelPreprocessContract &pre = m_model.preprocess;
  const bool meanStd = pre.normalize == "mean_std";
  const bool hasClamp = !std::isnan( pre.clampMin ) || !std::isnan( pre.clampMax );

  // Open every feed's frames; validate the temporal contract and co-registration.
  struct FeedReader
  {
    std::vector<std::unique_ptr<GdalDatasetWrapper>> frames; // per timestep
    std::vector<int> bands;
    int channels = 0;        // bands × temporal length (the fed channel count)
    const ModelInputContract *contract = nullptr;
    std::vector<float> sentinel;
    std::vector<bool> hasSentinel;
    std::vector<float> window; // reused per-tile buffer
  };
  std::vector<FeedReader> readers( feeds.size() );

  GdalDatasetWrapper primary;
  if ( !primary.open( QString::fromStdString( feeds[0].paths.at( 0 ) ) ) )
    throw RSOperatorError( ErrorCode::GdalError,
                           "failed to open primary input raster: " + feeds[0].paths[0] );
  const int rasterW = primary.width();
  const int rasterH = primary.height();
  if ( rasterW <= 0 || rasterH <= 0 )
    throw RSOperatorError( ErrorCode::InvalidInputData, "input raster is empty: " + feeds[0].paths[0] );
  const std::array<double, 6> primaryGt = primary.geoTransform();

  for ( std::size_t f = 0; f < feeds.size(); ++f )
  {
    FeedReader &reader = readers[f];
    reader.contract = &m_model.inputs[feedContract[f]];
    const ModelInputContract &contract = *reader.contract;

    const std::size_t declaredFrames =
      contract.temporalLength > 0 ? static_cast<std::size_t>( contract.temporalLength ) : 1u;
    const std::string missingPolicy =
      contract.missingTimestep.empty() ? "refuse" : contract.missingTimestep;
    if ( feeds[f].paths.size() > declaredFrames )
      throw RSOperatorError( ErrorCode::InvalidInputData,
                             "feed '" + feeds[f].name + "' provides "
                               + std::to_string( feeds[f].paths.size() ) + " frames but the input "
                               "contract declares temporal_length "
                               + std::to_string( contract.temporalLength ) );
    if ( feeds[f].paths.size() < declaredFrames && missingPolicy != "zero" )
      throw RSOperatorError( ErrorCode::InvalidInputData,
                             "feed '" + feeds[f].name + "' provides "
                               + std::to_string( feeds[f].paths.size() ) + " of "
                               + std::to_string( declaredFrames )
                               + " temporal frames and missing_timestep=refuse (declare "
                                 "missing_timestep=zero to fill missing frames explicitly)" );
    // Zero-filled frames beyond the provided paths materialize as absent
    // datasets — the read path substitutes zeros for them.

    // Band selection against the FIRST provided frame.
    const std::string &firstPath = feeds[f].paths.front();
    std::vector<int> bandList = feeds[f].bands;
    {
      GdalDatasetWrapper probe;
      if ( !probe.open( QString::fromStdString( firstPath ) ) )
        throw RSOperatorError( ErrorCode::GdalError, "failed to open input raster: " + firstPath );
      if ( probe.width() != rasterW || probe.height() != rasterH )
      {
        const std::array<double, 6> gt = probe.geoTransform();
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               gridMismatch( feeds[0].paths[0], rasterW, rasterH, primaryGt.data(),
                                             firstPath, probe.width(), probe.height(), gt.data() ) );
      }
      const std::array<double, 6> gt = probe.geoTransform();
      if ( f > 0 )
      {
        if ( const std::string grid = gridMismatch( feeds[0].paths[0], rasterW, rasterH,
                                                    primaryGt.data(), firstPath, rasterW, rasterH,
                                                    gt.data() );
             !grid.empty() )
          throw RSOperatorError( ErrorCode::InvalidInputData, grid );
      }
      const int rasterBands = probe.bandCount();
      if ( rasterBands <= 0 )
        throw RSOperatorError( ErrorCode::InvalidInputData, "input raster is empty: " + firstPath );
      if ( bandList.empty() )
      {
        bandList.resize( rasterBands );
        for ( int i = 0; i < rasterBands; ++i )
          bandList[static_cast<std::size_t>( i )] = i + 1;
      }
      else
      {
        for ( int b : bandList )
          if ( b < 1 || b > rasterBands )
            throw RSOperatorError( ErrorCode::InvalidParameter,
                                   "feed '" + feeds[f].name + "': band "
                                     + std::to_string( b ) + " out of range (1.."
                                     + std::to_string( rasterBands ) + ")" );
      }
      // Dtype contract per feed: each input contract's declared dtype must
      // match the actual GDAL type of every band fed from that feed (same
      // semantics as the single-input engine's #632/#705 check).
      if ( !contract.dtype.empty() )
      {
        static const std::map<std::string, int> kAccepted = {
          { "float32", GDT_Float32 }, { "float64", GDT_Float64 },
          { "float16", GDT_Float32 }, { "uint16", GDT_UInt16 },
          { "int16", GDT_Int16 },     { "uint8", GDT_Byte },
          { "int32", GDT_Int32 },     { "uint32", GDT_UInt32 },
        };
        const auto accepted = kAccepted.find( contract.dtype );
        if ( accepted == kAccepted.end() )
          throw RSOperatorError( ErrorCode::InvalidInputData,
                                 "feed '" + feeds[f].name + "': manifest declares unsupported "
                                   "input dtype '" + contract.dtype + "'" );
        for ( int band : bandList )
        {
          if ( probe.bandDataType( band ) != accepted->second )
            throw RSOperatorError( ErrorCode::InvalidInputData,
                                   "feed '" + feeds[f].name + "': manifest requires input dtype '"
                                     + contract.dtype + "' but band " + std::to_string( band )
                                     + " has GDAL type "
                                     + std::to_string( probe.bandDataType( band ) )
                                     + " (convert the raster or update the manifest)" );
        }
      }
      if ( !contract.bandRoles.empty()
           && contract.bandRoles.size() != bandList.size() )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "feed '" + feeds[f].name + "': manifest declares "
                                 + std::to_string( contract.bandRoles.size() ) + " band roles but "
                                 + std::to_string( bandList.size() ) + " bands are fed" );
      if ( meanStd && !pre.mean.empty() && pre.mean.size() != bandList.size() )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "preprocess.mean declares " + std::to_string( pre.mean.size() )
                                 + " channels but " + std::to_string( bandList.size() )
                                 + " bands are fed" );
      if ( meanStd && !pre.stdv.empty() && pre.stdv.size() != bandList.size() )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "preprocess.std declares " + std::to_string( pre.stdv.size() )
                                 + " channels but " + std::to_string( bandList.size() )
                                 + " bands are fed" );
    }
    reader.bands = bandList;
    reader.channels = static_cast<int>( bandList.size() * declaredFrames );

    // Open all provided frames.
    reader.frames.resize( declaredFrames );
    for ( std::size_t t = 0; t < feeds[f].paths.size(); ++t )
    {
      auto frame = std::make_unique<GdalDatasetWrapper>();
      if ( !frame->open( QString::fromStdString( feeds[f].paths[t] ) ) )
        throw RSOperatorError( ErrorCode::GdalError,
                               "failed to open input raster: " + feeds[f].paths[t] );
      reader.frames[t] = std::move( frame );
    }
    // Missing frames stay null → zero-filled below (missing_timestep=zero).

    // Per-band NoData sentinels from the first frame.
    reader.sentinel.assign( bandList.size(), 0.0f );
    reader.hasSentinel.assign( bandList.size(), false );
    if ( reader.frames[0] )
    {
      for ( std::size_t i = 0; i < bandList.size(); ++i )
      {
        bool has = false;
        const double nd = reader.frames[0]->bandNoDataValue( bandList[i], &has );
        if ( has && std::isfinite( nd ) )
        {
          reader.sentinel[i] = static_cast<float>( nd );
          reader.hasSentinel[i] = true;
        }
      }
    }
  }

  // Tile geometry: primary grid authority; fed channels for the batch budget
  // come from the PRIMARY feed (the others scale linearly and the admission
  // seam already accounted the session).
  const int tileSize = std::min( effectiveTileSize( m_model ), std::max( rasterW, rasterH ) );
  const int halo = std::max( 0, effectiveHalo( m_model ) );
  const int pad = std::max( 0, pre.pad );
  int batchSize = effectiveBatchSize( m_model, ModelRuntimeRegistry::instance().hardware(),
                                      tileSize, readers[0].channels );
  if ( options.batchSizeOverride > 0 )
    batchSize = std::min( batchSize, options.batchSizeOverride );
  batchSize = std::max( 1, batchSize );
  const int maxWin = tileSize + 2 * halo + 2 * pad;

  std::vector<CoreTile> core;
  for ( int y = 0; y < rasterH; y += tileSize )
    for ( int x = 0; x < rasterW; x += tileSize )
      core.push_back( CoreTile{ x, y, std::min( tileSize, rasterW - x ), std::min( tileSize, rasterH - y ) } );
  const int totalTiles = static_cast<int>( core.size() );
  const double minCoverage = std::clamp( m_model.tiling.minValidCoverage, 0.0, 1.0 );

  TileInferenceStats stats;
  stats.tileSize = tileSize;
  stats.halo = halo;
  stats.batchSize = batchSize;
  stats.tilesPlanned = totalTiles;
  stats.outWidth = rasterW;
  stats.outHeight = rasterH;

  // Reused per-feed window buffers (fed size incl. halo; pad applied after
  // preprocessing so the zero pad cannot be normalized over).
  for ( auto &reader : readers )
    reader.window.assign( static_cast<std::size_t>( maxWin ) * maxWin * reader.bands.size(), 0.0f );

  // Pending batch across ALL feeds.
  std::vector<std::vector<cv::Mat>> batchByFeed( feeds.size() );
  std::vector<cv::Mat> batchMasks;
  std::vector<std::pair<int, int>> batchFedSize;
  std::vector<CoreTile> batchCores;
  std::vector<int> batchValidPixels;
  std::vector<CoreTile> deferredNoData;

  std::unique_ptr<GdalStreamingOutput> writer;
  QString stagePath;
  int done = 0;
  int skipped = 0;

  // Preprocess one RAW window in place: sentinel→NaN, non-finite→0,
  // normalize, clamp. Channel-aware exactly like the single-input engine.
  auto preprocessWindow = [&]( FeedReader &reader, std::vector<float> &buffer, int winW,
                               int winH ) {
    const std::size_t bandCount = reader.bands.size();
    const std::size_t totalFloats = static_cast<std::size_t>( winH ) * winW * bandCount;
    for ( std::size_t i = 0; i < totalFloats; ++i )
    {
      const std::size_t b = i % bandCount;
      if ( reader.hasSentinel[b] && buffer[i] == reader.sentinel[b] )
        buffer[i] = std::numeric_limits<float>::quiet_NaN();
      if ( !std::isfinite( buffer[i] ) )
        buffer[i] = 0.0f; // nodata_policy "zero"
    }
    if ( meanStd || ( pre.normalize == "linear" && pre.scale != 1.0 ) )
    {
      const double *meanArr = meanStd && !pre.mean.empty() ? pre.mean.data() : nullptr;
      const double *stdArr = meanStd && !pre.stdv.empty() ? pre.stdv.data() : nullptr;
      for ( std::size_t i = 0; i < totalFloats; ++i )
      {
        const std::size_t c = i % bandCount;
        double v = buffer[i];
        if ( meanStd )
        {
          if ( meanArr )
            v -= meanArr[c];
          if ( stdArr && stdArr[c] > 0.0 )
            v /= stdArr[c];
        }
        v *= pre.scale;
        if ( hasClamp )
        {
          if ( !std::isnan( pre.clampMin ) && v < pre.clampMin )
            v = pre.clampMin;
          if ( !std::isnan( pre.clampMax ) && v > pre.clampMax )
            v = pre.clampMax;
        }
        buffer[i] = static_cast<float>( v );
      }
    }
    else if ( hasClamp )
    {
      for ( std::size_t i = 0; i < totalFloats; ++i )
      {
        double v = buffer[i];
        if ( !std::isnan( pre.clampMin ) && v < pre.clampMin )
          v = pre.clampMin;
        if ( !std::isnan( pre.clampMax ) && v > pre.clampMax )
          v = pre.clampMax;
        buffer[i] = static_cast<float>( v );
      }
    }
  };

  // One forward pass + stitch + write for the pending batch. The OOM ladder
  // and the atomic publish share the single-input semantics exactly.
  auto flushBatch = [&]( int currentTileIndex ) {
    context.throwIfCancelled();

    // Build one NCHW blob per feed: (B, channels, H, W).
    const int B = static_cast<int>( batchCores.size() );
    const int fedW = batchFedSize[0].first;
    const int fedH = batchFedSize[0].second;
    std::vector<NamedTensor> inputs( feeds.size() );
    for ( std::size_t f = 0; f < feeds.size(); ++f )
    {
      FeedReader &reader = readers[f];
      const int C = reader.channels;
      std::vector<cv::Mat> &mats = batchByFeed[f];
      const int dims[4] = { B, C, fedH, fedW };
      cv::Mat blob( 4, dims, CV_32F );
      blob.setTo( 0 );
      for ( int b = 0; b < B; ++b )
      {
        const cv::Mat &tileMat = mats[static_cast<std::size_t>( b )]; // HWC, C channels
        std::vector<cv::Mat> channels;
        cv::split( tileMat, channels );
        for ( int c = 0; c < C; ++c )
        {
          const cv::Mat &ch = channels[static_cast<std::size_t>( c )];
          float *dst = blob.ptr<float>( b, c, 0 );
          for ( int y = 0; y < fedH; ++y )
            std::memcpy( dst + static_cast<std::size_t>( y ) * fedW, ch.ptr<float>( y ),
                         static_cast<std::size_t>( fedW ) * sizeof( float ) );
        }
      }
      inputs[f] = NamedTensor{ reader.contract->name, TensorBlob::fromMat( blob ) };
    }

    // One named forward for the whole batch (named bind; all heads).
    std::vector<NamedTensor> outputs;
    try
    {
      outputs = m_runtime->inferNamed( inputs, headNames.front().empty() ? std::vector<std::string>{} : headNames );
    }
    catch ( const RSOperatorError & )
    {
      throw;
    }
    catch ( const std::exception &e )
    {
      throw RSOperatorError( ErrorCode::ComputationError,
                             std::string( "multi-input forward pass failed: " ) + e.what() );
    }

    // Head outputs: named results first, then graph order for unnamed ones.
    std::vector<cv::Mat> headOutputs;
    std::vector<std::string> resolvedHeadNames;
    for ( auto &nt : outputs )
    {
      if ( nt.second.rank() != 4 )
        throw RSOperatorError( ErrorCode::InvalidInputData,
                               "model output '" + nt.first + "' is rank "
                                 + std::to_string( nt.second.rank() )
                                 + " — the raster writer stitches rank-4 heads" );
      cv::Mat mat = nt.second.toMat();
      if ( const std::string typeError = outputTypeMismatch( mat.type(), nt.first ); !typeError.empty() )
        throw RSOperatorError( ErrorCode::InvalidInputData, typeError );
      if ( mat.size[1] <= 0 )
        throw RSOperatorError( ErrorCode::ComputationError, "model output has no channels" );
      if ( const std::string classesError = classesChannelMismatch( m_model, mat.size[1], nt.first );
           !classesError.empty() )
        throw RSOperatorError( ErrorCode::InvalidInputData, classesError );
      headOutputs.push_back( std::move( mat ) );
      resolvedHeadNames.push_back( nt.first );
    }
    if ( headOutputs.empty() )
      throw RSOperatorError( ErrorCode::ComputationError, "model produced no usable output" );
    if ( resolvedHeadNames.size() < headNames.size() && !headNames.front().empty() )
      throw RSOperatorError( ErrorCode::InvalidInputData,
                             "model produced " + std::to_string( resolvedHeadNames.size() )
                               + " heads but the manifest declares "
                               + std::to_string( headNames.size() ) );

    // Band layout: identical to run() — all heads in order + one uncertainty
    // band after the first head with >= 2 channels.
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
      const QFileInfo outFi( QString::fromStdString( outputPath ) );
      stagePath = QString::fromStdString( outputPath ) + QStringLiteral( ".tmp~" );
      QDir().mkpath( outFi.absolutePath() );
      QFile::remove( stagePath );
      writer = std::make_unique<GdalStreamingOutput>( stagePath, rasterW, rasterH, totalBands,
                                                      /*GDT_Float32*/ 6,
                                                      primary.geoTransform(), primary.projection() );
      if ( !writer->isOpen() )
        throw RSOperatorError( ErrorCode::FileNotWritable,
                               "failed to create output raster: " + outputPath );
      writer->setNoDataValue( std::numeric_limits<float>::quiet_NaN() );
      stats.outBands = totalBands;
      stats.headChannels = headChannelList;
      if ( uncertaintyHeadIndex >= 0 )
        stats.headChannels[static_cast<std::size_t>( uncertaintyHeadIndex )] += 1;
      QString layout;
      for ( std::size_t h = 0; h < resolvedHeadNames.size(); ++h )
      {
        if ( h )
          layout += QLatin1Char( ',' );
        const QString name = resolvedHeadNames[h].empty() ? QStringLiteral( "default" )
                                                          : QString::fromStdString( resolvedHeadNames[h] );
        layout += QString( "%1:%2" ).arg( name ).arg( headChannelList[h] );
      }
      if ( uncertaintyHeadIndex >= 0 )
        layout += QString( ",uncertainty:%1" ).arg( QString::fromStdString( uncertainty ) );
      writer->setMetadataItem( QStringLiteral( "SICNU_OUTPUT_HEADS" ), layout );
    }
    else if ( totalBands != stats.outBands )
    {
      throw RSOperatorError( ErrorCode::ComputationError,
                             "model output channel count changed mid-run (" +
                               std::to_string( stats.outBands ) + " → " + std::to_string( totalBands )
                               + ")" );
    }

    // Stitch: per head, per tile, per channel — the same crop/resize/restore
    // math as the single-input engine (grid-preserving ⇒ crop halo+pad).
    int bandOffset = 0;
    for ( std::size_t h = 0; h < headOutputs.size(); ++h )
    {
      const cv::Mat &output = headOutputs[h];
      const int outChannels = output.size[1];
      const int outH = output.size[2];
      const int outW = output.size[3];
      const bool isUncertaintyHead = ( uncertaintyHeadIndex == static_cast<int>( h ) );
      const cv::Mat flat =
        output.reshape( 1, std::vector<int>{ B * outChannels, outH * outW } );
      for ( int bi = 0; bi < B; ++bi )
      {
        const CoreTile &bt = batchCores[static_cast<std::size_t>( bi )];
        const int tileFedW = batchFedSize[static_cast<std::size_t>( bi )].first;
        const int tileFedH = batchFedSize[static_cast<std::size_t>( bi )].second;
        std::vector<cv::Mat> headPlanes;
        if ( isUncertaintyHead )
          headPlanes.reserve( static_cast<std::size_t>( outChannels ) );
        for ( int c = 0; c < outChannels; ++c )
        {
          cv::Mat plane = flat.row( bi * outChannels + c ).reshape( 1, outH ).clone();
          if ( outW == tileFedW && outH == tileFedH )
          {
            // Grid-preserving: crop halo+pad border away.
            const int margin = halo + pad;
            if ( margin > 0 )
              plane = plane( cv::Range( margin, margin + bt.h ), cv::Range( margin, margin + bt.w ) ).clone();
          }
          else if ( outW != bt.w || outH != bt.h )
          {
            cv::resize( plane, plane, cv::Size( bt.w, bt.h ), 0, 0, cv::INTER_LINEAR );
          }
          if ( isUncertaintyHead )
            headPlanes.push_back( plane );
          if ( m_model.postprocess.maskThreshold >= 0.0 )
          {
            const float thr = static_cast<float>( m_model.postprocess.maskThreshold );
            cv::Mat mask = plane >= thr;
            cv::Mat thresholded;
            mask.convertTo( thresholded, CV_32F, 1.0 / 255.0 );
            plane = thresholded;
          }
          // Restore NoData where every feed's every band was invalid.
          const cv::Mat &tileMask = batchMasks[static_cast<std::size_t>( bi )];
          for ( int row = 0; row < bt.h; ++row )
          {
            const uchar *maskRow = tileMask.ptr<uchar>( row );
            float *outRow = plane.ptr<float>( row );
            for ( int col = 0; col < bt.w; ++col )
              if ( maskRow[col] )
                outRow[col] = std::numeric_limits<float>::quiet_NaN();
          }
          const GdalBlockStream::Tile writeTile{ bt.x, bt.y, bt.w, bt.h, 0, bt.w, bt.h,
                                                 currentTileIndex, totalTiles };
          if ( !writer->writeTile( bandOffset + c + 1, writeTile, plane.ptr<float>() ) )
            throw RSOperatorError( ErrorCode::FileNotWritable,
                                   "failed to write output tile at (" + std::to_string( bt.x )
                                     + ", " + std::to_string( bt.y ) + ")" );
        }
        if ( isUncertaintyHead && uncertaintyBandOffset >= 0 )
        {
          cv::Mat unc = headUncertainty( headPlanes, uncertainty );
          const GdalBlockStream::Tile writeTile{ bt.x, bt.y, bt.w, bt.h, 0, bt.w, bt.h,
                                                 currentTileIndex, totalTiles };
          if ( !writer->writeTile( uncertaintyBandOffset + 1, writeTile, unc.ptr<float>() ) )
            throw RSOperatorError( ErrorCode::FileNotWritable, "failed to write uncertainty tile" );
        }
      }
      bandOffset += outChannels;
    }
    done += B;
    for ( auto &mats : batchByFeed )
      mats.clear();
    batchMasks.clear();
    batchFedSize.clear();
    batchCores.clear();
    batchValidPixels.clear();
    context.reportProgress( static_cast<double>( done + skipped ) / static_cast<double>( totalTiles ),
                            "Tiled multi-input inference: " + std::to_string( done + skipped ) + "/"
                              + std::to_string( totalTiles ) );
  };

  auto flushDeferredNoDataMulti = [&]( int currentTileIndex ) {
    if ( !writer || deferredNoData.empty() )
      return;
    while ( !deferredNoData.empty() && deferredNoData.front().y + deferredNoData.front().h
              <= currentTileIndex * 0 + rasterH ) // writer exists: flush all queued
    {
      const CoreTile &bt = deferredNoData.front();
      cv::Mat nanTile( bt.h, bt.w, CV_32F, std::numeric_limits<float>::quiet_NaN() );
      for ( int band = 1; band <= stats.outBands; ++band )
      {
        const GdalBlockStream::Tile writeTile{ bt.x, bt.y, bt.w, bt.h, 0, bt.w, bt.h,
                                               currentTileIndex, totalTiles };
        if ( !writer->writeTile( band, writeTile, nanTile.ptr<float>() ) )
          throw RSOperatorError( ErrorCode::FileNotWritable, "failed to write NoData tile" );
      }
      deferredNoData.erase( deferredNoData.begin() );
    }
  };

  try
  {
    for ( int tileIndex = 0; tileIndex < totalTiles; ++tileIndex )
    {
      context.throwIfCancelled();
      const CoreTile &t = core[static_cast<std::size_t>( tileIndex )];
      const int winX = t.x - halo;
      const int winY = t.y - halo;
      const int winW = t.w + 2 * halo;
      const int winH = t.h + 2 * halo;

      // Per-feed window reads for this tile. A core pixel is VALID when any
      // feed sees a finite value in any channel — NoData is only what nothing
      // sees. The mask is rebuilt from the prepared tile mats so the
      // halo/pad layout has a single source of truth.
      cv::Mat invalidMask( t.h, t.w, CV_8UC1, cv::Scalar( 1 ) );
      int validPixels = 0;
      std::vector<cv::Mat> tileMats( feeds.size() );
      for ( std::size_t f = 0; f < feeds.size(); ++f )
      {
        FeedReader &reader = readers[f];
        const int bandCount = static_cast<int>( reader.bands.size() );
        const std::size_t declaredFrames = reader.frames.size();
        // Stacked HWC buffer: (frames × bands) channels, zeros for missing
        // frames (missing_timestep=zero) and preprocessed data otherwise.
        std::vector<float> stacked( static_cast<std::size_t>( winH ) * winW * reader.channels, 0.0f );
        for ( std::size_t frame = 0; frame < declaredFrames; ++frame )
        {
          GdalDatasetWrapper *ds = reader.frames[frame].get();
          if ( !ds )
            continue; // missing frame stays zero-filled
          if ( !readBipWindow( *ds, reader.bands, winX, winY, winW, winH, reader.window.data() ) )
            throw RSOperatorError( ErrorCode::GdalError,
                                   "failed to read tile window at (" + std::to_string( t.x ) + ", "
                                     + std::to_string( t.y ) + ") of feed '" + reader.contract->name
                                     + "'" );
          // Validity from the RAW window BEFORE preprocessing zero-fills it:
          // a core pixel is valid when any band of any frame of any feed sees
          // a finite non-sentinel value (NoData is only what nothing sees).
          {
            const std::size_t windowStride = static_cast<std::size_t>( winW ) * bandCount;
            for ( int row = 0; row < t.h; ++row )
            {
              const float *winRow = reader.window.data()
                                      + static_cast<std::size_t>( row + halo ) * windowStride
                                      + static_cast<std::size_t>( halo ) * bandCount;
              uchar *maskRow = invalidMask.ptr<uchar>( row );
              for ( int col = 0; col < t.w; ++col )
              {
                if ( !maskRow[col] )
                  continue;
                const float *px = winRow + static_cast<std::size_t>( col ) * bandCount;
                for ( int c = 0; c < bandCount; ++c )
                {
                  const float v = px[c];
                  if ( std::isfinite( v ) && !( reader.hasSentinel[static_cast<std::size_t>( c )]
                                                && v == reader.sentinel[static_cast<std::size_t>( c )] ) )
                  {
                    maskRow[col] = 0;
                    break;
                  }
                }
              }
            }
          }
          preprocessWindow( reader, reader.window, winW, winH );
          const std::size_t frameBase = frame * static_cast<std::size_t>( bandCount );
          for ( int row = 0; row < winH; ++row )
          {
            for ( int col = 0; col < winW; ++col )
            {
              for ( int c = 0; c < bandCount; ++c )
              {
                stacked[( static_cast<std::size_t>( row ) * winW + col ) * reader.channels
                        + frameBase + static_cast<std::size_t>( c )] =
                  reader.window[( static_cast<std::size_t>( row ) * winW + col ) * bandCount
                                 + static_cast<std::size_t>( c )];
              }
            }
          }
        }
        // Symmetric zero pad AFTER preprocessing (so the pad cannot be
        // normalized over): the fed window grows by pad on every side.
        cv::Mat stackedHwc( winH, winW, CV_32FC( reader.channels ), stacked.data() );
        cv::Mat fedMat = stackedHwc.clone();
        if ( pad > 0 )
        {
          cv::Mat padded( winH + 2 * pad, winW + 2 * pad, CV_32FC( reader.channels ),
                          cv::Scalar( 0.0 ) );
          stackedHwc.copyTo( padded( cv::Rect( pad, pad, winW, winH ) ) );
          fedMat = padded;
        }
        tileMats[f] = std::move( fedMat );
      }
      // Aggregate valid count for the coverage gate / all-nodata skip.
      for ( int row = 0; row < t.h; ++row )
      {
        const uchar *maskRow = invalidMask.ptr<uchar>( row );
        for ( int col = 0; col < t.w; ++col )
          if ( !maskRow[col] )
            ++validPixels;
      }

      for ( std::size_t f = 0; f < feeds.size(); ++f )
        batchByFeed[f].push_back( tileMats[f] );
      batchCores.push_back( t );
      batchMasks.push_back( invalidMask.clone() );
      batchFedSize.emplace_back( winW + 2 * pad, winH + 2 * pad );
      batchValidPixels.push_back( validPixels );

      const bool batchFull =
        static_cast<int>( batchCores.size() ) >= batchSize || tileIndex == totalTiles - 1;
      if ( !batchFull )
        continue;

      // Coverage gate + all-nodata skip (Platform 7.0 / #705): skipped tiles
      // become NoData rows — never a resolution change, never a model change.
      const double coverage =
        static_cast<double>( validPixels ) / static_cast<double>( std::max( 1, t.w * t.h ) );
      if ( TileInferenceEngine::batchIsAllNoData( batchValidPixels )
           || ( minCoverage > 0.0 && coverage < minCoverage ) )
      {
        skipped += static_cast<int>( batchCores.size() );
        stats.tilesSkippedNoData += static_cast<int>( batchCores.size() );
        deferredNoData.insert( deferredNoData.end(), batchCores.begin(), batchCores.end() );
        for ( auto &mats : batchByFeed )
          mats.clear();
        batchMasks.clear();
        batchFedSize.clear();
        batchCores.clear();
        batchValidPixels.clear();
        context.reportProgress(
          static_cast<double>( done + skipped ) / static_cast<double>( totalTiles ),
          "Tiled multi-input inference: " + std::to_string( done + skipped ) + "/"
            + std::to_string( totalTiles ) );
        continue;
      }

      // OOM ladder: identical semantics — halve to serial, never shrink
      // tiles, never change the model.
      try
      {
        flushBatch( tileIndex );
      }
      catch ( const RSOperatorError &e )
      {
        if ( classifyInferenceError( e.what() ) != InferenceFailureKind::OutOfMemory )
          throw;
        if ( batchCores.size() <= 1 )
          throw RSOperatorError(
            ErrorCode::ComputationError,
            std::string( "inference ran out of memory even at batch=1 (tile " )
              + std::to_string( tileSize )
              + " px): free memory or use a smaller model - the engine never alters spatial "
                "resolution or model semantics to fit memory. Original error: "
              + e.what() );
        ++stats.batchReductions;
        const std::vector<std::vector<cv::Mat>> pending = std::move( batchByFeed );
        const std::vector<cv::Mat> pendingMasks = std::move( batchMasks );
        const std::vector<std::pair<int, int>> pendingFed = std::move( batchFedSize );
        const std::vector<CoreTile> pendingCores = std::move( batchCores );
        const std::vector<int> pendingValid = std::move( batchValidPixels );
        batchByFeed.assign( feeds.size(), {} );
        batchMasks.clear();
        batchFedSize.clear();
        batchCores.clear();
        batchValidPixels.clear();
        for ( std::size_t i = 0; i < pendingCores.size(); ++i )
        {
          for ( std::size_t f = 0; f < feeds.size(); ++f )
            batchByFeed[f].assign( 1, pending[f][i] );
          batchMasks.assign( 1, pendingMasks[i] );
          batchFedSize.assign( 1, pendingFed[i] );
          batchCores.assign( 1, pendingCores[i] );
          batchValidPixels.assign( 1, pendingValid[i] );
          try
          {
            flushBatch( tileIndex );
          }
          catch ( const RSOperatorError &inner )
          {
            if ( classifyInferenceError( inner.what() ) == InferenceFailureKind::OutOfMemory )
              throw RSOperatorError(
                ErrorCode::ComputationError,
                "inference ran out of memory even at batch=1 (tile " + std::to_string( tileSize )
                  + " px): free memory or use a smaller model — the engine never alters spatial "
                    "resolution or model semantics to fit memory. Original error: "
                  + inner.what() );
            throw;
          }
        }
      }
      context.reportProgress( static_cast<double>( done + skipped ) / static_cast<double>( totalTiles ),
                              "Tiled multi-input inference: " + std::to_string( done + skipped )
                                + "/" + std::to_string( totalTiles ) );
    }

    if ( !deferredNoData.empty() && !writer )
    {
      // Whole-raster NoData: one zero probe establishes the output shape.
      const CoreTile probeTile = deferredNoData.front();
      deferredNoData.erase( deferredNoData.begin() );
      const int probeFedW = probeTile.w + 2 * halo + 2 * pad;
      const int probeFedH = probeTile.h + 2 * halo + 2 * pad;
      for ( std::size_t f = 0; f < feeds.size(); ++f )
        batchByFeed[f].push_back(
          cv::Mat( probeFedH, probeFedW, CV_32FC( readers[f].channels ), cv::Scalar( 0.0 ) ) );
      batchMasks.push_back( cv::Mat( probeTile.h, probeTile.w, CV_8UC1, cv::Scalar( 1 ) ) );
      batchFedSize.emplace_back( probeFedW, probeFedH );
      batchCores.push_back( probeTile );
      batchValidPixels.push_back( 0 );
      flushBatch( totalTiles - 1 );
    }
    flushDeferredNoDataMulti( totalTiles - 1 );
  }
  catch ( ... )
  {
    if ( writer )
      writer->removeOutput();
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
  const QString finalPath = QString::fromStdString( outputPath );
  const QString backupPath = finalPath + QStringLiteral( ".prev~" );
  QFile::remove( backupPath );
  const bool hadExisting = QFile::exists( finalPath );
  if ( hadExisting && !QFile::rename( finalPath, backupPath ) )
  {
    QFile::remove( stagePath );
    throw RSOperatorError( ErrorCode::FileNotWritable,
                           "failed to back up the previous output before publishing: " + outputPath );
  }
  if ( !QFile::rename( stagePath, finalPath ) )
  {
    QFile::remove( stagePath );
    if ( hadExisting )
      QFile::rename( backupPath, finalPath );
    throw RSOperatorError( ErrorCode::FileNotWritable,
                           "failed to publish output raster to: " + outputPath );
  }
  QFile::remove( backupPath );
  context.reportProgressForced( 1.0, "Tiled multi-input inference complete" );
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

RasterOutputMode TileInferenceEngine::rasterOutputMode( const ModelInfo &model )
{
  const std::string &f = model.output.format;
  if ( f.empty() || f == "probability" )
    return RasterOutputMode::Probability;
  if ( f == "labels" )
    return RasterOutputMode::Labels;
  if ( f == "mask" )
    return RasterOutputMode::Mask;
  if ( f == "confidence" )
    return RasterOutputMode::Confidence;
  // Unknown formats are rejected at manifest parse; this is defense in depth.
  throw RSOperatorError( ErrorCode::InvalidInputData,
                         "unsupported output.format '" + f
                           + "' (supported: probability, labels, mask, confidence)" );
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
