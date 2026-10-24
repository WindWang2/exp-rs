// fault_types.cpp — value object serialization and the expectation relation
// vocabulary.
#include "fault_types.h"

#include <cmath>
#include <cstdint>
#include <string>

namespace sicnu::faultlab
{

Json::Value FaultDiagnostic::toJson() const
{
    Json::Value doc( Json::objectValue );
    doc["code"] = code;
    doc["message"] = message;
    switch ( severity )
    {
        case FaultSeverity::Info:
            doc["severity"] = "info";
            break;
        case FaultSeverity::Warning:
            doc["severity"] = "warning";
            break;
        case FaultSeverity::Error:
            doc["severity"] = "error";
            break;
    }
    return doc;
}

int FaultGrid::bandIndexByRole( const std::string &role ) const
{
    for ( std::size_t i = 0; i < bands.size(); ++i )
    {
        if ( bands[i].role == role )
        {
            return static_cast<int>( i );
        }
    }
    return -1;
}

Json::Value BandSpec::toJson() const
{
    Json::Value doc( Json::objectValue );
    doc["role"] = role;
    Json::Value samples( Json::arrayValue );
    for ( const double sample : this->samples )
    {
        // NaN no-data samples serialize as the NaN literal; the canonical
        // form digests the exact IEEE bit pattern's text.
        samples.append( sample );
    }
    doc["samples"] = samples;
    doc["scale"] = scale;
    doc["offset"] = offset;
    if ( !acquisitionDate.empty() )
    {
        doc["acquisition_date"] = acquisitionDate;
    }
    return doc;
}

Json::Value FaultGrid::toJson() const
{
    Json::Value doc( Json::objectValue );
    doc["width"] = width;
    doc["height"] = height;
    doc["crs"] = crsId;
    Json::Value gt( Json::arrayValue );
    for ( const double value : geoTransform )
    {
        gt.append( value );
    }
    doc["geo_transform"] = gt;
    doc["has_nodata"] = hasNoData;
    doc["nodata_value"] = noDataValue;
    Json::Value bandsJson( Json::arrayValue );
    for ( const auto &band : bands )
    {
        bandsJson.append( band.toJson() );
    }
    doc["bands"] = bandsJson;
    doc["extras"] = extras;
    return doc;
}

Json::Value Observable::toJson() const
{
    Json::Value doc( Json::objectValue );
    doc["id"] = id;
    switch ( kind )
    {
        case ObservableKind::Number:
            doc["kind"] = "number";
            doc["value"] = number;
            break;
        case ObservableKind::Text:
            doc["kind"] = "text";
            doc["value"] = text;
            break;
        case ObservableKind::Truth:
            doc["kind"] = "truth";
            doc["value"] = truth;
            break;
    }
    return doc;
}

const char *expectationRelationName( ExpectationRelation relation )
{
    switch ( relation )
    {
        case ExpectationRelation::Equals:
            return "equals";
        case ExpectationRelation::NotEquals:
            return "not_equals";
        case ExpectationRelation::Changed:
            return "changed";
        case ExpectationRelation::DeltaGe:
            return "delta_ge";
        case ExpectationRelation::DeltaLe:
            return "delta_le";
        case ExpectationRelation::InRange:
            return "in_range";
        case ExpectationRelation::TruthIs:
            return "truth_is";
    }
    return "changed";
}

bool parseExpectationRelation( const std::string &text, ExpectationRelation &out )
{
    if ( text == "equals" )
    {
        out = ExpectationRelation::Equals;
    }
    else if ( text == "not_equals" )
    {
        out = ExpectationRelation::NotEquals;
    }
    else if ( text == "changed" )
    {
        out = ExpectationRelation::Changed;
    }
    else if ( text == "delta_ge" )
    {
        out = ExpectationRelation::DeltaGe;
    }
    else if ( text == "delta_le" )
    {
        out = ExpectationRelation::DeltaLe;
    }
    else if ( text == "in_range" )
    {
        out = ExpectationRelation::InRange;
    }
    else if ( text == "truth_is" )
    {
        out = ExpectationRelation::TruthIs;
    }
    else
    {
        return false;
    }
    return true;
}

Json::Value ObservableExpectation::toJson() const
{
    Json::Value doc( Json::objectValue );
    doc["id"] = id;
    doc["relation"] = expectationRelationName( relation );
    if ( relation == ExpectationRelation::InRange )
    {
        doc["range_lo"] = rangeLo;
        doc["range_hi"] = rangeHi;
    }
    else
    {
        doc["value"] = value;
    }
    if ( !text.empty() )
    {
        doc["text"] = text;
    }
    return doc;
}

Json::Value ExpectationResult::toJson() const
{
    Json::Value doc( Json::objectValue );
    doc["id"] = id;
    doc["relation"] = expectationRelationName( relation );
    doc["passed"] = passed;
    doc["expected"] = expected;
    doc["observed"] = observed;
    doc["delta"] = delta;
    if ( !expectedText.empty() || !observedText.empty() )
    {
        doc["expected_text"] = expectedText;
        doc["observed_text"] = observedText;
    }
    if ( !note.empty() )
    {
        doc["note"] = note;
    }
    return doc;
}

Json::Value FaultSpec::toJson() const
{
    Json::Value doc( Json::objectValue );
    doc["family"] = familyId;
    doc["params"] = params;
    doc["seed"] = seed;
    return doc;
}

Json::Value FaultScenario::toJson() const
{
    Json::Value doc( Json::objectValue );
    doc["schema_version"] = kFaultScenarioSchemaId;
    doc["scenario_id"] = scenarioId;
    doc["title"] = title;
    doc["title_zh"] = titleZh;
    doc["fault"] = fault.toJson();
    doc["base_fixture"] = Json::Value( Json::objectValue );
    doc["base_fixture"]["fixture_id"] = fixtureId;
    doc["base_fixture"]["params"] = fixtureParams;
    doc["base_fixture"]["seed"] = fixtureSeed;
    doc["sandbox"] = Json::Value( Json::objectValue );
    doc["sandbox"]["class"] = sandboxClass;
    doc["sandbox"]["max_bytes"] = Json::Value::UInt64( maxBytes );
    Json::Value expectations( Json::arrayValue );
    for ( const auto &expectation : this->expectations )
    {
        expectations.append( expectation.toJson() );
    }
    doc["expected"] = Json::Value( Json::objectValue );
    doc["expected"]["observables"] = expectations;
    doc["expected"]["diagnosis"] = Json::Value( Json::objectValue );
    doc["expected"]["diagnosis"]["signature"] = expectedDiagnosisSignature;
    if ( !verifier.isNull() && verifier.isObject() && !verifier.getMemberNames().empty() )
    {
        doc["expected"]["verifier"] = verifier;
    }
    doc["learning_objective"] = Json::Value( Json::objectValue );
    doc["learning_objective"]["id"] = learningObjectiveId;
    doc["learning_objective"]["statement"] = learningObjective;
    doc["learning_objective"]["statement_zh"] = learningObjectiveZh;
    return doc;
}

bool parseFaultVerifierSpec( const Json::Value &verifier, FaultVerifierSpec &out,
                             std::string &error )
{
    const auto fail = [ &error ]( const std::string &reason ) {
        error = reason;
        return false;
    };
    if ( !verifier.isObject() )
        return fail( "expected.verifier must be an object" );
    for ( const std::string &member : verifier.getMemberNames() )
    {
        if ( member != "rules_ref" && member != "must_fail_assertions" )
            return fail( "expected.verifier: unknown member '" + member + "'" );
    }
    FaultVerifierSpec parsed;
    if ( verifier.isMember( "rules_ref" ) )
    {
        if ( !verifier["rules_ref"].isString() || verifier["rules_ref"].asString().empty() )
            return fail( "expected.verifier.rules_ref must be a non-empty string" );
        parsed.rulesRef = verifier["rules_ref"].asString();
    }
    if ( verifier.isMember( "must_fail_assertions" ) )
    {
        if ( !verifier["must_fail_assertions"].isArray() )
            return fail( "expected.verifier.must_fail_assertions must be an array" );
        for ( const Json::Value &id : verifier["must_fail_assertions"] )
        {
            if ( !id.isString() || id.asString().empty() )
                return fail(
                  "expected.verifier.must_fail_assertions entries must be non-empty strings" );
            parsed.mustFailAssertions.push_back( id.asString() );
        }
        // must-fail assertions without rules to grade against are inert by
        // construction — refuse rather than accept a declaration that cannot
        // be honored (#1464 discipline).
        if ( !parsed.mustFailAssertions.empty() && parsed.rulesRef.empty() )
            return fail( "expected.verifier.must_fail_assertions requires rules_ref" );
    }
    // The mirror inert shape, checked OUTSIDE the member block so it also
    // catches a rules_ref with the must_fail member REMOVED: rules to grade
    // against but no assertion that must fail. The declaration would
    // observe nothing and fold in a vacuous "true" — the #1464
    // accepted-but-inert shape one level down (R1b). A verifier declaration
    // must name what the fault must break, or it means nothing.
    if ( !parsed.rulesRef.empty() && parsed.mustFailAssertions.empty() )
        return fail( "expected.verifier.rules_ref requires at least one "
                     "must_fail_assertions entry" );
    out = parsed;
    error.clear();
    return true;
}

} // namespace sicnu::faultlab
