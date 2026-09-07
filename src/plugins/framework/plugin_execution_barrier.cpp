/***************************************************************************
 * src/plugins/framework/plugin_execution_barrier.cpp
 ***************************************************************************/
#include "plugin_execution_barrier.h"

#include <chrono>

namespace sicnu::plugins {

ExecutionLease::ExecutionLease( PluginExecutionBarrier &barrier, std::string pluginId )
    : mBarrier( &barrier )
    , mPluginId( std::move( pluginId ) )
{
}

ExecutionLease::~ExecutionLease()
{
    reset();
}

ExecutionLease::ExecutionLease( ExecutionLease &&other ) noexcept
    : mBarrier( other.mBarrier )
    , mPluginId( std::move( other.mPluginId ) )
{
    other.mBarrier = nullptr;
}

ExecutionLease &ExecutionLease::operator=( ExecutionLease &&other ) noexcept
{
    if ( this != &other )
    {
        reset();
        mBarrier = other.mBarrier;
        mPluginId = std::move( other.mPluginId );
        other.mBarrier = nullptr;
    }
    return *this;
}

void ExecutionLease::reset()
{
    if ( mBarrier )
    {
        mBarrier->release( mPluginId );
        mBarrier = nullptr;
    }
}

PluginExecutionBarrier &PluginExecutionBarrier::instance()
{
    static PluginExecutionBarrier barrier;
    return barrier;
}

PluginExecutionBarrier::LeasePtr PluginExecutionBarrier::acquire( const std::string &pluginId )
{
    if ( pluginId.empty() )
        return nullptr;
    std::lock_guard<std::mutex> lock( mMutex );
    State &state = mStates[pluginId];
    if ( state.draining || state.closed )
        return nullptr;
    ++state.active;
    // private ctor via ... ExecutionLease ctor is private with barrier as
    // friend — construct through the friend relationship.
    return LeasePtr( new ExecutionLease( *this, pluginId ) );
}

void PluginExecutionBarrier::beginDrain( const std::string &pluginId )
{
    if ( pluginId.empty() )
        return;
    std::lock_guard<std::mutex> lock( mMutex );
    State &state = mStates[pluginId];
    ++state.drainers;
    state.draining = true;
}

void PluginExecutionBarrier::cancelDrain( const std::string &pluginId )
{
    if ( pluginId.empty() )
        return;
    std::lock_guard<std::mutex> lock( mMutex );
    State &state = mStates[pluginId];
    if ( state.closed )
        return; // unload completed: a late cancel must not reopen
    if ( state.drainers > 0 )
        --state.drainers;
    state.draining = state.drainers > 0;
}

bool PluginExecutionBarrier::waitIdle( const std::string &pluginId, int timeoutMs )
{
    std::unique_lock<std::mutex> lock( mMutex );
    State &state = mStates[pluginId];
    const bool drained = mIdle.wait_for( lock, std::chrono::milliseconds( timeoutMs ), [&] {
        return state.active == 0;
    } );
    return drained;
}

void PluginExecutionBarrier::close( const std::string &pluginId )
{
    if ( pluginId.empty() )
        return;
    std::lock_guard<std::mutex> lock( mMutex );
    State &state = mStates[pluginId];
    state.draining = true;
    state.closed = true;
}

void PluginExecutionBarrier::open( const std::string &pluginId )
{
    if ( pluginId.empty() )
        return;
    std::lock_guard<std::mutex> lock( mMutex );
    State &state = mStates[pluginId];
    state.draining = false;
    state.closed = false;
    ++state.generation;
}

size_t PluginExecutionBarrier::activeCount( const std::string &pluginId ) const
{
    std::lock_guard<std::mutex> lock( mMutex );
    const auto iterator = mStates.find( pluginId );
    return iterator == mStates.end() ? 0 : iterator->second.active;
}

bool PluginExecutionBarrier::isRefusing( const std::string &pluginId ) const
{
    std::lock_guard<std::mutex> lock( mMutex );
    const auto iterator = mStates.find( pluginId );
    return iterator != mStates.end() && ( iterator->second.draining || iterator->second.closed );
}

unsigned long long PluginExecutionBarrier::generation( const std::string &pluginId ) const
{
    std::lock_guard<std::mutex> lock( mMutex );
    const auto iterator = mStates.find( pluginId );
    return iterator == mStates.end() ? 0 : iterator->second.generation;
}

void PluginExecutionBarrier::release( const std::string &pluginId )
{
    std::lock_guard<std::mutex> lock( mMutex );
    State &state = mStates[pluginId];
    if ( state.active > 0 )
        --state.active;
    if ( state.active == 0 )
        mIdle.notify_all();
}

} // namespace sicnu::plugins
