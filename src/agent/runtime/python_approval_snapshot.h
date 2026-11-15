#pragma once

#include "agent/sicnu_agent_export.h"

#include <QJsonObject>
#include <QString>

#include <functional>

namespace sicnu::agent::runtime
{

struct SICNU_AGENT_EXPORT PythonApprovalSnapshot
{
  QString canonicalToolId;
  /// Execution arguments: script_path becomes the approved inline source.
  QJsonObject args;
  /// Bounded identity fields only; never the raw environment probe response.
  QJsonObject identity;
  /// Empty on success, otherwise a fixed code suitable for the UI.
  QString error;
};

/// Blocking, read-only work. Call from a worker thread, never the GUI thread.
/// Uses the managed environment's existing bounded probe; never installs.
SICNU_AGENT_EXPORT PythonApprovalSnapshot capturePythonApprovalSnapshot(
  const QString &canonicalToolId, const QJsonObject &arguments,
  const std::function<bool()> &cancelled = {} );

/// Re-reads an original script file and probes the current exact environment.
/// Returns an empty string only when every captured identity still matches.
/// Blocking, read-only work; call from a worker thread.
SICNU_AGENT_EXPORT QString recheckPythonApprovalSnapshot(
  const PythonApprovalSnapshot &snapshot,
  const std::function<bool()> &cancelled = {} );

} // namespace sicnu::agent::runtime
