// src/processing/framework/atomic_algorithm_adapter.cpp
#include "atomic_algorithm_adapter.h"
#include "operators/framework/rs_operator_context.h"

namespace sicnu::processing {

AlgorithmDescriptor AlgorithmDescriptorBuilder::buildFromRsOperator( const operators::RSOperator &op )
{
  AlgorithmDescriptor desc;
  desc.id = op.name();
  desc.displayName = op.displayName();
  desc.group = op.group();
  desc.description = op.description();

  Json::Value meta = op.metadata();
  desc.agentMetadata = AgentMetadata::fromJson( meta );

  Json::Value schema = op.schema();
  if ( schema.isObject() && schema.isMember( "properties" ) && schema["properties"].isObject() )
  {
    const auto &props = schema["properties"];
    for ( const auto &key : props.getMemberNames() )
    {
      const auto &pObj = props[key];
      PortDescriptor pDesc;
      pDesc.name = key;
      if ( pObj.isMember( "title" ) && pObj["title"].isString() )
        pDesc.displayName = pObj["title"].asString();
      else
        pDesc.displayName = key;

      if ( pObj.isMember( "description" ) && pObj["description"].isString() )
        pDesc.description = pObj["description"].asString();

      if ( pObj.isMember( "x-ui-type" ) && pObj["x-ui-type"].isString() )
      {
        std::string uiType = pObj["x-ui-type"].asString();
        if ( uiType == "raster" ) pDesc.type = DataType::Raster;
        else if ( uiType == "vector" ) pDesc.type = DataType::Vector;
        else if ( uiType == "table" ) pDesc.type = DataType::Table;
        else pDesc.type = DataType::String;
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
        std::string tStr = pObj["type"].asString();
        if ( tStr == "number" ) pDesc.type = DataType::Numeric;
        else if ( tStr == "integer" ) pDesc.type = DataType::Integer;
        else if ( tStr == "boolean" ) pDesc.type = DataType::Boolean;
        else if ( tStr == "object" ) pDesc.type = DataType::Json;
        else pDesc.type = DataType::String;
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
        else if ( pObj["default"].isNumeric() ) pDesc.defaultValue = std::to_string( pObj["default"].asDouble() );
        else if ( pObj["default"].isBool() ) pDesc.defaultValue = pObj["default"].asBool() ? "true" : "false";
      }

      desc.inputs.push_back( pDesc );
    }
  }

  // Default output port for operator
  PortDescriptor outPort;
  outPort.name = "output";
  outPort.displayName = "Output Dataset";
  outPort.type = DataType::Raster;
  desc.outputs.push_back( outPort );

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

Json::Value RsOperatorAdapter::execute( const Json::Value &params, ProgressCallback progressCb )
{
  if ( !mOp ) return Json::Value( Json::objectValue );

  operators::RSOperatorContext ctx;
  if ( progressCb )
  {
    ctx.setProgressCallback( [progressCb]( double progress, const std::string &message ) {
      progressCb( static_cast<int>( progress * 100.0 ), message );
    } );
  }

  return mOp->run( params, ctx );
}

} // namespace sicnu::processing
