// src/processing/framework/agent_workflow_executor.h
#pragma once

#include "atomic_algorithm_registry.h"
#include <json/json.h>
#include <memory>
#include <string>

namespace sicnu::data {
  class DataManager;
}

namespace sicnu::processing {

class AgentWorkflowExecutor {
public:
  explicit AgentWorkflowExecutor( data::DataManager *dataManager = nullptr );
  ~AgentWorkflowExecutor() = default;

  void setDataManager( data::DataManager *dataManager );
  data::DataManager* dataManager() const;

  /**
   * Executes a single OpenAI / Agent Tool Call JSON request:
   * {
   *   "name": "rs_spectral_index", // or "rs:spectral_index"
   *   "arguments": {
   *     "input": "/path/to/raster.tif",
   *     "index": "NDVI"
   *   }
   * }
   * 
   * Returns a structured JSON result payload:
   * {
   *   "executionTimeMs": 120,
   *   "errorMessage": ""
   * }
   */
  Json::Value executeToolCall( const Json::Value &toolCallJson, ProgressCallback progressCb = nullptr );

  /**
   * Executes a multi-step sequential Agent DAG Plan JSON request:
   * {
   *   "steps": [
   *     { "id": "s1", "name": "rs_spectral_index", "arguments": { ... } },
   *     { "id": "s2", "name": "opencv_gaussian_blur", "arguments": { "input": "$s1.output" } }
   *   ]
   * }
   */
  Json::Value executeAgentPlan( const Json::Value &planJson, ProgressCallback progressCb = nullptr );

private:
  data::DataManager *mDataManager = nullptr;
};

} // namespace sicnu::processing
