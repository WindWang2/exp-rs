/***************************************************************************
 * benchmark_store_writer.h — Qt-side ExperimentStore adapter (Feature B)
 *
 * AgentBench core remains Qt-free; this translation unit is the only Qt link.
 * The store sink is a deliberate fail-closed boundary: agentbench suite
 * results carry no geospatial evaluation protocol, so they can never satisfy
 * the ExperimentStore benchmark-result contract (#1360) and are refused with
 * a typed reason instead of being persisted unreadably.
 ***************************************************************************/
#pragma once

#include "agent_ops/benchmark_adapter.h"

#include <string>

namespace sicnu::experiment {
class ExperimentStore;
}

namespace sicnu::app::agent_ops {

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
