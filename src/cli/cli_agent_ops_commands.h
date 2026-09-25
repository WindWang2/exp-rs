/***************************************************************************
 * src/cli/cli_agent_ops_commands.h — `session` command: agent ops driver
 *
 *   sicnu_geo_rs_cli session <action> [flags]
 *
 * Thin CLI envelope over the shared OpsDriver (src/agent_ops/ops_driver.h):
 * the SAME typed documents the MCP `scientific:agent_session` tool and the
 * workbench panel consume — one wire contract, three entries. Actions:
 *
 *   run          --goal <text> [--intent <i>] [--mode <m>] [--journal-dir <d>]
 *                [--session-id <id>] [--domain <d>] [--role <r>]
 *                [--refs <json>] [--approve-pending-repair]
 *   resume       --journal-dir <d> --session-id <id> --goal <text> [flags]
 *   reconcile    --journal-dir <d> --session-id <id>
 *   status | timeline | export   [--session-id <id>]
 *   pause | cancel | clear-pause | clear-cancel | actions
 *   approve-repair [--approve true|false]
 *
 * The command never invents session state: everything below the envelope is
 * OperationsCoordinator + agent_loop. The CLI host currently injects no
 * production science seams, so run/resume fail closed with the typed
 * SEAMS_UNAVAILABLE code until a host wires a seam bundle — discovery,
 * status, export and journal reconciliation are fully live.
 ***************************************************************************/
#pragma once

#include <QStringList>

namespace sicnu::agent_ops {
class OpsDriver;
}

namespace sicnu::cli {

struct CliIO;

/// Dispatches `session <action> ...` over @p driver (a null driver yields a
/// typed AGENT_OPS_UNAVAILABLE envelope). Returns the process exit code.
int commandAgentSession( QStringList arguments, const CliIO &io,
                         sicnu::agent_ops::OpsDriver *driver );

/// The CLI host's default driver (no production science seams injected —
/// run/resume fail closed; discovery/reconcile/status live).
sicnu::agent_ops::OpsDriver *defaultCliAgentOpsDriver();

} // namespace sicnu::cli
