# ADR 0031: Integrate PlaceholderGrammar into WorkflowSession

- **Status**: Accepted
- **Date**: 2026-08-02
- **Deciders**: exp-rs Core Architecture Team

## Context

`PlaceholderGrammar` (`parsePlaceholders`, `substitutePlaceholders`, `inferStepConnections`) existed as a pure parsing helper module. Parameter values recorded in `WorkflowSession` often contained upstream artifact references such as `$step1.output` or `${step1.portName}`.

Previously, `WorkflowSession::paramsFor(stepId)` returned raw recorded JSON values, forcing external callers (`WorkflowRuntime`, controllers, task center adapters) to manually call `substitutePlaceholders` and supply custom artifact lookup closures.

## Decision

1. **Add `resolveParams(stepId)` to `WorkflowSession`**: Expose `Json::Value resolveParams( const std::string &stepId ) const` on `WorkflowSession`. `paramsFor(stepId)` continues to return raw recorded parameters (for UI form editing), while `resolveParams(stepId)` returns parameter JSON with all placeholders dynamically substituted.
2. **Artifact Lookup Order**: `resolveParams` resolves placeholders against `m_artifacts` using:
   - Target step's `artifactOnSuccess` (e.g. `stepById(ref.stepId)->artifactOnSuccess`).
   - Explicit `ref.stepId + "." + ref.portName` or `ref.portName`.
   - If no artifact match is found, the raw placeholder text remains unchanged.
3. **`WorkflowRuntime` Deepening**: `WorkflowRuntime::runStep` calls `s->resolveParams(stepId)` to pass fully resolved parameter maps directly to operators.

## Consequences

- **Locality**: Parameter placeholder evaluation occurs directly within `WorkflowSession` using session artifact state.
- **Leverage**: Callers pass parameters to operators without duplicating placeholder substitution loops.
- **Testability**: `WorkflowSession::resolveParams` is fully testable headlessly in `test_workflow_runtime.cpp`.
