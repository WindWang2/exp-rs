#include <algorithm>
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
  // Provider callbacks re-enter registerAdapter() (same mMutex): never hold
  // the lock across them.
  if ( sRsOperatorProvider )
    sRsOperatorProvider( *this );
}

void AtomicAlgorithmRegistry::setRsOperatorProvider( std::function<void(AtomicAlgorithmRegistry&)> provider )
{
  // Only store the provider here: the callback re-enters registerAdapter()
  // which takes mMutex, and some callers (initialize(), reset()) may be
  // invoked with a lock already held on another thread or path. Populating
  // happens on the next initialize()/reset() — findAdapter never invokes
  // the provider (population via the ctor is what makes a bare test
  // registry resolve rs:* ids).
  sRsOperatorProvider = std::move( provider );
}

void AtomicAlgorithmRegistry::reset()
{
  {
    std::lock_guard<std::mutex> lock( mMutex );
    mAdapters.clear();
  }

  // Provider callbacks re-enter registerAdapter() (same mMutex): never hold
  // the lock across them. The #707 deadlock was the eager provider call in
  // setRsOperatorProvider() (reachable while the caller already held the
  // lock); that call is gone — population happens only here and in
  // initialize(), both lock-free at the call site.
  if ( sRsOperatorProvider )
    sRsOperatorProvider( *this );
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
  // thread AND when a full QgsApplication already exists — a bare
  // QCoreApplication (test fixtures, headless runners) makes
  // QCoreApplication::instance() non-null but lets processingRegistry()
  // lazily construct ApplicationMembers, whose QSettings/QLibraryInfo
  // initialization crashes without a fully-configured application (#697).
  // Worker threads and headless processes treat the algorithm as unlisted
  // and callers fall back to their defaults.
  if ( QgsApplication::instance()
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
  // Deterministic listing order (#643): mAdapters is an unordered_map, so the
  // iteration order (and therefore tools/list) varied between runs.
  std::sort( result.begin(), result.end(),
             []( const AlgorithmDescriptor &a, const AlgorithmDescriptor &b ) {
               return a.id < b.id;
             } );
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
