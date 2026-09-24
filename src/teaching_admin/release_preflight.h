// release_preflight.h — Teacher dry-run / preflight → versioned teaching_release_report.
#pragma once

#include "admin_types.h"

#include <QJsonObject>
#include <QSet>
#include <QStringList>
#include <QString>
#include <QVector>

namespace sicnu::teaching_admin {

struct PreflightInput
{
    QJsonObject curriculum;
    QJsonObject labSpec;           ///< optional single lab under test
    QJsonObject pipeline;          ///< optional recipe (read-only)
    QSet<QString> knownLabIds;
    QSet<QString> knownOperators;
    QJsonObject operatorParamSchemas; ///< operator id → {"properties": {…}}
    QJsonObject labRules;          ///< optional
    QJsonObject processRubric;     ///< optional
    QJsonObject packDocument;      ///< optional pack under test
    QString repoRoot;
    QString softwareVersion;       ///< platform profile string
    QStringList packIdsChecked;
};

struct TeachingReleaseReport
{
    QString schema = QString::fromLatin1( kReleaseReportSchema );
    bool ok = false;
    QString softwareVersion;
    QString curriculumDigest;
    QString labSpecDigest;
    QString rulesDigest;
    QString packDigest;
    QVector<AdminIssue> issues;
    QJsonObject sections; ///< named section validation results

    QJsonObject toJson() const;
    QByteArray canonicalBytes() const;
    QString canonicalDigest() const;
};

TeachingReleaseReport runPreflight( const PreflightInput &in );

} // namespace sicnu::teaching_admin
