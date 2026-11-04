// fault_observables.h/.cpp — observable measurement.
//
// Observables are the measured facts a scenario declares expectations
// against. Measurement is a pure function of the (clean or faulted) grid:
// no wall clock, no RNG, no I/O. Identifier conventions (stable, prefixed
// so the family registry can reference them as prefixes):
//
//   band_count, band_roles(text), crs(text)
//   geo_transform.origin_x / geo_transform.origin_y
//   nodata_fraction, finite_fraction, valid_fraction
//   band.mean.<role> / band.min.<role> / band.max.<role>
//   band.scale.<role> / band.offset.<role>
//   acquisition_dates(text), index_mean
//   leakage.overlap_fraction, leakage.test_count
//   threshold, positive_fraction, kappa
//   channel_order(text), model_output_mean
//   provenance.generator_present
//
// An observable is emitted only when its inputs exist on the grid (a role
// absent from the fixture, an extras block never declared): a missing
// observable is reported as such, never faked with a default.
#pragma once

#include "fault_types.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>

namespace sicnu::faultlab
{

/// Relative tolerance for "unchanged" comparisons of same-fixture
/// observables.
// swarm-review LT1-6: the constant was documented as relative but consumed
// as an absolute band everywhere; the semantics below make the
// implementation match the contract.
inline constexpr double kObservableTolerance = 1e-9;

/// Tolerance band around a compared quantity: kObservableTolerance scaled
/// by the magnitude of @p reference, with the constant itself as the floor
/// for near-zero operands (so zero-scale comparisons stay absolute).
inline double observableToleranceBand( double reference )
{
    return kObservableTolerance * std::max( 1.0, std::fabs( reference ) );
}

/// Measures every applicable observable of `grid`.
ObservableSet measureObservables( const FaultGrid &grid );

/// Looks up an observable; nullptr when the fixture never produced it.
const Observable *findObservable( const ObservableSet &set, const std::string &id );

} // namespace sicnu::faultlab
