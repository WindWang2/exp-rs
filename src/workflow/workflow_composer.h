// src/workflow/workflow_composer.h — subflow fragment expansion (D17, flash-workflow-engine-12)
#pragma once

//
// Composition: a node whose operatorId is "workflow:subflow" is a fragment
// instance — an embedded WorkflowDocument inlined into the parent at run
// time. Expansion is a pure, deterministic function:
//
//   parameters.fragment   embedded workflow document (object). File/path
//                         references are refused (fail closed) — remote or
//                         disk lookup would make the expanded graph depend
//                         on non-document state.
//   parameters.interface  { "inputs":  { "<instancePort>": {"node","port"} },
//                           "outputs": { "<instancePort>": {"node","port"} } }
//                         maps the instance's declared ports onto fragment
//                         internals. Every edge that touches the instance
//                         must resolve through this map.
//   parameters.bindings   { "<fragNodeId>": { "<param>": value, ... } }
//                         template substitution merged into fragment node
//                         parameters before inlining.
//
// Expansion rules (all fail-closed, errors name the instance nodeId):
//   - expanded ids are "<instanceNodeId>__<fragNodeId>" — a pure function of
//     the authored ids, so lineage signatures stay stable across runs. The
//     separator is "__", not "/": the registry executor's isSafeNodeId
//     (#1032) rejects path separators in node ids.
//   - every inlined node gets originNodeId = instance nodeId (schema 2.1
//     hook) so failures/provenance attribute to the designer-level node.
//   - parent edges are rewired through the interface map; an instance port
//     with an edge but no mapping is an error.
//   - nested subflows expand recursively up to kMaxSubflowDepth.
//   - expanded id collision with an existing node id is an error.
//
// The output document claims the authored version, bumped to "2.1" when
// expansion produced originNodeId annotations on a 2.0 document.
//

#include "workflow/workflow_ir_v2.h"

namespace sicnu::workflow {

class WorkflowComposer
{
  public:
    WorkflowComposer() = delete;

    static constexpr int kMaxSubflowDepth = 8;
    static constexpr const char *kSubflowOperatorId = "workflow:subflow";

    /// True when the document contains at least one fragment instance.
    static bool hasSubflowNodes( const WorkflowDocument &def );

    /// Identity when no subflow nodes are present. Otherwise returns the
    /// flattened document or an error prefixed "ir2.subflow_*:" naming the
    /// offending instance node.
    static Result<WorkflowDocument> expandSubflows( const WorkflowDocument &authored );
};

} // namespace sicnu::workflow
