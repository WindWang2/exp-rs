// examples/plugins/cpp-operator-demo/demo_operator.h
//
// Minimal third-party operator plugin built ONLY against the installed
// ExpRS SDK (find_package(ExpRS)) — no internal exp-rs headers.
#pragma once

#include "operators/framework/rs_operator.h"

namespace org_example {

/// A trivially reproducible raster-less demo operator: computes a checksum-ish
/// statistic from its parameters, proving the end-to-end registration path:
/// plugin load -> RSOperatorRegistry -> AtomicAlgorithmRegistry -> CLI/MCP.
class DemoStatsOperator : public sicnu::operators::RSOperator
{
public:
    std::string name() const override { return "demo:stats"; }
    std::string displayName() const override { return "Demo Statistics"; }
    std::string group() const override { return "demo"; }
    std::string description() const override
    {
        return "Example plugin operator: aggregates the 'values' parameter into "
               "count/sum/mean. Useful as a conformance smoke test.";
    }
    std::string determinismGrade() const override { return "bit_exact"; }

    Json::Value schema() const override
    {
        Json::Value schema( Json::objectValue );
        schema["type"] = "object";
        Json::Value properties( Json::objectValue );
        Json::Value values( Json::objectValue );
        values["type"] = "array";
        values["description"] = "Numbers to aggregate";
        properties["values"] = values;
        Json::Value label( Json::objectValue );
        label["type"] = "string";
        label["description"] = "Optional label echoed in the result";
        properties["label"] = label;
        schema["properties"] = properties;
        Json::Value required( Json::arrayValue );
        required.append( "values" );
        schema["required"] = required;
        return schema;
    }

    Json::Value run( const Json::Value &params, sicnu::operators::RSOperatorContext &context ) override;

};

} // namespace org_example
