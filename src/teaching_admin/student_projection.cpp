#include "student_projection.h"
#include "json_util.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include <algorithm>

namespace sicnu::teaching_admin {

namespace {

constexpr const char *kMaskedValue = "***";

QJsonValue maskAnswers( const QJsonValue &v )
{
    if ( v.isObject() )
    {
        const QJsonObject obj = v.toObject(); // materialize: iterators must not
        QJsonObject out;                      // span temporaries
        for ( auto it = obj.begin(); it != obj.end(); ++it )
            out.insert( it.key(), maskAnswers( it.value() ) );
        return out;
    }
    if ( v.isArray() )
    {
        const QJsonArray arr = v.toArray();
        QJsonArray out;
        for ( const auto &item : arr )
            out.append( maskAnswers( item ) );
        return out;
    }
    return QString::fromLatin1( kMaskedValue );
}

QJsonArray collectTeacherOnlyStrings( const QJsonObject &teacherSpec )
{
    QJsonArray out;
    for ( const auto &v : teacherSpec.value( QStringLiteral( "expected_results" ) ).toArray() )
    {
        const QJsonObject er = v.toObject();
        const QString claim = er.value( QStringLiteral( "claim" ) ).toString();
        if ( !claim.isEmpty() )
            out.append( claim );
    }
    return out;
}

} // namespace

QJsonObject projectStudentLabView( const QJsonObject &labSpec )
{
    QJsonArray stepsOut;
    for ( const auto &sv : labSpec.value( QStringLiteral( "steps" ) ).toArray() )
    {
        const QJsonObject step = sv.toObject();
        QJsonObject stepOut;
        for ( const char *keep : { "id", "title", "title_zh", "operator_id", "operator",
                                   "human_only", "reflection", "kind", "kind_zh", "description" } )
        {
            const QString key = QString::fromLatin1( keep );
            if ( step.contains( key ) )
                stepOut.insert( key, step.value( key ) );
        }
        // Param NAMES stay (students may type them), VALUES are the solution.
        const QJsonObject params = step.value( QStringLiteral( "params" ) ).toObject(
            step.value( QStringLiteral( "parameters" ) ).toObject() );
        if ( !params.isEmpty() )
            stepOut.insert( QStringLiteral( "params" ), maskAnswers( params ).toObject() );
        stepsOut.append( stepOut );
    }

    QJsonObject view{
        { QStringLiteral( "schema" ), QStringLiteral( "sicnu.teaching.student_lab_view/1" ) },
        { QStringLiteral( "lab_id" ), labSpec.value( QStringLiteral( "id" ) ) },
        { QStringLiteral( "read_only" ), true },
        { QStringLiteral( "note" ),
          QStringLiteral( "Student projection; answers and grading refs stay in teacher truth." ) },
        { QStringLiteral( "steps" ), stepsOut },
    };
    for ( const char *keep : { "version", "title", "title_zh", "theme", "audience",
                               "duration_minutes", "objectives", "principles", "glossary",
                               "prerequisites", "questions", "human_only", "reflection", "autonomy" } )
    {
        const QString key = QString::fromLatin1( keep );
        if ( labSpec.contains( key ) )
            view.insert( key, labSpec.value( key ) );
    }
    return sortKeys( view );
}

ValidationResult assertNoAnswerLeak( const QJsonObject &studentView, const QJsonObject &teacherSpec )
{
    ValidationResult r;
    const QString blob =
      QString::fromUtf8( QJsonDocument( studentView ).toJson( QJsonDocument::Compact ) );

    // Teacher-only keys must not exist anywhere in the view.
    for ( const char *forbidden : { "grading_ref", "expected_results" } )
    {
        if ( studentView.contains( QString::fromLatin1( forbidden ) ) )
            r.addError( QStringLiteral( "teacher_only_field" ), QString::fromLatin1( forbidden ),
                        QStringLiteral( "teacher-only field must not appear in the student view" ) );
    }

    // expected_results claims must not appear — even as verbatim excerpts
    // (paraphrase smuggling, e.g. a claim fragment pasted into a title).
    for ( const auto &claim : collectTeacherOnlyStrings( teacherSpec ) )
    {
        const QString s = claim.toString();
        if ( s.isEmpty() )
            continue;
        const QStringList tokens =
          s.split( QRegularExpression( QStringLiteral( "\\s+" ) ), Qt::SkipEmptyParts );
        const int window = static_cast<int>( std::min<qsizetype>( 4, tokens.size() ) );
        if ( window <= 0 )
            continue;
        bool leaked = false;
        for ( int start = 0; start + window <= tokens.size() && !leaked; ++start )
        {
            QString fragment;
            for ( int k = 0; k < window; ++k )
            {
                if ( k > 0 )
                    fragment += QLatin1Char( ' ' );
                fragment += tokens.at( start + k );
            }
            if ( blob.contains( fragment ) )
                leaked = true;
        }
        if ( leaked )
            r.addError( QStringLiteral( "answer_leak" ), QStringLiteral( "expected_results" ),
                        QStringLiteral( "expected-result excerpt leaked into the student view" ) );
    }

    // grading reference paths must not be quoted.
    const QString intentRef = teacherSpec.value( QStringLiteral( "grading_ref" ) )
                                .toObject()
                                .value( QStringLiteral( "intent_ref" ) )
                                .toString();
    if ( !intentRef.isEmpty() && blob.contains( intentRef ) )
        r.addError( QStringLiteral( "answer_leak" ), QStringLiteral( "grading_ref.intent_ref" ),
                    QStringLiteral( "grading reference path leaked into the student view" ) );

    // Step parameter values are the solution: at the same step/param position
    // the view must carry the mask (or nothing), never the original value;
    // string values additionally must not be quoted anywhere in the view.
    const QJsonArray teacherSteps = teacherSpec.value( QStringLiteral( "steps" ) ).toArray();
    const QJsonArray viewSteps = studentView.value( QStringLiteral( "steps" ) ).toArray();
    for ( int i = 0; i < teacherSteps.size(); ++i )
    {
        const QJsonObject step = teacherSteps.at( i ).toObject();
        const QJsonObject params = step.value( QStringLiteral( "params" ) ).toObject(
          step.value( QStringLiteral( "parameters" ) ).toObject() );
        const QJsonObject viewParams =
          i < viewSteps.size()
            ? viewSteps.at( i ).toObject()
                .value( QStringLiteral( "params" ) )
                .toObject( viewSteps.at( i )
                             .toObject()
                             .value( QStringLiteral( "parameters" ) )
                             .toObject() )
            : QJsonObject{};
        for ( auto it = params.begin(); it != params.end(); ++it )
        {
            const QString at =
              QStringLiteral( "steps[%1].params.%2" ).arg( i ).arg( it.key() );
            if ( viewParams.contains( it.key() ) && viewParams.value( it.key() ) == it.value() )
            {
                r.addError( QStringLiteral( "answer_leak" ), at,
                            QStringLiteral( "parameter value leaked into the student view" ) );
                continue;
            }
            if ( it.value().isString() )
            {
                const QString quoted =
                  QStringLiteral( "\"" ) + it.value().toString() + QStringLiteral( "\"" );
                if ( !it.value().toString().isEmpty() && blob.contains( quoted ) )
                    r.addError( QStringLiteral( "answer_leak" ), at,
                                QStringLiteral( "parameter value quoted in the student view" ) );
            }
        }
    }
    return r;
}

} // namespace sicnu::teaching_admin
