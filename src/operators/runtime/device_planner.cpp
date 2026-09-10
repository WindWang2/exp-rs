// src/operators/runtime/device_planner.cpp — Platform 7.0 device planner.
#include "operators/runtime/device_planner.h"

#include "operators/runtime/model_runtime.h"

#include <algorithm>

namespace sicnu::operators::runtime {

DeviceInventory DeviceInventory::fromHardware( const ModelHardwareCapabilities &hw )
{
  DeviceInventory inventory;
  // Per-device capacity: the enforced budget applies to EACH addressable
  // device (documented 7.0 semantics — real per-card enumeration arrives
  // with a provider that can actually query it; env overrides keep tests
  // and constrained deployments deterministic).
  for ( int i = 0; i < hw.cudaDeviceCount; ++i )
  {
    DeviceInfo info;
    info.index = i;
    info.name = "cuda:" + std::to_string( i );
    info.vramCapacityMb = hw.vramBudgetMb;
    inventory.m_devices.push_back( info );
  }
  return inventory;
}

const DeviceInfo *DeviceInventory::device( int index ) const
{
  for ( const DeviceInfo &info : m_devices )
    if ( info.index == index )
      return &info;
  return nullptr;
}

void VramLedger::reset()
{
  std::lock_guard<std::mutex> lock( m_mutex );
  m_devices.clear();
}

void VramLedger::setCapacity( int deviceIndex, int capacityMb )
{
  std::lock_guard<std::mutex> lock( m_mutex );
  m_devices[deviceIndex].capacityMb = capacityMb > 0 ? capacityMb : 0;
}

bool VramLedger::tryReserve( int deviceIndex, int vramMb, const std::string &holder,
                             std::string *why )
{
  if ( holder.empty() )
  {
    if ( why )
      *why = "reservation holder must be a non-empty session identity";
    return false;
  }
  std::lock_guard<std::mutex> lock( m_mutex );
  Entry &entry = m_devices[deviceIndex];
  if ( vramMb <= 0 )
    return true; // unknown estimate: nothing to account, admission granted
  if ( entry.capacityMb <= 0 )
    return true; // unenforced capacity: tracked below, never refused
  // Re-reserving the same holder REPLACES its previous reservation (idempotent
  // acquire paths): drop the old amount BEFORE the capacity check so a
  // same-holder upgrade is judged against the effectively-free VRAM.
  int previousMb = 0;
  if ( auto existing = entry.holders.find( holder ); existing != entry.holders.end() )
  {
    previousMb = existing->second;
    entry.reservedMb -= previousMb;
    entry.holders.erase( existing );
  }
  if ( entry.reservedMb + vramMb > entry.capacityMb )
  {
    // Restore the previous reservation — a refused upgrade must not lose it.
    entry.holders[holder] = previousMb;
    entry.reservedMb += previousMb;
    if ( why )
      *why = "cuda:" + std::to_string( deviceIndex ) + " has " +
             std::to_string( entry.capacityMb - entry.reservedMb ) + " MiB free of " +
             std::to_string( entry.capacityMb ) + " MiB; the model needs " +
             std::to_string( vramMb ) + " MiB";
    return false;
  }
  entry.holders[holder] = vramMb;
  entry.reservedMb += vramMb;
  return true;
}

void VramLedger::release( int deviceIndex, int vramMb, const std::string &holder )
{
  std::lock_guard<std::mutex> lock( m_mutex );
  const auto deviceIt = m_devices.find( deviceIndex );
  if ( deviceIt == m_devices.end() )
    return;
  Entry &entry = deviceIt->second;
  const auto holderIt = entry.holders.find( holder );
  if ( holderIt == entry.holders.end() )
    return; // idempotent release by contract
  const int released = std::min( holderIt->second, std::max( 0, vramMb ) );
  entry.reservedMb = std::max( 0, entry.reservedMb - released );
  entry.holders.erase( holderIt );
}

int VramLedger::reservedMb( int deviceIndex ) const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  const auto it = m_devices.find( deviceIndex );
  return it == m_devices.end() ? 0 : it->second.reservedMb;
}

int VramLedger::freeMb( int deviceIndex ) const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  const auto it = m_devices.find( deviceIndex );
  if ( it == m_devices.end() || it->second.capacityMb <= 0 )
    return 0; // unknown/unenforced capacity has no meaningful "free"
  return std::max( 0, it->second.capacityMb - it->second.reservedMb );
}

int VramLedger::capacityMb( int deviceIndex ) const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  const auto it = m_devices.find( deviceIndex );
  return it == m_devices.end() ? 0 : it->second.capacityMb;
}

std::vector<VramLedger::DeviceState> VramLedger::snapshot() const
{
  std::lock_guard<std::mutex> lock( m_mutex );
  std::vector<DeviceState> out;
  for ( const auto &[index, entry] : m_devices )
  {
    DeviceState state;
    state.index = index;
    state.capacityMb = entry.capacityMb;
    state.reservedMb = entry.reservedMb;
    state.holders = static_cast<int>( entry.holders.size() );
    out.push_back( state );
  }
  return out;
}

} // namespace sicnu::operators::runtime
