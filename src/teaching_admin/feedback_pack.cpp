#include "feedback_pack.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

namespace sicnu::teaching_admin {

QJsonObject buildFeedbackPack( const FeedbackPackInput &in )
{
    return QJsonObject{
        { QStringLiteral( "schema" ), QString::fromLatin1( kFeedbackPackSchema ) },
        { QStringLiteral( "student_id" ), in.row.studentId },
        { QStringLiteral( "lab_id" ), in.row.labId },
        { QStringLiteral( "overall" ),
          QJsonObject{
              { QStringLiteral( "score" ), in.row.score },
              { QStringLiteral( "verdict" ), in.row.verdict },
              { QStringLiteral( "status" ), in.row.status },
              { QStringLiteral( "message" ), in.row.message },
              { QStringLiteral( "missing_evidence" ), in.row.missingEvidence },
          } },
        { QStringLiteral( "dimensions" ), in.dimensionScores },
        { QStringLiteral( "verifier_findings" ), in.verifierFindings },
        { QStringLiteral( "missing_evidence" ), in.missingEvidence },
        { QStringLiteral( "first_divergence" ), in.firstDivergence },
        { QStringLiteral( "reproducibility" ), in.reproducibility },
        { QStringLiteral( "teacher_comment_placeholder" ),
          in.teacherCommentPlaceholder.isEmpty()
              ? QStringLiteral( "[teacher comment]" )
              : in.teacherCommentPlaceholder },
        { QStringLiteral( "versions" ),
          QJsonObject{
              { QStringLiteral( "rubric" ), in.row.rubricVersion },
              { QStringLiteral( "lab" ), in.row.labVersion },
              { QStringLiteral( "software" ), in.row.softwareVersion },
          } },
    };
}

ValidationResult assertNoCrossStudentLeak( const QJsonObject &feedback, const QString &studentId )
{
    ValidationResult r;
    const QString sid = feedback.value( QStringLiteral( "student_id" ) ).toString();
    if ( sid != studentId )
        r.addError( QStringLiteral( "cross_student_leak" ), QStringLiteral( "student_id" ),
                    QStringLiteral( "feedback student_id mismatch" ) );

    const QString blob = QString::fromUtf8( QJsonDocument( feedback ).toJson( QJsonDocument::Compact ) );
    if ( blob.contains( QLatin1String( "student_" ) ) && !studentId.isEmpty() )
    {
        QRegularExpression re( QStringLiteral( "student_[A-Za-z0-9_\\-]+" ) );
        auto it = re.globalMatch( blob );
        while ( it.hasNext() )
        {
            const QString m = it.next().captured();
            if ( m == QLatin1String( "student_id" ) || m == studentId )
                continue;
            {
                r.addError( QStringLiteral( "cross_student_leak" ), QStringLiteral( "body" ),
                            QStringLiteral( "foreign student token: " ) + m );
                break;
            }
        }
    }
    return r;
}

} // namespace sicnu::teaching_admin
