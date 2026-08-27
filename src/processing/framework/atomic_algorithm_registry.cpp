#include "atomic_algorithm_registry.h"
#include "agent_tool_call_exporter.h"
#include "provider_algorithm_adapter.h"

#include <qgsapplication.h>
#include <processing/qgsprocessingregistry.h>
#include <processing/qgsprocessingalgorithm.h>

#include <QCoreApplication>
#include <QThread>

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

void AtomicAlgorithmRegistry::initialize()
{
  // Explicit initialization seam for the canonical Agent-facing catalog.
  // Re-applies the RS operator provider (idempotent: registerAdapter overwrites
  // by algorithm id) so the catalog is fully populated once the operator
  // library has registered its provider — regardless of static-init ordering
  // between this library and the operators library. Called from
  // AlgorithmEngine::initialize() (app startup) and by tests via reset().
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

  // The QGIS processing registry lazily constructs QgsApplication members,
  // which is main-thread-only machinery (QgsRuntimeProfiler asserts - and
  // Qt aborts the process - when they are first touched from a worker
  // thread, e.g. WorkflowRuntime::runStepViaExecutionPlane's preflight
  // estimate on a runner thread). Only consult the registry from the main
  // thread; worker threads treat the algorithm as unlisted and callers fall
  // back to their defaults.
  if ( QCoreApplication::instance()
       && QThread::currentThread() == QCoreApplication::instance()->thread()
       && QgsApplication::processingRegistry() )
  {
    const QString qid = QString::fromStdString( algorithmId );
    if ( const QgsProcessingAlgorithm *alg = QgsApplication::processingRegistry()->algorithmById( qid ) )
    {
      auto adapter = std::make_shared<ProviderAlgorithmAdapter>( *alg );
      const_cast<AtomicAlgorithmRegistry*>(this)->mAdapters[algorithmId] = adapter;
      return adapter;
    }
  }

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
