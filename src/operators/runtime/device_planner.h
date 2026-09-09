// src/operators/runtime/device_planner.h — Platform 7.0 deterministic GPU
// device planner: device inventory + per-device VRAM ledger + admission.
//
// This is a PLACEMENT/ADMISSION seam, not a scheduler: it answers "may this
// acquisition land on this device" and keeps a deterministic reservation
// ledger so two model tasks cannot silently over-commit the same card.
// Queueing, retry policy and execution stay with the existing Workflow /
// JobEngine paths. Everything is injectable so multi-device decisions are
// testable on single-GPU (or GPU-less) hosts.
//
// Honest accounting: capacity comes from real enumeration when a provider
// offers it (none does in this build) or from the documented env overrides
// (SICNU_MODEL_CUDA_DEVICES / SICNU_MODEL_VRAM_MB — the budget applies to
// EACH device in the 7.0 ledger). Capacity 0 = unenforced, the historical
// behavior; reservations are still tracked and reported.
#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace sicnu::operators::runtime {

struct ModelHardwareCapabilities; // model_runtime.h (avoid the include cycle)

/// Static description of one CUDA device (Platform 7.0).
struct DeviceInfo
{
  int index = 0;
  std::string name;      ///< honest when unknown: "cuda:<index>"
  int vramCapacityMb = 0; ///< 0 = unknown / unenforced (reservations still tracked)
};

/// Device inventory derived from detected capabilities + env overrides.
/// Deterministic: equal inputs always yield equal inventories.
class DeviceInventory
{
  public:
    /// Builds the inventory for a host: cudaDeviceCount entries, per-device
    /// capacity = the enforced VRAM budget (0 when unset).
    static DeviceInventory fromHardware( const ModelHardwareCapabilities &hw );

    const std::vector<DeviceInfo> &cudaDevices() const { return m_devices; }
    bool empty() const { return m_devices.empty(); }
    /// Device by index (nullopt when not addressable).
    const DeviceInfo *device( int index ) const;

  private:
    std::vector<DeviceInfo> m_devices;
};

/**
 * Per-device VRAM reservation ledger. Thread-safe; every mutation is a
 * deterministic reserve/release pair owned by the session pool. The ledger
 * never queues and never retries — admission is a pure yes/no with a reason.
 */
class VramLedger
{
  public:
    /// Clears every capacity/reservation (test isolation seam).
    void reset();

    /// Sets a device's capacity (0 = unknown/unenforced). Registers the
    /// device in the ledger.
    void setCapacity( int deviceIndex, int capacityMb );

    /// Deterministic admission: reserve @p vramMb on @p deviceIndex against
    /// its capacity. Returns false with @p why when the reservation would
    /// exceed the capacity (unenforced devices always admit). Reservations
    /// of 0 MiB always succeed (no accounting noise for unknown estimates).
    bool tryReserve( int deviceIndex, int vramMb, const std::string &holder, std::string *why = nullptr );

    /// Releases a prior reservation. Unknown (device, holder) pairs are a
    /// no-op — release is idempotent by contract so pool paths stay simple.
    void release( int deviceIndex, int vramMb, const std::string &holder );

    int reservedMb( int deviceIndex ) const;
    /// capacity − reserved; 0 when capacity is unknown/unenforced or the
    /// device is not registered.
    int freeMb( int deviceIndex ) const;
    int capacityMb( int deviceIndex ) const;

    /// Snapshot for observability / pool stats.
    struct DeviceState
    {
      int index = 0;
      int capacityMb = 0;
      int reservedMb = 0;
      int holders = 0;
    };
    std::vector<DeviceState> snapshot() const;

  private:
    struct Entry
    {
      int capacityMb = 0;
      int reservedMb = 0;
      std::map<std::string, int> holders; ///< holder → reserved MiB
    };
    mutable std::mutex m_mutex;
    std::map<int, Entry> m_devices;
};

} // namespace sicnu::operators::runtime
