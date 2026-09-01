// src/processing/framework/algorithm_meta_store.h
#pragma once

#include <json/json.h>

#include "algorithm_descriptor.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace sicnu::processing {

/**
 * One algorithm catalog sidecar entry (ADR 0122 step 5). Deserialized from
 * data/processing/algorithm_meta/<id>.json files; a pure overlay on top of
 * the descriptors the AtomicAlgorithmRegistry already serves.
 */
struct AlgorithmMetaEntry {
  std::string id;       ///< Algorithm id, e.g. "rs:inference"
  std::string task;     ///< Task family: segmentation | classification | ...
  std::string input;    ///< Primary input contract, e.g. "raster"
  std::string output;   ///< Primary output contract, e.g. "polygon"
  bool gpu = false;     ///< Whether the algorithm can/should use a GPU
  double accuracy = -1.0; ///< Optional benchmark accuracy in [0, 1] (<0 = unreported)
  std::string notes;    ///< Human/agent-facing selection guidance
  std::vector<std::string> tags;

  Json::Value toJson() const;
};

/**
 * AlgorithmMetaStore — optional per-algorithm capability manifests loaded
 * from data/processing/algorithm_meta/*.json (ADR 0122). The store is a
 * lookup overlay: it never mutates descriptors, it enriches agent-facing
 * discovery responses (list/search/get schema) with a "catalog" object so
 * agents can match tasks to algorithms without reading code.
 */
class AlgorithmMetaStore {
  public:
    static AlgorithmMetaStore &instance();

    /// Loads every *.json sidecar under dir. Missing dir yields an empty
    /// store. Returns the number of entries loaded.
    size_t loadFromDirectory( const std::string &dir );

    /// Loads from the default sidecar directory
    /// (data/processing/algorithm_meta under the resolved runtime data root).
    size_t loadDefaults();

    std::optional<AlgorithmMetaEntry> find( const std::string &id ) const;
    std::vector<AlgorithmMetaEntry> entries() const;
    size_t size() const;

    /// Descriptor-backed resolution (#707): the AlgorithmDescriptor's
    /// AgentMetadata is the single source of truth. The sidecar survives as
    /// a SPARSE OVERRIDE that must agree with the descriptor: sidecar fields
    /// that contradict a declared descriptor field are dropped (descriptor
    /// wins) and reported via @a drift as "field: sidecar=X descriptor=Y"
    /// entries, so silent capability drift ("descriptor says GPU, sidecar
    /// says no GPU") can be logged and tested instead of shipping both
    /// answers to agents. Fields the descriptor leaves unset pass through
    /// from the sidecar. Returns nullopt when no sidecar exists.
    std::optional<AlgorithmMetaEntry> resolveAgainstDescriptor(
        const std::string &id,
        const AgentMetadata &descriptor,
        std::vector<std::string> *drift = nullptr ) const;

  private:
    AlgorithmMetaStore() = default;

    std::unordered_map<std::string, AlgorithmMetaEntry> mEntries;
};

} // namespace sicnu::processing
