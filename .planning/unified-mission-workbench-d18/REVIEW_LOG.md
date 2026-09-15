# REVIEW_LOG — D18 IR2 port→param mapping (D-W6)

Independent review of multi-input port→param mapping on the registry NodeExecutor path.

## Architecture
- Extends D-W5 registry bind with pure `applyIr2InputPortMapping` — OK (no third scheduler).
- Coordinator keys artifacts by `targetPortName` — OK (matches IR2 single-source invariant).
- Prefer explicit port names; legacy node-id keys order-only zip — OK (documented limitation).
- Unbound refusal before mapping — OK (mapping cannot invent success).

## Lifecycle
- Mapping is pure / deterministic; no new threads — OK.
- Explicit `node.parameters` never overwritten — OK.

## Workflow / Agent
- LabSpec lift still uses port `"input"` — compatible.
- Operators expecting `reference` / `dem` / `inputA` bind when IR2 ports share those names — OK.

## Tests
- `test_ir2_port_param_mapping` covers multi-input, explicit-params win, primary alias, legacy zip, unbound refuse helper.
- **Not executed** on agent box (no cmake/g++). Honest EVIDENCE.md.

## Findings disposition
| ID | Sev | Finding | Disposition |
|----|-----|---------|-------------|
| R2 | P2 | Synthetic executor on dock Run | **Cleared** (D-W5) |
| R7 | P2 | Unbound nodes previously succeeded synthetically | **Cleared** (D-W5) |
| R8 | P2 | Multi-input only primary `input` | **Cleared** (D-W6 port→param) |
| R1 | P2 | Dual WorkflowDefinition name | Alias call sites advanced; struct rename deferred |
| R4 | P2 | Builds not run on this box | Honest EVIDENCE.md |
| R9 | P3 | No schema-driven port rename | Documented limitation / follow-up |

No new P0.
