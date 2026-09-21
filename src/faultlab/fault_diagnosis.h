// fault_diagnosis.h — the teaching-side diagnosis mirror.
//
// The authoritative diagnosis engine is the lab diagnostic brain
// (src/agent/harness/lab_diagnostics.cpp) with its six canonical
// signatures; this module is the *teaching-side mirror* that maps a
// clean-to-faulted observable transition onto the SAME signature
// vocabulary, so a scenario can declare "this fault must be diagnosable as
// signature X" and the runner can check it without dragging the agent
// library into the Qt-free core. The integration test binds the two: the
// real diagnoseLabObservation must agree with this mirror on the
// observations the fixtures can produce.
//
// Signatures (identical ids to the lab catalog):
//   all_negative_index, all_nodata, kappa_near_zero, blank_change_mask,
//   crs_mismatch, scale_stripes — plus the typed diagnostic.unmatched for
// faults no canonical signature covers (never a silent fallback).
#pragma once

#include "fault_registry.h"
#include "fault_types.h"

#include <string>

namespace sicnu::faultlab
{

/// Maps the clean→faulted observable transition onto the canonical
/// signature vocabulary. Deterministic; unmatched inputs return
/// kDiagnosisUnmatched.
std::string diagnoseTransition( const ObservableSet &clean, const ObservableSet &faulted );

} // namespace sicnu::faultlab
