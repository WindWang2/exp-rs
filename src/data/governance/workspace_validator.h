// workspace_validator.h — asset/project/workspace integrity validation
// (Platform 3.0 Phase F). Machine-readable diagnostics + repair suggestions.
#pragma once

#include "data/governance/governance_types.h"

#include <QString>
#include <QVector>

namespace sicnu::data
{
class DataManager;
}

namespace sicnu::workspace
{

class WorkspaceService;

struct ValidationOptions
{
    /// Recompute content digests (expensive; default compares size/mtime only).
    bool verifyFingerprints = false;
    /// Probe remote URIs (network); default off — validation stays offline-safe.
    bool probeRemote = false;
    /// Upper bound on emitted diagnostics (bounded outputs everywhere).
    qint64 maxDiagnostics = 1000;
};

struct ValidationReport
{
    QVector<GovernanceDiagnostic> diagnostics;
    ValidationSummary summary;
    bool ok() const { return summary.errors == 0; }
    QJsonObject toJson() const;
};

class WorkspaceValidator
{
  public:
    WorkspaceValidator( sicnu::data::DataManager &dataManager, WorkspaceService &service );

    ValidationReport validateProject( const ValidationOptions &options = ValidationOptions() ) const;
    ValidationReport validateAsset( const QString &assetId,
                                    const ValidationOptions &options = ValidationOptions() ) const;

  private:
    void checkAsset( const QString &assetId, const ValidationOptions &options,
                     ValidationReport &report ) const;

    sicnu::data::DataManager &m_dataManager;
    WorkspaceService &m_service;
};

} // namespace sicnu::workspace
