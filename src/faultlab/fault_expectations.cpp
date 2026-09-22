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

    const double delta = clean != nullptr ? faulted->number - clean->number : 0.0;
    result.delta = delta;

    switch ( expectation.relation )
    {
        case ExpectationRelation::Changed:
            result.passed = std::fabs( delta ) > kObservableTolerance;
            if ( clean == nullptr )
            {
                result.note = "clean baseline for this observable is absent";
            }
            break;
        case ExpectationRelation::DeltaGe:
            result.passed = delta >= expectation.value - kObservableTolerance;
            break;
        case ExpectationRelation::DeltaLe:
            result.passed = delta <= expectation.value + kObservableTolerance;
            break;
        case ExpectationRelation::Equals:
            result.passed = std::fabs( delta - expectation.value ) <= kObservableTolerance;
            break;
        case ExpectationRelation::NotEquals:
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
            result.passed = clean != nullptr ? ( faulted->text != clean->text ) : true;
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
            result.passed = clean != nullptr ? ( faulted->truth != clean->truth ) : true;
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
