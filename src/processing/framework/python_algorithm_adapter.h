#pragma once

#include "atomic_algorithm_adapter.h"

#include <functional>
#include <memory>
#include <string>

namespace sicnu::processing {

class PythonAlgorithmAdapter : public AtomicAlgorithmAdapter
{
public:
  using ExecuteCallback =
    std::function<Json::Value( const Json::Value &params, ProgressCallback progressCb,
                               std::function<bool()> isCancelledFn )>;

  PythonAlgorithmAdapter( AlgorithmDescriptor desc, ExecuteCallback executor );
  ~PythonAlgorithmAdapter() override = default;

  std::string algorithmId() const override { return mDesc.id; }
  AlgorithmDescriptor descriptor() const override { return mDesc; }

  Json::Value execute( const Json::Value &params, ProgressCallback progressCb = nullptr,
                       std::function<bool()> isCancelledFn = nullptr ) override;

private:
  AlgorithmDescriptor mDesc;
  ExecuteCallback mExecutor;
};

} // namespace sicnu::processing
