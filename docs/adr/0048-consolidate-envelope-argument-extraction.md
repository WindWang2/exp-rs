# ADR 0048: Consolidate Tool-Call Envelope Argument Extraction

## Status
Accepted

## Context
`AgentCopilotDockWidget::planArgumentsFor` re-implemented in QJson-land the
envelope-shape knowledge that `ToolCallDispatcher::parseEnvelope` already
owned (historical envelope shapes, `function.arguments` as object or JSON
string). Two owners of the same parsing knowledge could drift apart, and the
same ~5-line QJson→`Json::Value` conversion appeared at multiple call sites
(agent dock, plan-preview handler, tests) with no shared helper.

## Decision
1. **Expose `ToolCallDispatcher::argumentsFor(envelope)`** as a public
   static that delegates to the existing `parseEnvelope` — one owner of
   envelope shape for `classify`, `submit`, `rejectionReason`, and the plan
   path.

2. **Add `jsonValueFromQJson(const QJsonValue&)`** to
   `json_params_converter.h` (header-only, recursive, lossless) and migrate
   the QJson→`Json::Value` call sites (agent dock, plan-preview handler,
   canvas-sync test) to it.

3. **Delete `planArgumentsFor`**: the plan approval card now receives the
   dispatcher-extracted arguments; the dock's run-button and completion
   paths read `Json::Value` directly instead of round-tripping through
   `QJsonDocument`.

## Consequences
- **Envelope shape has a single owner**: plan detection and plan display
  can no longer disagree about where arguments live.
- **One conversion helper**: QJson→`Json::Value` conversions at call sites
  can no longer drift.
- **Behavior preserved**: `classify`/`submit`/`rejectionReason` unchanged;
  plain string/file JSON parses (task center, job panel, runners) are a
  different conversion and stay as-is.
