/***************************************************************************
 * rs_inference_operator.cpp  —  On-device ONNX inference RSOperator
 ***************************************************************************/
#include "rs_inference_operator.h"

#include "operators/framework/rs_json_params.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include "operators/framework/rs_schema.h"
#include "operators/framework/model_catalog.h"
#include "operators/runtime/model_execution_service.h"
#include "operators/runtime/model_runtime.h"
#include "operators/runtime/tile_inference_engine.h"

#include "processing/features/feature_cube.h"
#include "processing/framework/resource_estimation.h"
#include "processing/gdal/gdal_dataset_wrapper.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStringList>

#include <algorithm>
#include <string>

namespace sicnu::operators::rs {

using namespace params;
using runtime::ModelRuntimeRegistry;
using runtime::TileInferenceEngine;

namespace {

using namespace params;

/// Platform 8.0 WP-D: expands a LOCAL STAC Collection/Item document into a
/// time-ordered frame list for a temporal feed. Items come from a collection's
/// `links` with rel "item" (relative paths resolve against the document) or a
/// direct item path; each item contributes the asset matching @p assetKey
/// (default "COG"→"data"→first asset) and its `properties.datetime`.
/// Network STAC stays with the app-layer STAC client (QGIS) — this helper is
/// deliberately local-file only and refuses URLs (honest boundary, no
/// hidden network fetch inside an operator param parse).
/// @a outPaths / @a outTimestamps receive the ordered frames.
void expandStacCollection( const std::string &documentPath, const std::string &assetKey,
                           std::vector<std::string> *outPaths,
                           std::vector<std::string> *outTimestamps )
{
  if ( documentPath.rfind( "http://", 0 ) == 0 || documentPath.rfind( "https://", 0 ) == 0
       || documentPath.rfind( "s3://", 0 ) == 0 )
    throw RSOperatorError( ErrorCode::InvalidParameter,
                           "stac_collection '" + documentPath
                             + "' is remote — resolve it through the STAC client and feed "
                               "local items (operators never fetch the network in param parse)" );
  QFile doc( QString::fromStdString( documentPath ) );
  if ( !doc.open( QIODevice::ReadOnly ) )
    throw RSOperatorError( ErrorCode::FileNotFound,
                           "stac_collection not found: " + documentPath );
  const QByteArray bytes = doc.readAll();
  doc.close();

  const Json::Value parsed = [ & ] {
    Json::Value value;
    Json::Reader reader;
    if ( !reader.parse( bytes.constData(), bytes.constData() + bytes.size(), value ) )
      throw RSOperatorError( ErrorCode::InvalidInputData,
                             "stac_collection is not valid JSON: " + documentPath );
    return value;
  }();

  const QFileInfo docInfo( QString::fromStdString( documentPath ) );
  std::vector<std::pair<std::string, std::string>> frames; // (datetime, href)

  const auto collectItem = [ & ]( const std::string &itemPath ) {
    QFile itemFile( QString::fromStdString( itemPath ) );
    if ( !itemFile.open( QIODevice::ReadOnly ) )
      throw RSOperatorError( ErrorCode::FileNotFound, "STAC item not found: " + itemPath );
    const QByteArray itemBytes = itemFile.readAll();
    itemFile.close();
    Json::Value item;
    Json::Reader reader;
    if ( !reader.parse( itemBytes.constData(), itemBytes.constData() + itemBytes.size(), item )
         || item["properties"].isNull() )
      throw RSOperatorError( ErrorCode::InvalidInputData,
                             "STAC item is not valid: " + itemPath );
    // STAC relative hrefs resolve against the document containing them —
    // the ITEM (which may live in a subdirectory of the collection), not
    // the top-level document.
    const QFileInfo itemInfo( QString::fromStdString( itemPath ) );
    const std::string datetime = item["properties"].get( "datetime", "" ).asString();
    if ( datetime.empty() )
      throw RSOperatorError( ErrorCode::InvalidInputData,
                             "STAC item carries no properties.datetime: " + itemPath );
    const Json::Value &assets = item["assets"];
    if ( !assets.isObject() || assets.empty() )
      throw RSOperatorError( ErrorCode::InvalidInputData,
                             "STAC item carries no assets: " + itemPath );
    std::string href;
    if ( !assetKey.empty() && assets.isMember( assetKey ) )
      href = assets[assetKey].get( "href", "" ).asString();
    else
    {
      // Documented fallback order: "COG" → "data" → "image" → first entry.
      for ( const char *key : { "COG", "data", "image" } )
        if ( assets.isMember( key ) )
        {
          href = assets[key].get( "href", "" ).asString();
          break;
        }
      if ( href.empty() )
        href = assets.begin()->get( "href", "" ).asString();
    }
    if ( href.empty() )
      throw RSOperatorError(
        ErrorCode::InvalidInputData,
        "STAC item carries no usable asset href"
          + ( assetKey.empty() ? std::string( " (tried COG, data, image, first entry)" )
                               : " for asset '" + assetKey + "'" )
          + ": " + itemPath );
    if ( QFileInfo( QString::fromStdString( href ) ).isRelative() )
      href = itemInfo.absoluteDir().filePath( QString::fromStdString( href ) ).toStdString();
    frames.emplace_back( datetime, href );
  };

  const std::string type = parsed.get( "type", "" ).asString();
  if ( type == "Collection" )
  {
    const Json::Value &links = parsed["links"];
    if ( !links.isArray() )
      throw RSOperatorError( ErrorCode::InvalidInputData,
                             "STAC collection carries no links array: " + documentPath );
    constexpr std::size_t kMaxStacItems = 1024; // mirrors the engine frame cap
    for ( const auto &link : links )
    {
      if ( link.get( "rel", "" ).asString() != "item" )
        continue;
      if ( frames.size() >= kMaxStacItems )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "STAC collection resolves to more than "
                                 + std::to_string( kMaxStacItems )
                                 + " items: " + documentPath + " (coarsen the series)" );
      std::string href = link.get( "href", "" ).asString();
      if ( href.empty() )
        continue;
      if ( QFileInfo( QString::fromStdString( href ) ).isRelative() )
        href = docInfo.absoluteDir().filePath( QString::fromStdString( href ) ).toStdString();
      collectItem( href );
    }
    if ( frames.empty() )
      throw RSOperatorError( ErrorCode::InvalidInputData,
                             "STAC collection resolves to no items: " + documentPath );
  }
  else if ( type == "Feature" )
  {
    collectItem( documentPath );
  }
  else
    throw RSOperatorError( ErrorCode::InvalidInputData,
                           "stac_collection '" + documentPath
                             + "' is neither a Collection nor an Item (type '" + type + "')" );

