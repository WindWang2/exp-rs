// grader_adapters.h — pure JSON→JSON document-shape adapters (ADR 0174 §8:
// "the module ships pure JSON→JSON adapters for the document shapes only").
//
// These adapters PROJECT recorded documents into `sicnu.grader.evidence/1`
// items. They are the leaf-side half of the collector story: production
// callers (Qt side) wire stores/projection services; everything here is
// jsoncpp-only and shape-driven. Three hard rules:
//
//   1. PROJECTION, NEVER COMPUTATION. Recorded members are copied verbatim;
//      the adapter never derives a number, a digest or a state that the
//      document does not carry. Absent evidence stays absent (an empty
//      metrics block in a projector summary projects NOTHING, never a zero).
//   2. REAL SHAPES, TYPED REFUSALS. Envelopes are gated against the
//      producer's own versioning (d17_provenance/1.0, WorkflowRun
//      serialization versions, metrics_schema_version, evidence
//      schema_version). A foreign version or a hostile member type is a
//      typed grader:e-* refusal, never a best-effort parse.
//   3. DETERMINISTIC IDENTITY. evidenceIds are derived from the recorded
//      run identity + recorded node/step/path so two runs of the same
//      workflow merge into one bundle without collisions, and re-projecting
//      the same document is byte-stable.
//
// Projected item inventory:
//   provenance (d17_provenance/1.0)
//     run node       → custom item, key = run node id
//     nodeExec node  → stage item, key = attributes.nodeId (state preserved)
//     artifact node  → artifact_state item, key = attributes.path, state
//                      "present" (the node's existence IS the record),
//                      facts carry fingerprint/size + producedBy/consumedBy/
//                      reusedBy edge facts (only edges with resolvable
//                      endpoints)
//   checkpoint (WorkflowRun serialization v1/v2)
//     envelope       → custom item, key = runId, state = envelope state
//     stepPlans[]    → stage items, key = stepId, state = status
//   metric record (metrics_schema_version 1; run_id required)
//     numeric leaves of `metrics` → metric items keyed by dotted path,
//     source = "metric_record:<run_id>:<metrics_hash>" (run-namespaced:
//     metrics_hash is a content hash, identity comes from the run)
//   EvidenceProjector summary (schema_version 1)
//     status/identity/environment/steps/completeness → custom items,
//     artifacts[] → artifact_state items, metrics.document numeric leaves →
//     metric items
#pragma once

#include "grader_types.h"

#include <json/json.h>

#include <string>
#include <vector>

namespace sicnu::grader {

/// Projects a d17_provenance/1.0 document (ProvenanceGraph::toJson shape).
std::vector<GradeEvidenceItem> provenanceToEvidence( const Json::Value &doc, GraderError &error );

/// Projects a WorkflowRun checkpoint document (WorkflowRun::toJson shape,
/// serialization versions 1 and 2).
std::vector<GradeEvidenceItem> checkpointToEvidence( const Json::Value &doc, GraderError &error );

/// Projects a MetricRecord document (run_id/protocol/metrics/metrics_hash/
/// metrics_schema_version shape).
std::vector<GradeEvidenceItem> metricRecordToEvidence( const Json::Value &doc, GraderError &error );

/// Projects an EvidenceProjector summary document (evidence.h v1 layout).
std::vector<GradeEvidenceItem> projectorSummaryToEvidence( const Json::Value &doc, GraderError &error );

} // namespace sicnu::grader
