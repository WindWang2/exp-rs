// src/operators/runtime/nvml_inventory.h — Platform 9.0 real NVIDIA device
// enumeration. The 7.0/8.0 device planner inferred devices from env vars
// only; this module asks the ACTUAL driver through NVML, loaded with dlopen
// (no link-time dependency — hosts without NVIDIA keep building and keep
// the historical behavior). The env overrides in
// ModelHardwareCapabilities::detect() remain the test seams and win over
// NVML, so tests stay deterministic on every host.
#pragma once

#include <string>
#include <vector>

namespace sicnu::operators::runtime {

/// One real CUDA device as reported by the NVIDIA driver.
struct NvidiaDeviceInfo
{
  int index = 0;
  std::string name;        ///< product name ("cuda:<i>" when the driver gives none)
  int totalVramMb = 0;     ///< 0 = unknown
  int freeVramMb = -1;     ///< -1 = unknown (never fabricated)
  int computeMajor = 0;    ///< 0 = unknown
  int computeMinor = 0;
};

/// Result of one NVML probe. `available` is false on non-NVIDIA hosts,
/// driver-less hosts, or when the probe is disabled — @p unavailableReason
/// says which, honestly.
struct NvidiaInventory
{
  bool available = false;
  std::string unavailableReason;
  std::vector<NvidiaDeviceInfo> devices;

  /// Probes the driver NOW (each call re-reads free VRAM — callers that need
  /// an admission-decision snapshot probe fresh; detect() caches its result).
  /// SICNU_MODEL_NO_NVML=1 disables the probe (deterministic tests).
  static NvidiaInventory probe();
};

} // namespace sicnu::operators::runtime
