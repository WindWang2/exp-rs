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

namespace
{

/// R1 review: the boundary classes must be UNICODE letter/digit aware. With
/// ASCII-only classes, cohort {张三, 张三丰} made 张三's pattern match inside
/// 张三丰's own student_id — the guard withheld a legitimate pack on the
/// production path. \p{L}/\p{N} keep 数字 adjacency protection (2023002
/// inside 20230021) AND extend it to CJK and every other script.
QRegularExpression compileForeignIdRegex( const QStringList &cohortStudentIds )
{
    QStringList patterns;
    for ( const QString &id : cohortStudentIds )
    {
        if ( id.isEmpty() )
            continue;
        patterns << QRegularExpression::escape( id );
    }
    patterns.removeDuplicates();
    std::sort( patterns.begin(), patterns.end() ); // deterministic first hit
    if ( patterns.isEmpty() )
        return QRegularExpression();
    return QRegularExpression( QStringLiteral( "(?<![\\p{L}\\p{N}_\\-])(?:" )
                                 + patterns.join( QLatin1Char( '|' ) )
                                 + QStringLiteral( ")(?![\\p{L}\\p{N}_\\-])" ) );
}

/// Scan with an ALL-cohort-ids regex (compiled once per batch) and skip the
/// owner's own id at match time: one compiled pattern serves every pack —
/// per-pack recompilation on the GUI thread was the R1 P3-8 finding.
void addForeignIdErrorIfAny( ValidationResult &r, const QString &blob, const QString &studentId,
                             const QRegularExpression &allIdsRegex )
{
    // R2 finding 1: a default-constructed QRegularExpression is VALID with
    // an EMPTY pattern — isValid() alone does not mean "nothing to scan",
    // and an empty pattern matches the empty string at offset 0, which
    // fabricated a foreign-id error for every roster-less check. Gate on
    // the pattern being non-empty as well.
    if ( allIdsRegex.pattern().isEmpty() || !allIdsRegex.isValid() )
        return;
    auto it = allIdsRegex.globalMatch( blob );
    while ( it.hasNext() )
    {
        const QString m = it.next().captured();
        if ( m == studentId )
            continue; // the owner's own id is legitimate in its own pack
        r.addError( QStringLiteral( "cross_student_leak" ), QStringLiteral( "body" ),
                    QStringLiteral( "foreign student id: " ) + m );
        return;
    }
}

} // namespace

ValidationResult assertNoCrossStudentLeak( const QJsonObject &feedback, const QString &studentId )
{
    return assertNoCrossStudentLeak( feedback, studentId, QStringList{} );
}

/// All three leak checks with a PRE-COMPILED all-cohort-ids regex (may be
/// invalid = no ids to scan): owner match, legacy student_ prefix scan,
/// roster-defined foreign ids with Unicode word boundaries.
static ValidationResult leakGuardCore( const QJsonObject &feedback, const QString &studentId,
                                       const QRegularExpression &allIdsRegex )
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
            r.addError( QStringLiteral( "cross_student_leak" ), QStringLiteral( "body" ),
                        QStringLiteral( "foreign student token: " ) + m );
            break;
        }
    }

    // #1689: roster-defined foreign ids. Real cohorts carry plain numeric
    // student numbers and Chinese names — invisible to the prefix scan
    // above. Unicode word boundaries keep an id embedded in a longer token
    // (2023002 inside 20230021, 张三 inside 张三丰) from false-positiving.
    // Identically-named students cannot be distinguished texturally at all —
    // a documented limitation, not a guard promise.
    addForeignIdErrorIfAny( r, blob, studentId, allIdsRegex );
    return r;
}

ValidationResult assertNoCrossStudentLeak( const QJsonObject &feedback, const QString &studentId,
                                           const QStringList &cohortStudentIds )
{
    return leakGuardCore( feedback, studentId, compileForeignIdRegex( cohortStudentIds ) );
}

FeedbackPackEmission buildGuardedFeedbackPacks( const QVector<FeedbackPackInput> &inputs,
                                                const QStringList &cohortStudentIds )
{
    // R1 P3-8: compile the cohort regex ONCE for the whole batch — the
    // per-pack public overload would recompile an O(cohort) alternation for
    // every student on the GUI thread. The guard itself (all three checks)
    // is the shared leakGuardCore, so the production path and the public
    // API cannot drift.
    const QRegularExpression allIdsRegex = compileForeignIdRegex( cohortStudentIds );
    FeedbackPackEmission emission;
    for ( const FeedbackPackInput &in : inputs )
    {
        const QJsonObject pack = buildFeedbackPack( in );
        const ValidationResult guard = leakGuardCore( pack, in.row.studentId, allIdsRegex );
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
