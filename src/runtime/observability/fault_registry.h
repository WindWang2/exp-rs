// fault_registry.h — deterministic fault injection (task B of Verification 7.0).
//
// Compiled into sicnu_runtime (leaf, Qt-free). The fast path is one relaxed
// atomic load of the global armed-fault count, shared with
// execution_telemetry's hot-path contract: enabled-nothing costs ~nothing,
// and arming happens only from test code.
//
// Semantics (deterministic; no sleeps, no filesystem tricks):
//   * arm( {name, mode, count, payload} ) — before exercising the seam. Modes:
//       NextN    — fires true for the next `count` calls, then disarms.
//       Always   — fires true until disarm(name)/disarmAll().
//       EveryNth — fires true on every n-th call (count = n).
//   * shouldFail(name) — the production-site probe. Mutex-guarded map only on
//     the armed path; the mutex is never held across user code, so a fault
//     site may re-enter (nested publish) without deadlock. Nesting semantics
//     are mode-defined: a NextN fault consumed by the outer firing is
//     disarmed, so cleanup/rollback re-entry runs fault-free; an Always fault
//     re-fires on re-entry (arm that deliberately when a test wants the
//     rollback path to fail too). There is deliberately NO suspend guard —
//     prediction beats interception for deterministic tests.
//   * payload(name) — optional annotation the site may use (e.g. to write a
//     truncated buffer). Empty when nothing armed.
//   * Armed RAII hard-disarms everything on scope exit so a failing assertion
//     cannot leak faults into other tests.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace sicnu::runtime::observability::fault
{

enum class Mode : uint8_t
{
    NextN,
    Always,
    EveryNth
};

struct FaultAction
{
    std::string name;
    Mode mode = Mode::NextN;
    uint32_t count = 1; ///< NextN: firings; EveryNth: period
    std::string payload;
};

/// Production-site probe; true when an armed fault fires at this call.
/// A NextN fault that re-enters the same seam during its failure path has
/// already consumed (and disarmed) itself, so cleanup/rollback paths run
/// fault-free — arm additional firings explicitly when a test wants the
/// rollback to fail too.
bool shouldFail( const std::string &name );
/// Literal-string overload: checks the armed counter BEFORE constructing a
/// std::string, so a disarmed probe performs no heap allocation (the
/// documented "one relaxed atomic load" budget is literal for call sites
/// that pass a string literal, i.e. every SICNU_FAULT_POINT site).
bool shouldFail( const char *name );

/// Test-side control.
void armFault( const FaultAction &action );
void disarmFault( const std::string &name );
void disarmAllFaults();
std::string faultPayload( const std::string &name );
/// Number of currently armed faults (0 = global fast path).
uint32_t armedFaultCount();

/// Safety net for tests: arm on construction, disarmAll on destruction —
/// even when an assertion throws mid-test.
class ArmedFault
{
  public:
    explicit ArmedFault( FaultAction action );
    ~ArmedFault();
    ArmedFault( const ArmedFault & ) = delete;
    ArmedFault &operator=( const ArmedFault & ) = delete;
};

} // namespace sicnu::runtime::observability::fault
