#include "feedback_pack.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include <algorithm>

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
    return assertNoCrossStudentLeak( feedback, studentId, QStringList{} );
}

ValidationResult assertNoCrossStudentLeak( const QJsonObject &feedback, const QString &studentId,
                                           const QStringList &cohortStudentIds )
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
            // The match carries the "student_" prefix (e.g. "student_alice"),
            // so the owner's own token must be compared against the prefixed
            // spelling as well; otherwise a pack that legitimately mentions
            // its own student trips a false cross_student_leak. The bare form
            // stays for ids that already begin with "student_".
            if ( m == QLatin1String( "student_id" ) || m == studentId
                 || m == QStringLiteral( "student_" ) + studentId )
                continue;
            {
                r.addError( QStringLiteral( "cross_student_leak" ), QStringLiteral( "body" ),
                            QStringLiteral( "foreign student token: " ) + m );
                break;
            }
        }
    }

    // #1689: roster-defined foreign ids. Real cohorts carry plain numeric
    // student numbers and Chinese names — invisible to the prefix scan
    // above. Every other id of the batch is matched with word boundaries so
    // an id embedded in a longer token (2023002 inside 20230021) does not
    // false-positive; the owner's own id is excluded by construction.
    QStringList foreignPatterns;
    for ( const QString &id : cohortStudentIds )
    {
        if ( id.isEmpty() || id == studentId )
            continue;
        foreignPatterns << QRegularExpression::escape( id );
    }
    if ( !foreignPatterns.isEmpty() )
    {
        foreignPatterns.removeDuplicates();
        std::sort( foreignPatterns.begin(), foreignPatterns.end() ); // deterministic first hit
        const QRegularExpression re( QStringLiteral( "(?<![A-Za-z0-9_\\-])(?:" )
                                       + foreignPatterns.join( QLatin1Char( '|' ) )
                                       + QStringLiteral( ")(?![A-Za-z0-9_\\-])" ) );
        const auto m = re.match( blob );
        if ( m.hasMatch() )
            r.addError( QStringLiteral( "cross_student_leak" ), QStringLiteral( "body" ),
                        QStringLiteral( "foreign student id: " ) + m.captured() );
    }
    return r;
}

FeedbackPackEmission buildGuardedFeedbackPacks( const QVector<FeedbackPackInput> &inputs,
                                                const QStringList &cohortStudentIds )
{
    FeedbackPackEmission emission;
    for ( const FeedbackPackInput &in : inputs )
    {
        const QJsonObject pack = buildFeedbackPack( in );
        const ValidationResult guard =
          assertNoCrossStudentLeak( pack, in.row.studentId, cohortStudentIds );
        if ( guard.ok )
        {
            emission.packs.append( pack );
            continue;
        }
        // A suspected leak is WITHHELD and accounted — never emitted on the
        // "best effort" theory that the guard might be wrong. The refusal
        // names the student and the first issue so the teacher can inspect
        // the source row.
        emission.blockedStudents.append( in.row.studentId );
        QString why;
        if ( !guard.issues.isEmpty() )
            why = guard.issues.first().message;
        emission.refusals.append( in.row.studentId + QLatin1String( ": " ) + why );
    }
    emission.blockedStudents.removeDuplicates();
    return emission;
}

} // namespace sicnu::teaching_admin
