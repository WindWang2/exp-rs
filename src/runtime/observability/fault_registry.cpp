// fault_registry.cpp — see fault_registry.h for the contract.
#include "fault_registry.h"

#include <atomic>
#include <map>
#include <mutex>

namespace sicnu::runtime::observability::fault
{
namespace
{
struct Entry
{
    Mode mode = Mode::NextN;
    uint32_t remaining = 1;
    uint32_t period = 1;
    uint64_t calls = 0;
    std::string payload;
};

std::mutex &registryMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::map<std::string, Entry> &registryMap()
{
    static std::map<std::string, Entry> map;
    return map;
}

/// Fast-path counter: live entries. Zero → shouldFail is one relaxed load
/// (production hot path). Maintained on every map mutation.
std::atomic<uint32_t> g_armedCount{ 0 };

void syncArmedCount_locked()
{
    g_armedCount.store( static_cast<uint32_t>( registryMap().size() ), std::memory_order_relaxed );
}
} // namespace

bool shouldFail( const char *name )
{
    if ( g_armedCount.load( std::memory_order_relaxed ) == 0 )
        return false;
    if ( !name )
        return false;
    // Armed: reuse the std::string path (allocation only happens on the
    // armed path, never on the production fast path).
    return shouldFail( std::string( name ) );
}

bool shouldFail( const std::string &name )
{
    if ( g_armedCount.load( std::memory_order_relaxed ) == 0 )
        return false;
    std::lock_guard<std::mutex> lock( registryMutex() );
    auto &map = registryMap();
    auto it = map.find( name );
    if ( it == map.end() )
        return false;
    Entry &entry = it->second;
    ++entry.calls;
    bool fires = false;
    switch ( entry.mode )
    {
    case Mode::NextN:
        if ( entry.remaining > 0 )
        {
            --entry.remaining;
            fires = true;
            if ( entry.remaining == 0 )
            {
                map.erase( it );
                syncArmedCount_locked();
            }
        }
        else
        {
            map.erase( it );
            syncArmedCount_locked();
        }
        break;
    case Mode::Always:
        fires = true;
        break;
    case Mode::EveryNth:
        fires = entry.period > 0 && ( entry.calls % entry.period ) == 0;
        break;
    }
    return fires;
}

void armFault( const FaultAction &action )
{
    std::lock_guard<std::mutex> lock( registryMutex() );
    Entry &entry = registryMap()[action.name];
    entry.mode = action.mode;
    entry.remaining = action.mode == Mode::Always ? UINT32_MAX : action.count;
    // Normalize: EveryNth with period 0 would never fire yet permanently
    // defeat the global fast path (armedCount stays > 0).
    entry.period = action.count > 0 ? action.count : 1;
    entry.calls = 0;
    entry.payload = action.payload;
    syncArmedCount_locked();
}

void disarmFault( const std::string &name )
{
    std::lock_guard<std::mutex> lock( registryMutex() );
    registryMap().erase( name );
    syncArmedCount_locked();
}

void disarmAllFaults()
{
    std::lock_guard<std::mutex> lock( registryMutex() );
    registryMap().clear();
    syncArmedCount_locked();
}

std::string faultPayload( const std::string &name )
{
    if ( g_armedCount.load( std::memory_order_relaxed ) == 0 )
        return std::string();
    std::lock_guard<std::mutex> lock( registryMutex() );
    auto &map = registryMap();
    auto it = map.find( name );
    return it == map.end() ? std::string() : it->second.payload;
}

uint32_t armedFaultCount()
{
    return g_armedCount.load( std::memory_order_relaxed );
}

ArmedFault::ArmedFault( FaultAction action )
{
    armFault( std::move( action ) );
}

ArmedFault::~ArmedFault()
{
    disarmAllFaults();
}

} // namespace sicnu::runtime::observability::fault
