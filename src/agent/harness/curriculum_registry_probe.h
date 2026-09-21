// src/agent/harness/curriculum_registry_probe.h
#pragma once

//
// RS14: real-registry wiring for the curriculum availability validator.
//
// This TU is the ONLY place where the curriculum seam touches the runtime
// registries (RSOperatorRegistry + CapabilityKnowledge). It is compiled into
// sicnu_agent so the pure-C++ curriculum tests stay dependency-light.
//

#include "agent/harness/curriculum_availability.h"

namespace sicnu::agent::harness {

/// Probes backed by RSOperatorRegistry::hasOperator (existence) and
/// CapabilityKnowledge::entryForOperator (agent grounding note). Both
/// singletons auto-load on first query; neither is modified here.
CurriculumOperatorProbes defaultCurriculumProbes();

} // namespace sicnu::agent::harness
