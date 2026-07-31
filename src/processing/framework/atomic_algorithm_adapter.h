// src/processing/framework/atomic_algorithm_adapter.h
#pragma once

#include "algorithm_descriptor.h"
#include "operators/framework/rs_operator.h"

#include <memory>
#include <functional>

class QgsProcessingAlgorithm;

namespace sicnu::processing {

using ProgressCallback = std::function<void( int percent, const std::string &message )>;

class AtomicAlgorithmAdapter
{
public:
  virtual ~AtomicAlgorithmAdapter() = default;

  virtual std::string algorithmId() const = 0;
  virtual AlgorithmDescriptor descriptor() const = 0;

  virtual Json::Value execute( const Json::Value &params, ProgressCallback progressCb = nullptr ) = 0;
};

using AtomicAlgorithmAdapterPtr = std::shared_ptr<AtomicAlgorithmAdapter>;

class RsOperatorAdapter : public AtomicAlgorithmAdapter
{
public:
  explicit RsOperatorAdapter( std::unique_ptr<operators::RSOperator> op );
  ~RsOperatorAdapter() override = default;

  std::string algorithmId() const override;
  AlgorithmDescriptor descriptor() const override;
  Json::Value execute( const Json::Value &params, ProgressCallback progressCb = nullptr ) override;

private:
  std::unique_ptr<operators::RSOperator> mOp;
  AlgorithmDescriptor mDesc;
};

class AlgorithmDescriptorBuilder
{
public:
  static AlgorithmDescriptor buildFromRsOperator( const operators::RSOperator &op );
  static AlgorithmDescriptor buildFromQgsAlgorithm( const QgsProcessingAlgorithm &alg );
};

} // namespace sicnu::processing
