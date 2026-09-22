// admin_types.h — shared value types for Teacher Authoring & Assessment Console.
// Projection / orchestration only: no second curriculum, LabSpec, grader, or pack truth.
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace sicnu::teaching_admin {

inline constexpr const char *kReleaseReportSchema = "sicnu.teaching_release_report/1";
inline constexpr const char *kFeedbackPackSchema = "sicnu.teaching.feedback_pack/1";
inline constexpr const char *kClassSummarySchema = "sicnu.teaching.class_summary/1";
inline constexpr const char *kBatchOrchestrationSchema = "sicnu.teaching.batch_orchestration/1";

struct AdminIssue
{
    QString code;     ///< closed vocabulary slug, e.g. "cyclic_prerequisites"
    QString path;     ///< JSON-pointer-ish location
    QString message;  ///< human-readable (zh or en)
    QString severity; ///< "error" | "warning"

    QJsonObject toJson() const
    {
        return QJsonObject{
            { QStringLiteral( "code" ), code },
            { QStringLiteral( "path" ), path },
            { QStringLiteral( "message" ), message },
            { QStringLiteral( "severity" ), severity.isEmpty() ? QStringLiteral( "error" ) : severity },
        };
    }
};

inline QJsonArray issuesToJson( const QVector<AdminIssue> &issues )
{
    QJsonArray arr;
    for ( const auto &i : issues )
        arr.append( i.toJson() );
    return arr;
}

struct ValidationResult
{
    bool ok = true;
    QVector<AdminIssue> issues;

    void addError( const QString &code, const QString &path, const QString &message )
    {
        ok = false;
        issues.push_back( { code, path, message, QStringLiteral( "error" ) } );
    }

    void addWarning( const QString &code, const QString &path, const QString &message )
    {
        issues.push_back( { code, path, message, QStringLiteral( "warning" ) } );
    }

    QJsonObject toJson() const
    {
        return QJsonObject{
            { QStringLiteral( "ok" ), ok },
            { QStringLiteral( "issues" ), issuesToJson( issues ) },
        };
    }
};

} // namespace sicnu::teaching_admin
