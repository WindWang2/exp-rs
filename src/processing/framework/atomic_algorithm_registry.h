// src/processing/framework/atomic_algorithm_registry.h
#pragma once

#include "atomic_algorithm_adapter.h"

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace sicnu::processing {

class AtomicAlgorithmRegistry {
public:
  static AtomicAlgorithmRegistry& instance();

  /**
   * Resets the registry state (clears all adapters and re-registers builtins).
   * Useful for test isolation.
   */
  void reset();

  /**
   * Registers an algorithm adapter. Overwrites if algorithmId already exists.
   */
  void registerAdapter( AtomicAlgorithmAdapterPtr adapter );

  /**
   * Unregisters an algorithm adapter by ID.
   */
  bool unregisterAdapter( const std::string &algorithmId );

  /**
   * Finds a registered algorithm adapter by ID.
   * Returns nullptr if not found.
   */
  AtomicAlgorithmAdapterPtr findAdapter( const std::string &algorithmId ) const;

  /**
   * Returns descriptors of all currently registered algorithms.
   */
  std::vector<AlgorithmDescriptor> listDescriptors() const;

  /**
   * Exports all registered algorithms into OpenAI / Qwen Tool Call Function Format JSON array.
   */
  Json::Value exportOpenAiToolDefinitions() const;

  /**
   * Exports all registered algorithms into a Markdown System Prompt tool catalog.
   */
  std::string exportSystemPromptCatalog() const;

  /**
   * Total number of registered adapters.
   */
  size_t adapterCount() const;

  /**
   * Automatically populates built-in RSOperators from RSOperatorRegistry.
   */
  void registerBuiltinRsOperators();

private:
  AtomicAlgorithmRegistry();
  ~AtomicAlgorithmRegistry() = default;

  AtomicAlgorithmRegistry( const AtomicAlgorithmRegistry& ) = delete;
  AtomicAlgorithmRegistry& operator=( const AtomicAlgorithmRegistry& ) = delete;

  mutable std::mutex mMutex;
  std::unordered_map<std::string, AtomicAlgorithmAdapterPtr> mAdapters;
};

} // namespace sicnu::processing