  // Validate EVERY datetime BEFORE sorting: throwing from a comparator
  // gives no ordering guarantee and reports the failure data-order
  // dependently.
  for ( const auto &frame : frames )
    if ( !QDateTime::fromString( QString::fromStdString( frame.first ), Qt::ISODate ).isValid() )
      throw RSOperatorError( ErrorCode::InvalidInputData,
                             "STAC datetime '" + frame.first + "' is not ISO 8601 in: "
                               + documentPath );
  // Sort by parsed instant; equal instants keep document order (stable).
  std::stable_sort( frames.begin(), frames.end(),
                    [ & ]( const auto &a, const auto &b ) {
                      const QDateTime ta = QDateTime::fromString( QString::fromStdString( a.first ),
                                                                  Qt::ISODate );
                      const QDateTime tb = QDateTime::fromString( QString::fromStdString( b.first ),
                                                                  Qt::ISODate );
                      return ta < tb;
                    } );
  for ( const auto &frame : frames )
  {
    outPaths->push_back( frame.second );
    outTimestamps->push_back( frame.first );
  }
}

} // namespace

Json::Value RsInferenceOperator::schema() const
{
    using namespace schema;
    Json::Value props( Json::objectValue );
    props["input"] = makeRasterParam( "input", "Input raster" );
    props["model"] = makeStringParam( "model", "Path to an ONNX model readable by cv::dnn, or a model catalog name (see spatial:list_models)" );
    props["output"] = makeOutputParam( "output", "Output inference raster", "tif" );
    // `bands` is an optional array of 1-based band indices (default: all bands).
    // Described as a raw JSON-schema array (no array helper exists yet) so an
    // agent reading the schema passes ["bands": [1,2,3]], not a single int.
    Json::Value bandsParam( Json::objectValue );
    bandsParam["name"] = "bands";
    bandsParam["type"] = "array";
    bandsParam["description"] = "1-based band numbers to feed (default: all bands)";
    Json::Value items( Json::objectValue );
    items["type"] = "integer";
    items["minimum"] = 1;
    bandsParam["items"] = items;
    props["bands"] = bandsParam;
    // Platform 3.0 knobs (goal §10): flip test-time augmentation and a hard
    // batch cap for memory-pinned runs.
    props["tta"] = makeEnumParam( "tta", "Test-time augmentation (flip averaging)",
                                  { "none", "hflip", "hvflip" }, "none" );
    props["batchCap"] = makeIntegerParam( "batchCap", "Hard cap on tiles per forward pass (0 = manifest/budget default)", 0 );
    props["device"] = makeStringParam( "device", "Execution device (cpu/cuda)", "" );
    // Platform 8.0 multimodal / temporal feeds: one object per manifest input
    // contract. When declared, `input` is not used (the primary feed is the
    // grid authority).
    {
      Json::Value named( Json::objectValue );
      named["name"] = "named_inputs";
      named["type"] = "array";
      named["description"] =
        "Named multi-input/temporal feeds for multi-input models (one object per "
        "manifest input): {name, paths[], bands[], timestamps[], quality_masks[], "
        "prepared_from[]}. paths are time-ordered frames; timestamps are ISO 8601 "
        "(strictly increasing); quality_masks mark invalid pixels per frame; "
        "prepared_from records pre-aligned source paths (provenance only).";
      Json::Value namedItems( Json::objectValue );
      namedItems["type"] = "object";
      named["items"] = namedItems;
      props["named_inputs"] = named;
    }

    Json::Value outputs( Json::objectValue );
    outputs["output"] = makeRasterParam( "output", "Output raster path" );
    outputs["backend"] = makeStringParam( "backend", "Inference backend", "" );
    outputs["device"] = makeStringParam( "device", "Execution device (cpu/cuda)", "" );
    outputs["model"] = makeStringParam( "model", "Resolved model name or path", "" );
    outputs["outBands"] = makeIntegerParam( "outBands", "Number of bands written", 0 );
    outputs["width"] = makeIntegerParam( "width", "Output raster width", 0 );
    outputs["height"] = makeIntegerParam( "height", "Output raster height", 0 );
    outputs["tileSize"] = makeIntegerParam( "tileSize", "Core tile edge used (px)", 0 );
    outputs["tiles"] = makeIntegerParam( "tiles", "Tiles processed", 0 );

    Json::Value root = makeRootSchema( displayName(), description(), props, outputs );
    root["required"] = makeRequired( { "model", "output" } ); // `input` XOR `named_inputs`
    return root;
}

