// fault_point.h — deterministic fault injection points (test-only arming).
//
// Every production seam calls SICNU_FAULT_POINT("component.op") at the head
// of its REAL failure branch. The registry (fault_registry.{h,cpp} in
// sicnu_runtime) answers false in ~one relaxed atomic load when nothing is
// armed — the same hot-path budget as execution_telemetry. Tests arm faults
// by name (FaultRegistry::arm / Armed RAII); arming is only ever done from
// test code, never from production bootstrap.
//
// Usage at a production seam (one explicit if, reviewable):
//
//     if ( SICNU_FAULT_POINT( "output_committer.publish" ) )
//         return CommitResult::failure( ...same failure as a real error... );
//
// The point must route through the *production failure path* — a fault point
// never fabricates success and never bypasses error handling; it only makes
// the real code take the real failure branch. Nesting: a consumed NextN fault
// is disarmed before the failure branch runs, so rollback re-entry is
// fault-free; Always re-fires on re-entry.
#pragma once

#include "fault_registry.h"

#define SICNU_FAULT_POINT( name ) \
    ::sicnu::runtime::observability::fault::shouldFail( static_cast<const char *>( name ) )
