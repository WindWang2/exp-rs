// src/processing/framework/atomic_algorithm_registry.cpp
#include "atomic_algorithm_registry.h"
#include "agent_tool_call_exporter.h"
#include "operators/framework/rs_operator_registry.h"
#include "task_center.h"

namespace sicnu::processing {

AtomicAlgorithmRegistry& AtomicAlgorithmRegistry::instance()
{
  static AtomicAlgorithmRegistry sInstance;
  return sInstance;
}

AtomicAlgorithmRegistry::AtomicAlgorithmRegistry()
{
  registerBuiltinRsOperators();
}

void AtomicAlgorithmRegistry::reset()
{
  std::lock_guard<std::mutex> lock( mMutex );
  mAdapters.clear();
  
  // Re-register builtins
  auto names = operators::RSOperatorRegistry::instance().operatorNames();
  for ( const auto &name : names )
  {
    auto op = operators::RSOperatorRegistry::instance().create( name );
    if ( op )
    {
      auto adapter = std::make_shared<RsOperatorAdapter>( std::move( op ) );
      mAdapters[adapter->algorithmId()] = adapter;
    }
  }
}

void AtomicAlgorithmRegistry::registerAdapter( AtomicAlgorithmAdapterPtr adapter )
{
  if ( !adapter ) return;
  std::lock_guard<std::mutex> lock( mMutex );
  mAdapters[adapter->algorithmId()] = adapter;
}

bool AtomicAlgorithmRegistry::unregisterAdapter( const std::string &algorithmId )
{
  std::lock_guard<std::mutex> lock( mMutex );
  return mAdapters.erase( algorithmId ) > 0;
}

AtomicAlgorithmAdapterPtr AtomicAlgorithmRegistry::findAdapter( const std::string &algorithmId ) const
{
  std::lock_guard<std::mutex> lock( mMutex );
  auto it = mAdapters.find( algorithmId );
  if ( it != mAdapters.end() )
    return it->second;
  return nullptr;
}

std::vector<AlgorithmDescriptor> AtomicAlgorithmRegistry::listDescriptors() const
{
  std::lock_guard<std::mutex> lock( mMutex );
  std::vector<AlgorithmDescriptor> result;
  result.reserve( mAdapters.size() );

  for ( const auto &pair : mAdapters )
  {
    if ( pair.second )
    {
      result.push_back( pair.second->descriptor() );
    }
  }
  return result;
}

Json::Value AtomicAlgorithmRegistry::exportOpenAiToolDefinitions() const
{
  return AgentToolCallExporter::exportOpenAiToolDefinitions( listDescriptors() );
}

std::string AtomicAlgorithmRegistry::exportSystemPromptCatalog() const
{
  return AgentToolCallExporter::exportSystemPromptCatalog( listDescriptors() );
}

size_t AtomicAlgorithmRegistry::adapterCount() const
{
  std::lock_guard<std::mutex> lock( mMutex );
  return mAdapters.size();
}

std::string AtomicAlgorithmRegistry::executeToolCall( const std::string &jsonToolCall )
{
  Json::CharReaderBuilder builder;
  Json::Value root;
  std::string errs;
  std::unique_ptr<Json::CharReader> reader( builder.newCharReader() );

  if ( !reader->parse( jsonToolCall.c_str(), jsonToolCall.c_str() + jsonToolCall.length(), &root, &errs ) )
  {
    Json::Value errRes;
    errRes["status"] = "error";
    errRes["error"] = "Failed to parse JSON tool call: " + errs;
    return errRes.toStyledString();
  }

  std::string toolName;
  Json::Value paramsNode( Json::objectValue );

  if ( root.isMember( "name" ) )
  {
    toolName = root["name"].asString();
  }
  else if ( root.isMember( "function" ) && root["function"].isMember( "name" ) )
  {
    toolName = root["function"]["name"].asString();
  }

  if ( root.isMember( "parameters" ) )
  {
    paramsNode = root["parameters"];
  }
  else if ( root.isMember( "function" ) && root["function"].isMember( "arguments" ) )
  {
    if ( root["function"]["arguments"].isString() )
    {
      std::string argsStr = root["function"]["arguments"].asString();
      Json::Value parsedArgs;
      if ( reader->parse( argsStr.c_str(), argsStr.c_str() + argsStr.length(), &parsedArgs, &errs ) )
      {
        paramsNode = parsedArgs;
      }
    }
    else
    {
      paramsNode = root["function"]["arguments"];
    }
  }

  auto adapter = findAdapter( toolName );
  if ( !adapter )
  {
    Json::Value errRes;
    errRes["status"] = "error";
    errRes["error"] = "Algorithm adapter not found: " + toolName;
    return errRes.toStyledString();
  }

  Json::Value execResult = adapter->execute( paramsNode );
  return execResult.toStyledString();
}

void AtomicAlgorithmRegistry::registerBuiltinRsOperators()
{
  auto names = operators::RSOperatorRegistry::instance().operatorNames();
  for ( const auto &name : names )
  {
    auto op = operators::RSOperatorRegistry::instance().create( name );
    if ( op )
    {
      auto adapter = std::make_shared<RsOperatorAdapter>( std::move( op ) );
      mAdapters[adapter->algorithmId()] = adapter;
    }
  }
}

} // namespace sicnu::processing
