// src/agent_ops/delivery_assembler.h
#pragma once

//
// Feature G: FinalDelivery assembler.
// Machine-readable delivery from SessionResult + optional capsule/trace/benchmark
// refs. Round-trippable via FinalDelivery::fromJson.
//

#include "agent_ops/ops_types.h"
#include "agent_loop/scientific_agent_session.h"
#include "agentbench/trace.h"

#include <optional>
#include <string>

namespace sicnu::agent_ops {

struct DeliveryExtras {
    Json::Value capsule{Json::objectValue};
    Json::Value benchmarkRefs{Json::arrayValue};
    std::optional<sicnu::agentbench::AgentTrace> trace;
    Json::Value questions{Json::arrayValue};
    Json::Value claims{Json::arrayValue};
};

class DeliveryAssembler {
  public:
    FinalDelivery assemble(const sicnu::agent_loop::SessionResult &result,
                           const DeliveryExtras &extras = {}) const;

    /// Capsule-oriented export document (portable refs only; no absolute paths).
    Json::Value capsuleExportDocument(const FinalDelivery &delivery) const;
};

} // namespace sicnu::agent_ops
