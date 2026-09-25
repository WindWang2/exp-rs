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
    /// Optional unified Scientific Verifier report document
    /// ("sicnu.verification.report/1"). When present it participates in the
    /// claim-evidence gate: a delivery may only claim high confidence when
    /// the loop's own verdict passed AND the unified report is not present
    /// with a non-pass overall (its "indeterminate" can never read as
    /// success). Consumed as a JSON contract; no verifier link.
    Json::Value verificationReport{Json::objectValue};
};

class DeliveryAssembler {
  public:
    FinalDelivery assemble(const sicnu::agent_loop::SessionResult &result,
                           const DeliveryExtras &extras = {}) const;

    /// Capsule-oriented export document (portable refs only; no absolute paths).
    Json::Value capsuleExportDocument(const FinalDelivery &delivery) const;
};

} // namespace sicnu::agent_ops
