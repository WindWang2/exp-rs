// fault_expectations.h/.cpp — the expectation engine.
//
// Compares measured observables of a clean run against a faulted run and
// produces per-expectation evidence. A failure is evidence
// (expected/observed/delta + note), never a bare boolean — the same
// graded-failure discipline as the lab grader. An observable the fixture
// never produced fails its expectation with a note instead of being faked.
#pragma once

#include "fault_types.h"

#include <vector>

namespace sicnu::faultlab
{

/// Checks `expectations` against the clean and faulted observable sets.
std::vector<ExpectationResult> checkExpectations(
    const ObservableSet &clean, const ObservableSet &faulted,
    const std::vector<ObservableExpectation> &expectations );

} // namespace sicnu::faultlab
