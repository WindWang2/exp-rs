// src/processing/framework/resource_monitor.h
//
// ADR 0063 - Process RSS watermark for TaskCenter memory throttling.
// Samples the resident set size (RSS) of this process and compares it against a
// configurable watermark. TaskCenter consults memoryPressureHigh() before
// launching a new task, so a near-OOM process holds queued tasks until a
// running one finishes and frees memory (processNextQueuedTasks re-enters).
#pragma once

#include <functional>

namespace sicnu {

class ResourceMonitor
{
  public:
    /// Returns current process RSS in MB (0 if unavailable). The default
    /// implementation reports INSTANTANEOUS current RSS (ADR-0063 amendment):
    /// /proc/self/status VmRSS on Linux, mach_task_basic_info resident_size on
    /// macOS, 0 elsewhere. This is current (not peak) so the watermark gate
    /// reopens as soon as memory is freed. Inject a custom sampler for tests.
    using RssSampler = std::function<unsigned int()>;

    ResourceMonitor();

    /// Watermark in MB at/above which TaskCenter stops launching new tasks.
    /// 0 disables the gate. Defaults to 75% of system RAM (or 0 if unknown).
    void setMemoryLimitMb( unsigned int mb );
    unsigned int memoryLimitMb() const;

    /// True when current RSS is at or above the watermark (gate closed).
    /// Returns false when the limit is 0 (disabled) or RSS is unavailable.
    bool memoryPressureHigh() const;

    /// Inject a custom RSS sampler (tests). Pass {} to restore the default.
    void setRssSampler( RssSampler sampler );

    /// Current RSS in MB (for UI / debugging). 0 if unavailable.
    unsigned int currentRssMb() const;

  private:
    unsigned int m_limitMb = 0;
    RssSampler m_sampler;
};

} // namespace sicnu