Json::Value RsInferenceOperator::metadata() const
{
    Json::Value meta( Json::objectValue );
    meta["group"] = group();
    meta["displayName"] = displayName();
    meta["description"] = description();
    meta["tags"].append( "inference" );
    meta["tags"].append( "onnx" );
    meta["tags"].append( "edge-ai" );
    meta["tags"].append( "deep-learning" );
    meta["task"] = "inference";
    meta["notes"] = "ONNX inference via the model runtime (cv::dnn provider, cached sessions). Accepts a model path or a ModelCatalog name (spatial:list_models); catalog models must be ready (artifact present, checksum verified). Bounded tiled execution with manifest v2 preprocessing/postprocessing contracts; memory policy Streaming.";
    meta["gpu"] = true; // CUDA-capable per model (opencv_dnn_runtime); CPU fallback per manifest
    meta["purpose"] = "Run a pretrained ONNX model on a raster with pure C++ (cv::dnn), tiled with bounded memory.";
    meta["prerequisites"].append( "Model must be loadable by cv::dnn::readNetFromONNX." );
    meta["workflowHints"].append( "Preprocessing/postprocessing follow the model manifest (v2) contracts; default is bands-in/raster-out identity chaining." );
    meta["workflowHints"].append( "Catalog models must be ready (artifact present, checksum verified) — see spatial:list_models." );
    return meta;
}

