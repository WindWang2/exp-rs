// script_adapters.h — structured subprocess adapters (never free-text log scrape).
#pragma once

#include "admin_types.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace sicnu::teaching_admin {

struct ScriptRunRequest
{
    QString program;          ///< python3 / bash / absolute tool
    QStringList arguments;
    QString workingDirectory;
    int timeoutMs = 120000;
};

struct ScriptRunResult
{
    bool started = false;
    int exitCode = -1;
    bool timedOut = false;
    QByteArray stdoutBytes;
    QByteArray stderrBytes;
    QString error; ///< typed when process could not start

    QJsonObject toJson() const;
};

/// Run a controlled subprocess. Captures stdout/stderr; does not scrape logs
/// for semantics — callers parse known JSON/exit contracts.
ScriptRunResult runScript( const ScriptRunRequest &req );

/// Adapter: gen_lab_packs.py → structured result.
ScriptRunResult runGenLabPacks( const QString &repoRoot, const QString &pythonExe = QStringLiteral( "python3" ) );

/// Adapter: verify_bundle_manifest.py <bundleDir>.
struct BundleVerifyResult
{
    bool verifiable = false; ///< false when exit 2 / missing script
    bool ok = false;
    int exitCode = -1;
    QStringList findings;
    QString summary;
    QJsonObject toJson() const;
};

BundleVerifyResult verifyOfflineBundle( const QString &repoRoot, const QString &bundleDir,
                                        const QString &pythonExe = QStringLiteral( "python3" ) );

/// Adapter: build_offline_bundle.sh (POSIX).
ScriptRunResult buildOfflineBundle( const QString &repoRoot, const QString &buildDir, const QString &outDir,
                                    const QString &version = QString(), int maxMb = 250 );

/// Adapter: run_classroom_batch.py
ScriptRunResult runClassroomBatch( const QString &repoRoot, const QString &cliPath, const QString &labId,
                                   const QString &submissionsDir, const QString &outPrefix, int jobs = 2,
                                   int timeoutSec = 120, const QString &pythonExe = QStringLiteral( "python3" ) );

} // namespace sicnu::teaching_admin
