// fault_expectations.cpp — the expectation engine (see header).
#include "fault_expectations.h"

#include "fault_observables.h"

#include <cmath>
#include <string>

namespace sicnu::faultlab
{

namespace
{

ExpectationResult missingObservable( const ObservableExpectation &expectation )
{
    ExpectationResult result;
    result.id = expectation.id;
    result.relation = expectation.relation;
    result.passed = false;
    result.note = "observable not produced by this fixture";
    return result;
}

ExpectationResult checkNumber( const ObservableExpectation &expectation,
                               const Observable *clean, const Observable *faulted )
{
    ExpectationResult result;
    result.id = expectation.id;
    result.relation = expectation.relation;
    result.expected = expectation.value;
    result.observed = faulted->number;

    // Delta relations judge a CHANGE; without a clean baseline the change
    // is unprovable, not zero (R18-E V3 — the same discipline Changed,
    // Text and Truth already carry). The old fallback fabricated
    // delta = 0.0, so `equals 0` / `delta_ge -1` / `delta_le 1` passed
    // vacuously on zero evidence.
    // A text operand on a NUMBER observable cannot be evaluated — the
    // numeric relations below read expectation.value only, so a
    // value-less equals with a stray "text" member used to grade against
    // the silent 0.0 default (a vacuous pass, R2 review). Fail closed.
    if ( !expectation.text.empty() && expectation.relation != ExpectationRelation::InRange )
    {
        result.passed = false;
        result.note = "text operand declared for a numeric relation on a number observable";
        return result;
    }

    const bool hasBaseline = clean != nullptr;
    const double delta = hasBaseline ? faulted->number - clean->number : 0.0;
    result.delta = delta;

    // delta_ge/delta_le/equals/not_equals are all statements about the
    // change, so a missing clean baseline makes them unprovable, not
    // vacuously true: with delta fabricated as 0.0 they could pass on
    // zero evidence (delta_le with a non-negative operand, delta_ge with
    // a non-positive one, equals/not_equals around 0). Same
    // baseline-absent discipline as Changed above and the text/truth
    // paths — an expectation that only exists after fault injection must
    // not pass on zero evidence. in_range needs no baseline: it bounds
    // the faulted value itself.
    const bool deltaNeedsBaseline =
        clean == nullptr &&
        ( expectation.relation == ExpectationRelation::DeltaGe ||
          expectation.relation == ExpectationRelation::DeltaLe ||
          expectation.relation == ExpectationRelation::Equals ||
          expectation.relation == ExpectationRelation::NotEquals );
    if ( deltaNeedsBaseline )
    {
        result.passed = false;
        result.note = "clean baseline for this observable is absent";
        return result;
    }

    switch ( expectation.relation )
    {
        case ExpectationRelation::Changed:
            result.passed = hasBaseline && std::fabs( delta ) > kObservableTolerance;
            if ( !hasBaseline )
            {
                result.note = "clean baseline for this observable is absent";
            }
            break;
        case ExpectationRelation::DeltaGe:
            if ( !hasBaseline )
            {
                result.passed = false;
                result.note = "clean baseline for this observable is absent";
                break;
            }
            result.passed = delta >= expectation.value - kObservableTolerance;
            break;
        case ExpectationRelation::DeltaLe:
            if ( !hasBaseline )
            {
                result.passed = false;
                result.note = "clean baseline for this observable is absent";
                break;
            }
            result.passed = delta <= expectation.value + kObservableTolerance;
            break;
        case ExpectationRelation::Equals:
            if ( !hasBaseline )
            {
                result.passed = false;
                result.note = "clean baseline for this observable is absent";
                break;
            }
            result.passed = std::fabs( delta - expectation.value ) <= kObservableTolerance;
            break;
        case ExpectationRelation::NotEquals:
            if ( !hasBaseline )
            {
                result.passed = false;
                result.note = "clean baseline for this observable is absent";
                break;
            }
            result.passed = std::fabs( delta - expectation.value ) > kObservableTolerance;
            break;
        case ExpectationRelation::InRange:
            result.passed = faulted->number >= expectation.rangeLo - kObservableTolerance &&
                           faulted->number <= expectation.rangeHi + kObservableTolerance;
            result.expected = expectation.rangeLo;
            result.note = "in_range [" + std::to_string( expectation.rangeLo ) + ", " +
                          std::to_string( expectation.rangeHi ) + "]";
            break;
        case ExpectationRelation::TruthIs:
            result.passed = ( faulted->truth == ( expectation.value != 0.0 ) );
            result.observed = faulted->truth ? 1.0 : 0.0;
            break;
    }
    return result;
}

ExpectationResult checkText( const ObservableExpectation &expectation, const Observable *clean,
                             const Observable *faulted )
{
    ExpectationResult result;
    result.id = expectation.id;
    result.relation = expectation.relation;
    result.expectedText = expectation.text;
    result.observedText = faulted->text;

    switch ( expectation.relation )
    {
        case ExpectationRelation::Equals:
            result.passed = faulted->text == expectation.text;
            break;
        case ExpectationRelation::NotEquals:
            result.passed = faulted->text != expectation.text;
            break;
        case ExpectationRelation::Changed:
            // Without a clean baseline "changed" is unprovable, not vacuously
            // true — the number path fails with the same note when the
            // clean observable is absent, and an expectation that only
            // exists after fault injection must not pass on zero evidence.
            if ( clean == nullptr )
            {
                result.passed = false;
                result.note = "clean baseline for this observable is absent";
            }
            else
            {
                result.passed = faulted->text != clean->text;
            }
            break;
        default:
            result.passed = false;
            result.note = "relation not defined for text observables";
            break;
    }
    return result;
}

ExpectationResult checkTruth( const ObservableExpectation &expectation, const Observable *clean,
                              const Observable *faulted )
{
    ExpectationResult result;
    result.id = expectation.id;
    result.relation = expectation.relation;
    result.expected = expectation.value;
    result.observed = faulted->truth ? 1.0 : 0.0;

    switch ( expectation.relation )
    {
        case ExpectationRelation::TruthIs:
            result.passed = ( faulted->truth == ( expectation.value != 0.0 ) );
            break;
        case ExpectationRelation::Changed:
            // Same baseline-absent discipline as checkNumber/checkText: the
            // vacuous true let a fault-only truth observable pass Changed
            // with no clean evidence at all.
            if ( clean == nullptr )
            {
                result.passed = false;
                result.note = "clean baseline for this observable is absent";
            }
            else
            {
                result.passed = faulted->truth != clean->truth;
            }
            break;
        default:
            result.passed = false;
            result.note = "relation not defined for truth observables";
            break;
    }
    return result;
}

} // namespace

std::vector<ExpectationResult> checkExpectations(
    const ObservableSet &clean, const ObservableSet &faulted,
    const std::vector<ObservableExpectation> &expectations )
{
    std::vector<ExpectationResult> results;
    results.reserve( expectations.size() );

    for ( const auto &expectation : expectations )
    {
        const Observable *faultedObservable = findObservable( faulted, expectation.id );
        if ( faultedObservable == nullptr )
        {
            results.push_back( missingObservable( expectation ) );
            continue;
        }
        const Observable *cleanObservable = findObservable( clean, expectation.id );

        switch ( faultedObservable->kind )
        {
            case ObservableKind::Number:
                results.push_back( checkNumber( expectation, cleanObservable, faultedObservable ) );
                break;
            case ObservableKind::Text:
                results.push_back( checkText( expectation, cleanObservable, faultedObservable ) );
                break;
            case ObservableKind::Truth:
                results.push_back( checkTruth( expectation, cleanObservable, faultedObservable ) );
                break;
        }
    }
    return results;
}

} // namespace sicnu::faultlab