Json::Value RsInferenceOperator::executionEstimate() const
{
    // Static fallback: the tiled engine's per-tile working set for a default
    // 512 px tile, 4 bands, batch 1 (~3 tile-sized buffers) plus a weights
    // overhead floor. The dynamic estimateExecution(params) refines this from
    // the actual raster header and model contracts.
    return sicnu::processing::makeStreamingEstimate( 512, 512, 4, 4, 3,
                                                     /*matrixBytes*/ 0,
                                                     /*fixedOverhead*/ 64 * 1024 * 1024 );
}

Json::Value RsInferenceOperator::estimateExecution( const Json::Value &params ) const
{
    const std::string inputPath = params.isObject() && params.isMember( "input" )
                                    ? params["input"].asString()
                                    : std::string();
    const std::string modelReference = params.isObject() && params.isMember( "model" )
                                         ? params["model"].asString()
                                         : std::string();

    // Contract lookup for estimation must not require readiness (a missing
    // artifact still carries parseable tiling/runtime contracts).
    const ModelInfo model = runtime::resolveModelReference( modelReference, nullptr );

    const int tile = TileInferenceEngine::effectiveTileSize( model );
    const int halo = TileInferenceEngine::effectiveHalo( model );
    const std::uint64_t batch = static_cast<std::uint64_t>( std::max( 1, model.tiling.batchSize ) );

    std::uint64_t bands = 4; // conservative default when the raster is unknown
    GdalDatasetWrapper ds;
    if ( !inputPath.empty() && ds.open( QString::fromStdString( inputPath ) ) )
    {
        bands = static_cast<std::uint64_t>( ds.bandCount() );
        // A bands parameter narrows the fed channels.
        if ( params.isObject() && params.isMember( "bands" ) && params["bands"].isArray() )
            bands = std::max<std::uint64_t>( 1, static_cast<std::uint64_t>( params["bands"].size() ) );
    }

    const std::uint64_t edge = static_cast<std::uint64_t>( tile + 2 * halo );
    // #689: with resize "to_input" the engine feeds model.input.width x
    // .height tensors — the batched tiles, the blob and the per-tile output
    // planes are all sized by the fixed graph input, not the read window, so
    // a small tile_size with a large graph input under-estimated RAM by orders
    // of magnitude. Estimate on the LARGER of the two geometries so neither
    // the halo window nor the fed tensor is under-counted.
    std::uint64_t fedW = edge;
    std::uint64_t fedH = edge;
    if ( model.preprocess.resize == "to_input" && model.input.width > 0 && model.input.height > 0 )
    {
        fedW = std::max( edge, static_cast<std::uint64_t>( model.input.width ) );
        fedH = std::max( edge, static_cast<std::uint64_t>( model.input.height ) );
    }
    // Read window + detached tile + blob + output planes ≈ 4 tile-sized sets
    // per batched tile; model weights are the fixed overhead when declared.
    std::uint64_t modelRamBytes =
        static_cast<std::uint64_t>( std::max( 0, model.runtime.estimatedRamMb ) ) * 1024 * 1024;
    // #689: no shipped manifest declares estimated_ram_mb, which hid the
    // (dominant) weight bytes from the admission estimate. When undeclared,
    // floor the model term with the resolved artifact's size on disk (the
    // serialized weights, rounded up to whole MiB) and keep the read-window
    // math unchanged.
    if ( modelRamBytes == 0 && !model.resolvedArtifactPath.empty() )
    {
        const std::uint64_t artifactBytes = static_cast<std::uint64_t>(
            QFileInfo( QString::fromStdString( model.resolvedArtifactPath ) ).size() );
        constexpr std::uint64_t kMiB = 1024 * 1024;
        if ( artifactBytes > 0 )
            modelRamBytes = ( ( artifactBytes + kMiB - 1 ) / kMiB ) * kMiB;
    }
    // Multi-head/uncertainty amplification (review 3): flushBatch retains
    // every declared head's output planes alongside the input blob. Count the
    // DECLARED channels (classes x tensor_names + optional uncertainty band);
    // undeclared heads keep the historical input-sized allowance.
    std::uint64_t declaredOutChannels = 0;
    if ( !model.output.classes.empty() )
        declaredOutChannels +=
            model.output.classes.size() *
            std::max<std::size_t>( 1, model.output.tensorNames.size() );
    else if ( !model.output.tensorNames.empty() )
        declaredOutChannels = model.output.tensorNames.size();
    if ( model.output.uncertainty == "entropy" || model.output.uncertainty == "margin" )
        declaredOutChannels += 1;
    const std::uint64_t perTileSets = batch * ( 4 + declaredOutChannels );

    Json::Value est = sicnu::processing::makeStreamingEstimate( fedW, fedH, bands, 4,
                                                                perTileSets, /*matrixBytes*/ 0,
                                                                /*fixedOverhead*/ modelRamBytes + 32 * 1024 * 1024 );
    // VRAM contract surfaces for admission tooling (TaskCenter admits on RAM
    // today; GPU-aware admission is a documented follow-up).
    if ( model.runtime.gpu )
        est["estimatedVramMb"] = model.runtime.estimatedVramMb;
    return est;
}

