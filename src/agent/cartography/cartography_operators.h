/***************************************************************************
 * cartography_operators.h — Workbench 10.0 cartography workflow bridge
 *
 * Thin RSOperator adapters over the SAME cartography engine the agent tools
 * use (composition solver, preflight, MapSpecCompiler, governed export).
 * They close ISSUES.md C-1: `cartography:*` steps become first-class
 * workflow/pipeline nodes — one registry, every surface (GUI workflow
 * editor, headless CLI pipeline, TaskCenter, MCP) dispatches identically.
 *
 * The adapters add no engine behavior: validation, solve, compile, repair
 * and export semantics are exactly the engine's. No-QGIS hosts (worker
 * processes) get honest NotInitialized refusals for the layout-bound
 * operators; preflight/validate/repair are Qt-free and run anywhere.
 *
 * Registration: initCartographyOperators() is idempotent and safe to call
 * after RSOperatorRegistry::instance() has completed its init chain (it
 * never runs inside that chain). Hosts call it at startup; the workflow
 * editor and CLI pipeline need nothing else.
 ***************************************************************************/
#pragma once

namespace sicnu::agent::cartography
{

/// Registers cartography:compose / preflight / validate / repair / export
/// with the RSOperatorRegistry. Idempotent: a second call is a no-op.
void initCartographyOperators();

} // namespace sicnu::agent::cartography
