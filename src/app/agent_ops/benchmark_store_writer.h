/***************************************************************************
 * benchmark_store_writer.h — Qt-side ExperimentStore adapter (Feature B)
 *
 * Projects BenchmarkPersistDocument into ExperimentStore::saveBenchmarkResult.
 * AgentBench core remains Qt-free; this translation unit is the only Qt link.
 ***************************************************************************/
#pragma once

#include "agent_ops/benchmark_adapter.h"

#include <QString>
#include <memory>
#include <string>

namespace sicnu::experiment {
class ExperimentStore;
class BenchmarkResult;
}

namespace sicnu::app::agent_ops {

/// Builds a BenchmarkResult value object from a persist document (no I/O).
sicnu::experiment::BenchmarkResult toBenchmarkResult(
    const sicnu::agent_ops::BenchmarkPersistDocument &doc);

class ExperimentStoreBenchmarkSink final : public sicnu::agent_ops::IBenchmarkResultSink
{
  public:
    explicit ExperimentStoreBenchmarkSink(sicnu::experiment::ExperimentStore *store);

    bool save(const sicnu::agent_ops::BenchmarkPersistDocument &doc,
              std::string *error = nullptr) override;

  private:
    sicnu::experiment::ExperimentStore *m_store = nullptr;
};

} // namespace sicnu::app::agent_ops
