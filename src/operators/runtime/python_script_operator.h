#pragma once

#include "operators/framework/rs_operator.h"

namespace sicnu::operators::python
{

class PythonEnvironmentOperator : public RSOperator
{
  public:
    std::string name() const override { return "python:environment"; }
    std::string displayName() const override { return "Python environment"; }
    std::string group() const override { return "python"; }
    std::string description() const override;
    RSOperatorMemoryPolicy memoryPolicy() const override { return RSOperatorMemoryPolicy::UnsupportedForLargeRaster; }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override;
};

class PythonScriptOperator : public RSOperator
{
  public:
    std::string name() const override { return "python:run_script"; }
    std::string displayName() const override { return "Run Python script"; }
    std::string group() const override { return "python"; }
    std::string description() const override;
    RSOperatorMemoryPolicy memoryPolicy() const override { return RSOperatorMemoryPolicy::UnsupportedForLargeRaster; }
    Json::Value schema() const override;
    Json::Value metadata() const override;
    Json::Value executionEstimate() const override;
    Json::Value run( const Json::Value &params, RSOperatorContext &context ) override;
};

} // namespace sicnu::operators::python
