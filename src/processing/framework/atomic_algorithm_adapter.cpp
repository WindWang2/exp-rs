// src/processing/framework/atomic_algorithm_adapter.cpp
#include "atomic_algorithm_adapter.h"
#include "operators/framework/rs_operator_context.h"
#include "operators/framework/rs_operator_error.h"
#include <cmath>
#include <unordered_set>

namespace sicnu::processing {

namespace {

/// Maps a schema type string to a DataType ("number"/"integer"/"boolean"/
/// "string"/"object"/"array").
DataType typeStringToDataType( const std::string &tStr )
{
  if ( tStr == "number" ) return DataType::Numeric;
  if ( tStr == "integer" ) return DataType::Integer;
  if ( tStr == "boolean" ) return DataType::Boolean;
  if ( tStr == "object" ) return DataType::Json;
  return DataType::String;
}

/// Maps a ui-type/format hint ("raster"/"vector"/"table") to a DataType.
DataType uiTypeToDataType( const std::string &uiType )
{
  if ( uiType == "raster" ) return DataType::Raster;
  if ( uiType == "vector" ) return DataType::Vector;
  if ( uiType == "table" ) return DataType::Table;
  return DataType::String;
}

/// Applies schema constraints (minimum/maximum/array/x-rs-contract) shared by
/// input and output ports.
void applyConstraints( PortDescriptor &port, const Json::Value &pObj )
{
  if ( pObj.isMember( "minimum" ) && pObj["minimum"].isNumeric() )
  {
    port.hasMinimum = true;
    port.minimum = pObj["minimum"].asDouble();
  }
  if ( pObj.isMember( "maximum" ) && pObj["maximum"].isNumeric() )
  {
    port.hasMaximum = true;
    port.maximum = pObj["maximum"].asDouble();
  }
  if ( pObj.isMember( "x-rs-contract" ) && pObj["x-rs-contract"].isObject() )
    port.rsContract = pObj["x-rs-contract"];
}

/// Parses the name/displayName/description common to input and output ports.
void applyCommonPortFields( PortDescriptor &port, const std::string &key,
                            const Json::Value &pObj )
{
  port.name = key;
  if ( pObj.isMember( "title" ) && pObj["title"].isString() )
    port.displayName = pObj["title"].asString();
  else
    port.displayName = key;

  if ( pObj.isMember( "description" ) && pObj["description"].isString() )
    port.description = pObj["description"].asString();
}

/// Builds a PortDescriptor for a single declared output ("outputs" object
/// member in the root schema). Output ports are produced by the operator, so
/// they are never required as parameters.
PortDescriptor buildOutputPort( const std::string &key, const Json::Value &oObj )
{
  PortDescriptor port;
  applyCommonPortFields( port, key, oObj );

  const bool isFileOutput =
    oObj.isMember( "SicnuFileRole" ) && oObj["SicnuFileRole"].isString()
    && oObj["SicnuFileRole"].asString() == "output";

  // Semantic ui-type hints ("raster"/"vector"/"table") win.
  if ( oObj.isMember( "x-ui-type" ) && oObj["x-ui-type"].isString() )
  {
    port.type = uiTypeToDataType( oObj["x-ui-type"].asString() );
  }
  else if ( isFileOutput && oObj.isMember( "format" ) && oObj["format"].isString() )
  {
    // File-typed output with a concrete format ("tif"/"csv"/"shp"/...).
    const std::string fmt = oObj["format"].asString();
    port.fileFormat = fmt;
    port.type = dataTypeFromFileFormat( fmt );
  }
  else if ( isFileOutput )
  {
    // Output declared as a file without a concrete format — assume raster
    // (the historical default for this platform's operators).
    port.type = DataType::Raster;
  }
  else if ( oObj.isMember( "format" ) && oObj["format"].isString()
            && ( oObj["format"].asString() == "raster"
                 || oObj["format"].asString() == "vector"
                 || oObj["format"].asString() == "table" ) )
  {
    // makeRasterParam/makeVectorParam-style outputs carry format="raster"
    // with SicnuFileRole="input" (the helper's generic role); treat them as
    // the semantic data kind they name.
    port.type = uiTypeToDataType( oObj["format"].asString() );
  }
  else if ( oObj.isMember( "type" ) && oObj["type"].isString()
            && oObj["type"].asString() == "array" )
  {
    // Array output (e.g. endmember matrix) — type the items when declared.
    port.isArray = true;
    if ( oObj.isMember( "items" ) && oObj["items"].isObject()
         && oObj["items"].isMember( "type" ) && oObj["items"]["type"].isString() )
      port.itemType = typeStringToDataType( oObj["items"]["type"].asString() );
    else
      port.itemType = DataType::String;
    port.type = port.itemType;
  }
  else if ( oObj.isMember( "type" ) && oObj["type"].isString() )
  {
    port.type = typeStringToDataType( oObj["type"].asString() );
  }
  else
  {
    port.type = DataType::String;
  }

  if ( oObj.isMember( "default" ) )
  {
    if ( oObj["default"].isString() ) port.defaultValue = oObj["default"].asString();
    else if ( oObj["default"].isInt64() || oObj["default"].isUInt64() || oObj["default"].isInt() || oObj["default"].isUInt() )
      port.defaultValue = std::to_string( oObj["default"].asInt64() );
    else if ( oObj["default"].isNumeric() )
    {
      // Trim trailing zeros to avoid "3.000000" for integer-valued doubles.
      double d = oObj["default"].asDouble();
      if ( std::floor( d ) == d )
        port.defaultValue = std::to_string( static_cast<long long>( d ) );
      else
        port.defaultValue = std::to_string( d );
    }
    else if ( oObj["default"].isBool() ) port.defaultValue = oObj["default"].asBool() ? "true" : "false";
  }

  applyConstraints( port, oObj );
  return port;
}

} // namespace

AlgorithmDescriptor AlgorithmDescriptorBuilder::buildFromRsOperator( const operators::RSOperator &op )
{
  AlgorithmDescriptor desc;
  desc.id = op.name();
  desc.displayName = op.displayName();
  desc.group = op.group();
  desc.description = op.description();

  Json::Value meta = op.metadata();
  // Every operator declares a large-raster memory policy (default full_raster).
  meta["memoryPolicy"] = memoryPolicyName( op.memoryPolicy() );
  // Every operator declares its numeric reproducibility grade (ADR 0124,
  // default bit_exact): visible to GUI, CLI, MCP tools, and agents through
  // the same metadata surface.
  meta["determinism"] = determinismGradeName( op.determinism() );
  // largeRasterSafe derives from the memory policy: streaming and
  // multipass_streaming operators are safe on large rasters.
  const operators::RSOperatorMemoryPolicy policy = op.memoryPolicy();
  const bool largeRasterSafe = ( policy == operators::RSOperatorMemoryPolicy::Streaming
                                 || policy == operators::RSOperatorMemoryPolicy::MultiPassStreaming
                                 || policy == operators::RSOperatorMemoryPolicy::ExternalProcess );
  meta["largeRasterSafe"] = largeRasterSafe;
  // Declared execution-resource estimate (tile size / RAM / disk), when the
  // operator quantifies its large-raster behavior (ADR 0117).
  Json::Value estimate = op.executionEstimate();
  if ( estimate.isObject() && !estimate.empty() )
    meta["execution"] = std::move( estimate );
  desc.agentMetadata = AgentMetadata::fromJson( meta );

  Json::Value schema = op.schema();
  std::unordered_set<std::string> requiredSet;
  bool hasRequiredList = false;
  if ( schema.isObject() && schema.isMember( "required" ) && schema["required"].isArray() )
  {
    hasRequiredList = true;
    for ( const auto &req : schema["required"] )
    {
      if ( req.isString() )
      {
        requiredSet.insert( req.asString() );
      }
    }
  }

  if ( schema.isObject() && schema.isMember( "properties" ) && schema["properties"].isObject() )
  {
    const auto &props = schema["properties"];
    for ( const auto &key : props.getMemberNames() )
    {
      const auto &pObj = props[key];
      PortDescriptor pDesc;
      applyCommonPortFields( pDesc, key, pObj );
      pDesc.required = ( requiredSet.count( key ) > 0 ) ||
                        ( pObj.isMember( "required" ) && pObj["required"].isBool() && pObj["required"].asBool() );

      if ( pObj.isMember( "x-ui-type" ) && pObj["x-ui-type"].isString() )
      {
        pDesc.type = uiTypeToDataType( pObj["x-ui-type"].asString() );
      }
      else if ( pObj.isMember( "format" ) && pObj["format"].isString() )
      {
        std::string fmt = pObj["format"].asString();
        if ( fmt == "raster" ) pDesc.type = DataType::Raster;
        else if ( fmt == "vector" ) pDesc.type = DataType::Vector;
        else if ( fmt == "table" ) pDesc.type = DataType::Table;
        else pDesc.type = DataType::String;
      }
      else if ( pObj.isMember( "type" ) && pObj["type"].isString() )
      {
        const std::string tStr = pObj["type"].asString();
        if ( tStr == "array" )
        {
          pDesc.isArray = true;
          if ( pObj.isMember( "items" ) && pObj["items"].isObject()
               && pObj["items"].isMember( "type" ) && pObj["items"]["type"].isString() )
            pDesc.itemType = typeStringToDataType( pObj["items"]["type"].asString() );
          else
            pDesc.itemType = DataType::String;
          pDesc.type = pDesc.itemType;
        }
        else
        {
          pDesc.type = typeStringToDataType( tStr );
        }
      }

      if ( pObj.isMember( "enum" ) && pObj["enum"].isArray() )
      {
        pDesc.type = DataType::Enum;
        for ( const auto &item : pObj["enum"] )
        {
          if ( item.isString() )
            pDesc.enumOptions.push_back( item.asString() );
        }
      }

      if ( pObj.isMember( "default" ) )
      {
        if ( pObj["default"].isString() ) pDesc.defaultValue = pObj["default"].asString();
        else if ( pObj["default"].isInt64() || pObj["default"].isUInt64() || pObj["default"].isInt() || pObj["default"].isUInt() )
          pDesc.defaultValue = std::to_string( pObj["default"].asInt64() );
        else if ( pObj["default"].isNumeric() )
        {
          double d = pObj["default"].asDouble();
          if ( std::floor( d ) == d )
            pDesc.defaultValue = std::to_string( static_cast<long long>( d ) );
          else
            pDesc.defaultValue = std::to_string( d );
        }
        else if ( pObj["default"].isBool() ) pDesc.defaultValue = pObj["default"].asBool() ? "true" : "false";
      }

      applyConstraints( pDesc, pObj );
      desc.inputs.push_back( pDesc );
    }
  }

  // Real outputs: operators declare them in the root schema "outputs" object
  // (makeRootSchema). Statistics-only and multi-output operators must not be
  // described as a single Raster port.
  if ( schema.isObject() && schema.isMember( "outputs" ) && schema["outputs"].isObject() )
  {
    const auto &outs = schema["outputs"];
    for ( const auto &key : outs.getMemberNames() )
    {
      const auto &oObj = outs[key];
      if ( !oObj.isObject() )
        continue;
      desc.outputs.push_back( buildOutputPort( key, oObj ) );
    }
  }

  // Backward-compatible fallback: operators that declare no explicit outputs
  // are described with a single Raster "output" port (historical default).
  if ( desc.outputs.empty() )
  {
    PortDescriptor outPort;
    outPort.name = "output";
    outPort.displayName = "Output Dataset";
    outPort.type = DataType::Raster;
    desc.outputs.push_back( outPort );
  }

  return desc;
}

RsOperatorAdapter::RsOperatorAdapter( std::unique_ptr<operators::RSOperator> op )
  : mOp( std::move( op ) )
{
  if ( mOp )
  {
    mDesc = AlgorithmDescriptorBuilder::buildFromRsOperator( *mOp );
  }
}

std::string RsOperatorAdapter::algorithmId() const
{
  return mOp ? mOp->name() : "";
}

AlgorithmDescriptor RsOperatorAdapter::descriptor() const
{
  return mDesc;
}

Json::Value AtomicAlgorithmAdapter::estimateExecution( const Json::Value & /*params*/ ) const
{
  // Default: unknown/auto. Adapters whose algorithm can quantify the working
  // set from parameters override this with an overflow-safe dynamic estimate.
  return Json::Value( Json::objectValue );
}

Json::Value RsOperatorAdapter::estimateExecution( const Json::Value &params ) const
{
  if ( !mOp ) return Json::Value( Json::objectValue );
  return mOp->estimateExecution( params );
}

Json::Value RsOperatorAdapter::execute( const Json::Value &params, ProgressCallback progressCb,
                                        std::function<bool()> isCancelledFn )
{
  if ( !mOp ) return Json::Value( Json::objectValue );

  operators::RSOperatorContext ctx;
  if ( progressCb )
  {
    ctx.setProgressCallback( [progressCb]( double progress, const std::string &message ) {
      progressCb( static_cast<int>( progress * 100.0 ), message );
    } );
  }
  // External cancel bridge: the operator's cooperative cancellation
  // (ctx.throwIfCancelled() / ctx.isCancelled()) now observes the caller's
  // cancel predicate (e.g. JobEngine/TaskCenter cancel flag) mid-run, not
  // only once before execution.
  if ( isCancelledFn )
  {
    ctx.setCancelCallback( isCancelledFn );
  }
  if ( ctx.isCancelled() )
  {
    throw operators::RSOperatorError( operators::ErrorCode::Cancelled,
                                      "Cancelled before run: " + mOp->name() );
  }

  return mOp->run( params, ctx );
}

} // namespace sicnu::processing
