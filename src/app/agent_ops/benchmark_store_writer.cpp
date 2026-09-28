/***************************************************************************
 * benchmark_store_writer.cpp
 ***************************************************************************/
#include "app/agent_ops/benchmark_store_writer.h"

#include "experiment/experiment_store.h"

namespace sicnu::app::agent_ops {

ExperimentStoreBenchmarkSink::ExperimentStoreBenchmarkSink(
    sicnu::experiment::ExperimentStore *store)
    : m_store(store)
{
}

bool ExperimentStoreBenchmarkSink::save(const sicnu::agent_ops::BenchmarkPersistDocument &doc,
                                        std::string *error)
{
    // #1360 write gate, producer side. BenchmarkPersistDocument has nowhere
    // to carry a geospatial evaluation protocol (no dataset version / split
    // manifest pins), so a projected row can never pass the store's
    // read-back gate — before the store gated writes, this sink silently
    // persisted benchmark results the store itself could never parse back.
    // Fail closed with the typed reason; agentbench reports belong in the
    // agent-ops run record, not the ExperimentStore benchmark_results table.
    Q_UNUSED(doc);
    if (error)
    {
        *error = "agentbench suite results carry no evaluation protocol "
                 "(dataset_version_id / split_manifest_id) required by "
                 "ExperimentStore::saveBenchmarkResult (#1360); persist "
                 "agentbench reports in the agent-ops run record instead";
    }
    return false;
}

} // namespace sicnu::app::agent_ops
