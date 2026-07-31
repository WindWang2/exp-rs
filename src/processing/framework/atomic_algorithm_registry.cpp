#include "atomic_algorithm_registry.h"
#include "agent_tool_call_exporter.h"

namespace sicnu::processing {

static std::function<void(AtomicAlgorithmRegistry&)> sRsOperatorProvider;

AtomicAlgorithmRegistry& AtomicAlgorithmRegistry::instance()
{
  static AtomicAlgorithmRegistry sInstance;
  return sInstance;
}

AtomicAlgorithmRegistry::AtomicAlgorithmRegistry()
{
  registerBuiltinRsOperators();
}

void AtomicAlgorithmRegistry::setRsOperatorProvider( std::function<void(AtomicAlgorithmRegistry&)> provider )
{
  sRsOperatorProvider = std::move( provider );
  // Populate after storing the provider. registerAdapter takes mMutex itself.
  if ( sRsOperatorProvider )
  {
    sRsOperatorProvider( instance() );
  }
}

void AtomicAlgorithmRegistry::reset()
{
  {
    std::lock_guard<std::mutex> lock( mMutex );
    mAdapters.clear();
  }

  // Provider callbacks re-enter via registerAdapter(); never hold mMutex across them.
  if ( sRsOperatorProvider )
  {
    sRsOperatorProvider( *this );
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

void AtomicAlgorithmRegistry::registerBuiltinRsOperators()
{
  if ( sRsOperatorProvider )
  {
    sRsOperatorProvider( *this );
  }
}

} // namespace sicnu::processing
