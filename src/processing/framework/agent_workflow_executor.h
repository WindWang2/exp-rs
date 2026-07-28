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
   *   "status": "success", // or "error"
   *   "algorithmId": "rs:spectral_index",
   *   "output": { ... },
   *   "executionTimeMs": 120,
   *   "errorMessage": ""
   * }
   */
  Json::Value executeToolCall( const Json::Value &toolCallJson, ProgressCallback progressCb = nullptr );

private:
  data::DataManager *mDataManager = nullptr;
};

} // namespace sicnu::processing
