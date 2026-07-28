// src/processing/framework/agent_tool_call_exporter.h
#pragma once

#include "algorithm_descriptor.h"
#include <json/json.h>
#include <string>
#include <vector>

namespace sicnu::processing {

class AgentToolCallExporter {
public:
  /**
   * Converts a list of descriptors into an OpenAI / Qwen Tool Call Function JSON array:
   * [
   *   {
   *     "type": "function",
   *     "function": {
   *       "name": "rs_spectral_index",
   *       "description": "...",
   *       "parameters": { ... }
   *     }
   *   }
   * ]
   */
  static Json::Value exportOpenAiToolDefinitions( const std::vector<AlgorithmDescriptor> &descriptors );

  /**
   * Exports descriptors into a Markdown system prompt tool catalog table for AI Agent prompt injection.
   */
  static std::string exportSystemPromptCatalog( const std::vector<AlgorithmDescriptor> &descriptors );
};

} // namespace sicnu::processing
