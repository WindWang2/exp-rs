// src/agent/harness/autonomy_tools.h
#pragma once

//
// RS14-12: the autonomy status tool surface.
//
// harness:autonomy_status is the read-only, machine-readable projection of
// the effective autonomy policy (sicnu.autonomy-status/1): the current
// level, the mode, and per capability whether it is allowed, limited, or
// forbidden — with typed reasons. It is what the UI renders and what a
// future agent reads; it never decides anything itself (the projection is a
// pure function of the same engine the gates call).

namespace sicnu::agent::harness {

/// Registers harness:autonomy_status. Idempotent; called from
/// SpatialToolRegistry::registerBuiltinTools().
void registerAutonomyTools();

} // namespace sicnu::agent::harness
