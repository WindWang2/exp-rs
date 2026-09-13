// execution_resource_bridge.h — LSEE 10.0 (ADR 0148 §6): feed TaskCenter's
// resource admission dimensions from the EXISTING device/resource truths.
//
// This is a read-only consumer seam, not a second detector: GPU VRAM truth
// stays with the Model Runtime's NVML inventory (src/operators/runtime),
// and the memory watermark stays with ResourceMonitor. The bridge only
// copies facts into the admission configuration at wire-up time.
#pragma once

#include <cstdint>

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
    const char *unavailableReason = nullptr;
};

/// Probes the Model Runtime's NVML inventory and, when real device truth
/// exists, configures @p center's VRAM admission budget to the LARGEST
/// device's total VRAM minus @p reservePercent (the Model Session Pool keeps
/// its own per-device admission — this budget governs the task-level vram
/// dimension only). A host without NVIDIA device truth keeps its previous
/// budget untouched (wired=false; never fabricated).
VramWireResult wireVramBudgetFromDeviceTruth( TaskCenter &center,
                                              unsigned int reservePercent = 10 );

} // namespace processing
} // namespace sicnu
