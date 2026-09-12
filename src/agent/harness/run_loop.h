// src/agent/harness/run_loop.h
#pragma once

//
// Harness 9.0 (M5): observe → verify → repair loop, as Pi-invocable harness
// actions — never a second agent loop.
//
// `harness:diagnose_run` reads a run's authoritative state (engine state,
// per-step errors, persisted verification sidecars, declared plan outputs)
// and emits STRUCTURED repair proposals. Every proposal resolves through the
// closed action vocabulary (#881) and carries an explicit kind and risk.
// The tool keeps a bounded per-run diagnose budget: when it is exhausted the
// document says `stop: true` with a typed reason, so a poll loop cannot
// repair forever. The tool itself never executes a repair — it proposes;
// Pi (or the human) decides and calls the proposed surface.
//

#include <json/json.h>
#include <string>

namespace sicnu::agent::harness {

/// Registers harness:diagnose_run on the SpatialToolRegistry.
void registerRunLoopTools();

} // namespace sicnu::agent::harness
