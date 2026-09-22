// src/agent/harness/curriculum_availability.h
#pragma once

//
// RS14: the operator/data availability validator for curriculum manifests.
//
// Answers, per module and lab, the teacher's and the agent's question "can
// this course actually run on THIS install?" with typed, machine-readable
// evidence — never with a silent fallback and never by fabricating
// capability.
//
// Three operator states (closed vocabulary):
//   "available"       — registered AND capability-mirror note present
//   "registered_no_capability_note" — registered, mirror entry missing
//                       (warning-grade: runnable, but agents lose grounding)
//   "unknown"         — not registered anywhere (error-grade)
//
// Declared-but-not-guaranteed capabilities (e.g. rs:infer without offline
// model weights) are NOT operators of lab steps; they appear verbatim from
// the manifest's forward_references with state "declared_forward_reference".
// This layer cannot decide a model runtime's availability, so it surfaces
// the declaration (with its reason and wiring note) instead of guessing.
//
// Dependency policy: jsoncpp + std only. The probes are injected; the
// real-registry wiring (RSOperatorRegistry + CapabilityKnowledge) lives in
// curriculum_registry_probe.h, compiled only into sicnu_agent.
//

#include <json/json.h>
#include <functional>
#include <string>

namespace sicnu::agent::harness {

struct CurriculumOperatorProbes
{
    /// True when the operator id resolves against the runtime registry.
    std::function<bool( const std::string & )> registered;
    /// True when the operator has an agent capability-mirror entry.
    std::function<bool( const std::string & )> capabilityNote;
};

/// Builds `sicnu.curriculum.availability/1`:
/// {
///   "schema": "sicnu.curriculum.availability/1",
///   "modules": [ { "module_id", "labs": [ {
///       "lab_id", "resolvable": "labspec"|"registry"|"external"|"unknown",
///       "operators": [ {"operator_id", "state"}... ],     // sorted by id
///       "data_packs": [ {"name", "present": bool}... ]
///   } ] } ],
///   "forward_references": [ {"capability", "state", "reason_zh"?} ]
/// }
///
/// Requires a loaded ("ok") manifest document; anything else returns a
/// document with ok=false and a typed issue. External labs have an empty
/// operator list (their steps belong to the owning track, not this course).
Json::Value buildAvailabilityReport( const Json::Value &manifest,
                                     const std::string &labsDir,
                                     const std::string &packsDir,
                                     const CurriculumOperatorProbes &probes );

} // namespace sicnu::agent::harness
