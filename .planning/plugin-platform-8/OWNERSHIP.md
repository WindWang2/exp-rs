# Plugin Platform 8.0 — Ownership Map

Authoritative owners this track must extend (never fork):

| Domain | Owner | Notes |
|---|---|---|
| Plugin lifecycle state | `exprs::PluginRegistry` (src/sdk/exprs/plugin_registry.*) | single owner; sink seam for contributions |
| Contribution registration | `exprs::PluginContributionSink` implemented by `PluginRuntimeHost` (src/plugins/framework) | proxies register into ordinary app registries |
| Operator surface | `RSOperatorRegistry` / `AtomicAlgorithmRegistry` | plugin operators land here as adapters/proxies |
| Model execution | `IModelRuntime` / `runModelInference` seams; `ModelRuntimeRegistry` | plugin model runtimes register here |
| Agent tools | `AgentToolCatalog` via `PluginAgentToolProvider` | |
| Data providers | `DataProviderRegistry` (src/plugins/framework/data_provider_registry.*) | |
| Map/UI | QGIS canvas + host-owned widgets; `PluginUiHost` + `UiShellSink` | host renders; plugins contribute |
| Command/UX authority | `CommandRegistry`, `ContextRules`, `SelectionContext` | declarative UI contributions must route through these |
| IPC wire contract | `exprs::Ipc*` (src/sdk/exprs/ipc_*.h) + `host_protocol.h` | v1.0 → minor bump for 8.0 additions |
| Host-process sessions | `sicnu::plugins::PluginHostProcessSession/Runtime` (src/plugins/host) | worker per plugin; proxies |
| Quotas/capabilities | `exprs::PluginQuota`, `exprs::PluginCapabilities` | clamped host authority |
| Packaging | `exprs::PluginPackage` | extend, don't fork |
| Diagnostics | `exprs::PluginDiagnostic*` (E1xxx–E6xxx) | stable codes; extend E6xxx family additively |

Files this track expects to touch (owned): `src/sdk/exprs/**` (additive), `src/plugins/host/**`,
`src/plugins/framework/**` (UI schema host side), `src/cli/cli_commands.cpp` (conformance kit),
`tests/**` (new suites/fixtures), `docs/plugins/**`, `docs/sdk/versioning.md`.

Shared files to keep minimal (other 8.0 tracks may touch): `tests/CMakeLists.txt` (append-only),
`src/cli/cli_commands.cpp` (scoped to `commandPlugin`), `src/plugins/CMakeLists.txt` trees.
