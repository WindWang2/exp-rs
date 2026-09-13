// execution_resource_bridge.h — LSEE 10.0 (ADR 0148 §6): feed TaskCenter's
// resource admission dimensions from the EXISTING device/resource truths.
//
// This is a read-only consumer seam, not a second detector: GPU VRAM truth
// stays with the Model Runtime's NVML inventory (src/operators/runtime),
// and the memory watermark stays with ResourceMonitor. The bridge only
// copies facts into the admission configuration at wire-up time.
#pragma once

#include <cstdint>
#include <string>

namespace sicnu::operators::runtime {
struct NvidiaInventory; // F-B-4: injectable probe-result parameter
} // namespace sicnu::operators::runtime

namespace sicnu {

class TaskCenter;

namespace processing {

/// Result of one wire-up attempt (each field maps to a distinct, honest
/// outcome — nothing is claimed that was not probed).
struct VramWireResult
{
    bool wired = false;          ///< true when the vram budget was set
    unsigned int budgetMb = 0;   ///< the budget that was set
    int deviceCount = 0;         ///< devices reported by the probe
    /// Max free VRAM across devices at probe time (MiB; -1 unknown).
    int freeVramMb = -1;
    /// Human-readable reason when !wired (no device truth / probe disabled).
    /// std::string, NOT const char*: the probe's reason lives in a local
    /// object that dies before the caller reads this (F-A-4).
    std::string unavailableReason;
};

/// Probes the Model Runtime's NVML inventory and, when real device truth
/// exists, configures @p center's VRAM admission budget to the LARGEST
/// device's total VRAM minus @p reservePercent (the Model Session Pool keeps
/// its own per-device admission — this budget governs the task-level vram
/// dimension only). A host without NVIDIA device truth keeps its previous
/// budget untouched (wired=false; never fabricated). HOST OPT-IN: nothing
/// calls this by default — the vram gate stays off unless a host (or test)
/// activates it (DECISIONS D-6). @p inventoryOverride injects a probe result
/// for tests (F-B-4); null probes the real driver.
VramWireResult wireVramBudgetFromDeviceTruth(
  TaskCenter &center, unsigned int reservePercent = 10,
  const operators::runtime::NvidiaInventory *inventoryOverride = nullptr );

/// Idempotently installs the DEFAULT execution environment pin provider
/// (closed set v1: the GDAL release version). Thread-safe. Every seam that
/// produces or consumes implementation identities calls this — TaskCenter
/// and WorkflowRunCoordinator — so identity bytes never depend on which
/// singleton a caller touched first (F-A-11: an identity computed before
/// the pin install would silently use empty pins and diverge afterwards).
void installExecutionEnvironmentPins();

} // namespace processing
} // namespace sicnu