Json::Value RsInferenceOperator::run( const Json::Value &params, RSOperatorContext &context )
{
    if ( !params.isObject() )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "Operator parameters must be a JSON object" );

    runtime::ModelExecutionRequest request;
    // Platform 8.0: named multi-input/temporal feeds replace the single
    // `input` path when declared (the primary feed becomes the grid
    // authority). Parse strictly: every array element is typed and
    // parallel arrays must agree in length — the engine re-checks the
    // semantics (ordering, grid identity) against real rasters.
    const bool hasNamed = params.isObject() && params.isMember( "named_inputs" )
                            && !params["named_inputs"].isNull();
    if ( hasNamed )
    {
      if ( !params["named_inputs"].isArray() || params["named_inputs"].empty() )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "named_inputs must be a non-empty array of feed objects" );
      for ( const auto &entry : params["named_inputs"] )
      {
        if ( !entry.isObject() )
          throw RSOperatorError( ErrorCode::InvalidParameter,
                                 "every named_inputs entry must be an object" );
        runtime::NamedRasterFeed feed;
        feed.name = entry.isMember( "name" ) && entry["name"].isString()
                      ? entry["name"].asString()
                      : std::string();
        if ( entry.isMember( "stac_collection" ) && entry.isMember( "paths" ) )
          throw RSOperatorError( ErrorCode::InvalidParameter,
                                 "named_inputs feed '" + feed.name
                                   + "': declare stac_collection OR paths, not both" );
        if ( entry.isMember( "stac_collection" ) )
        {
          // Local STAC Collection/Item → time-ordered frames + timestamps.
          if ( !entry["stac_collection"].isString() )
            throw RSOperatorError( ErrorCode::InvalidParameter,
                                   "named_inputs feed '" + feed.name
                                     + "': stac_collection must be a document path" );
          const std::string assetKey =
            entry.isMember( "stac_asset" ) && entry["stac_asset"].isString()
              ? entry["stac_asset"].asString()
              : std::string();
          expandStacCollection( entry["stac_collection"].asString(), assetKey, &feed.paths,
                                &feed.timestamps );
        }
        else if ( !entry.isMember( "paths" ) || !entry["paths"].isArray() || entry["paths"].empty() )
          throw RSOperatorError( ErrorCode::InvalidParameter,
                                 "named_inputs feed '" + feed.name
                                   + "' needs a non-empty paths array (or stac_collection)" );
        else
        {
          for ( const auto &p : entry["paths"] )
          {
            if ( !p.isString() )
              throw RSOperatorError( ErrorCode::InvalidParameter,
                                     "named_inputs feed '" + feed.name + "': paths must be strings" );
            feed.paths.push_back( p.asString() );
          }
        }
        if ( entry.isMember( "bands" ) )
        {
          if ( !entry["bands"].isArray() )
            throw RSOperatorError( ErrorCode::InvalidParameter,
                                   "named_inputs feed '" + feed.name + "': bands must be an array" );
          for ( const auto &b : entry["bands"] )
          {
            if ( !b.isInt() )
              throw RSOperatorError( ErrorCode::InvalidParameter,
                                     "named_inputs feed '" + feed.name + "': bands must be integers" );
            feed.bands.push_back( b.asInt() );
          }
        }
        const auto parseStringArray = [ & ]( const char *key,
                                             std::vector<std::string> *out ) {
          if ( !entry.isMember( key ) || entry[key].isNull() )
            return;
          if ( !entry[key].isArray() )
            throw RSOperatorError( ErrorCode::InvalidParameter,
                                   "named_inputs feed '" + feed.name + "': " + key
                                     + " must be an array" );
          for ( const auto &v : entry[key] )
          {
            if ( !v.isString() )
              throw RSOperatorError( ErrorCode::InvalidParameter,
                                     "named_inputs feed '" + feed.name + "': " + key
                                       + " must contain strings" );
            out->push_back( v.asString() );
          }
        };
        parseStringArray( "timestamps", &feed.timestamps );
        parseStringArray( "quality_masks", &feed.qualityMasks );
        parseStringArray( "prepared_from", &feed.preparedFrom );
        request.namedInputs.push_back( std::move( feed ) );
      }
    }
    else
    {
      request.inputPath = requireString( params, "input" );
    }
    request.modelReference = requireString( params, "model" );
    request.outputPath = requireString( params, "output" );
    // Free token, not an enum: explicit cuda:N references must pass through.
    if ( params.isObject() && params.isMember( "device" ) )
    {
      if ( !params["device"].isString() )
        throw RSOperatorError( ErrorCode::InvalidParameter, "device must be a string" );
      request.deviceToken = params["device"].asString();
    }

    const int bandCount = [ & ] {
      if ( !request.namedInputs.empty() )
        return 0; // multi-input: band selection happens per feed
      GdalDatasetWrapper ds;
      if ( !ds.open( QString::fromStdString( request.inputPath ) ) )
        throw RSOperatorError( ErrorCode::GdalError,
                               "Failed to open input raster: " + request.inputPath );
      return ds.bandCount();
    }();
    if ( !request.namedInputs.empty() )
    {
      // Per-feed band selection rides the feeds; a top-level bands list next
      // to named feeds is an authoring error (it would be silently ignored).
      if ( params.isMember( "bands" ) && !params["bands"].isNull() )
        throw RSOperatorError( ErrorCode::InvalidParameter,
                               "bands applies to the single-input path — select bands "
                                 "per feed inside named_inputs" );
      request.bands = {};
    }
    else
    {
      if ( bandCount <= 0 )
        throw RSOperatorError( ErrorCode::GdalError,
                               "Failed to read band count from input raster" );
      request.bands = parseBands( params, bandCount );
    }

    const std::string tta = getEnum( params, "tta", { "none", "hflip", "hvflip" }, "none" );
    if ( tta == "hflip" )
      request.tta = runtime::TtaMode::HFlip;
    else if ( tta == "hvflip" )
      request.tta = runtime::TtaMode::HVFlip;
    request.batchSizeOverride = std::max( 0, getInt( params, "batchCap", 0 ) );

    const runtime::ModelExecutionResult result =
      runtime::runModelInference( request, context );
    return result.payload;
}

} // namespace sicnu::operators::rs
