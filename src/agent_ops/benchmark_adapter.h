// src/agent_ops/benchmark_adapter.h
#pragma once

//
// Feature B: Benchmark Persistence Adapter — projection half (Qt-free).
// Suite/case report → portable BenchmarkPersistDocument. The Qt-side writer
// that calls ExperimentStore::saveBenchmarkResult lives under
// src/app/agent_ops/ (AgentBench core stays Qt-free).
//

#include "agent_ops/ops_types.h"
#include "agentbench/suite.h"

#include <optional>
#include <string>
#include <vector>

namespace sicnu::agent_ops {

inline constexpr const char *kBenchmarkPersistKind = "sicnu.agent_ops.benchmark_persist/v1";

struct BenchmarkPersistDocument {
    std::string kind = kBenchmarkPersistKind;
    std::string suiteId;
    std::string suiteVersion;
    std::string packDigest;
    std::string resultId;
    std::string status; ///< completed | failed
    Json::Value summary{Json::objectValue};
    Json::Value cases{Json::arrayValue};
    Json::Value rawReport{Json::objectValue};

    Json::Value toJson() const;
    static std::optional<BenchmarkPersistDocument> fromJson(const Json::Value &doc,
                                                            std::string *error = nullptr);
};

class IBenchmarkResultSink {
  public:
    virtual ~IBenchmarkResultSink() = default;
    virtual bool save(const BenchmarkPersistDocument &doc, std::string *error = nullptr) = 0;
};

class InMemoryBenchmarkSink final : public IBenchmarkResultSink {
  public:
    bool save(const BenchmarkPersistDocument &doc, std::string *error = nullptr) override;
    const std::vector<BenchmarkPersistDocument> &saved() const { return mSaved; }

  private:
    std::vector<BenchmarkPersistDocument> mSaved;
};

class BenchmarkAdapter {
  public:
    BenchmarkPersistDocument projectSuiteReport(const sicnu::agentbench::SuiteReport &report) const;
    bool persist(const BenchmarkPersistDocument &doc, IBenchmarkResultSink &sink,
                 std::string *error = nullptr) const;
};

} // namespace sicnu::agent_ops
