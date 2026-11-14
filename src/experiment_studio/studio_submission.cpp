// studio_submission.cpp — see studio_submission.h for the contract.
#include "experiment_studio/studio_submission.h"
#include "experiment_studio/studio_json_numeric.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>

#include <algorithm>
#include <cmath>

namespace sicnu::experiment_studio
{

namespace
{

/// Minimal RFC-8785-style canonicalization for the fields a study spec can
/// carry: objects with lexicographically sorted keys, compact scalars. We do
/// NOT reuse the data-layer canonicalizer here — this leaf links Qt Core and
/// Sicnu::study only, and must stay a projection module.
void appendCanonical( const QJsonValue &value, QByteArray *out );

void appendCanonicalObject( const QJsonObject &object, QByteArray *out )
{
    QStringList keys = object.keys();
    std::sort( keys.begin(), keys.end() );
    out->append( '{' );
    bool first = true;
    for ( const QString &key : keys )
    {
        if ( !first ) out->append( ',' );
        first = false;
        appendCanonical( QJsonValue( key ), out );
        out->append( ':' );
        appendCanonical( object.value( key ), out );
    }
    out->append( '}' );
}

void appendCanonical( const QJsonValue &value, QByteArray *out )
{
    if ( value.isNull() || value.isUndefined() )
    {
        out->append( "null" );
    }
    else if ( value.isBool() )
    {
        out->append( value.toBool() ? "true" : "false" );
    }
    else if ( isNumericJsonValue( value ) )
    {
        const double d = value.toDouble();
        // Keep integral values integral so a spec written as 20.0 and one
        // written as 20 digest identically.
        if ( std::floor( d ) == d && std::abs( d ) < 9007199254740992.0 )
            out->append( QByteArray::number( static_cast<qint64>( d ) ) );
        else
            out->append( QByteArray::number( d, 'g', 17 ) );
    }
    else if ( value.isString() )
    {
        // Let Qt own JSON string escaping (quotes, control chars, unicode),
        // then take the quoted scalar out of the single-element array.
        const QByteArray wrapped =
            QJsonDocument( QJsonArray{ value } ).toJson( QJsonDocument::Compact );
        if ( wrapped.size() >= 2 && wrapped.startsWith( '[' ) && wrapped.endsWith( ']' ) )
            out->append( wrapped.mid( 1, wrapped.size() - 2 ) );
        else
            out->append( "\"\"" ); // unreachable for a well-formed QJsonValue
    }
    else if ( value.isArray() )
    {
        out->append( '[' );
        bool first = true;
        for ( const QJsonValue &item : value.toArray() )
        {
            if ( !first ) out->append( ',' );
            first = false;
            appendCanonical( item, out );
        }
        out->append( ']' );
    }
    else if ( value.isObject() )
    {
        appendCanonicalObject( value.toObject(), out );
    }
}

QByteArray canonicalSpecBytes( const QJsonObject &specJson )
{
    QByteArray bytes;
    appendCanonicalObject( specJson, &bytes );
    return bytes;
}

} // namespace

QString studioSpecDigest( const QJsonObject &specJson )
{
    return QString::fromLatin1(
        QCryptographicHash::hash( canonicalSpecBytes( specJson ), QCryptographicHash::Sha256 )
            .toHex() );
}

QJsonObject StudioSubmission::toJson() const
{
    QJsonObject o;
    o.insert( QStringLiteral( "schema" ), schema );
    o.insert( QStringLiteral( "submission_id" ), static_cast<double>( submissionId ) );
    o.insert( QStringLiteral( "store_path" ), storePath );
    o.insert( QStringLiteral( "study_id" ), studyId );
    o.insert( QStringLiteral( "experiment_id" ), experimentId );
    o.insert( QStringLiteral( "algorithm_id" ), algorithmId );
    o.insert( QStringLiteral( "spec_digest" ), specDigest );
    o.insert( QStringLiteral( "spec" ), specJson );
    o.insert( QStringLiteral( "designer_vm" ), designerVm );
    return o;
}

Result<StudioSubmission> StudioSubmission::fromJson( const QJsonObject &json )
{
    if ( json.value( QStringLiteral( "schema" ) ).toString()
         != QString::fromUtf8( kStudioSubmissionSchema ) )
    {
        return Result<StudioSubmission>::failure( studioError(
            QStringLiteral( "experiment_studio.submission_schema_mismatch" ),
            QStringLiteral( "foreign submission schema refused" ) ) );
    }
    StudioSubmission s;
    const QJsonValue id = json.value( QStringLiteral( "submission_id" ) );
    if ( !isNumericJsonValue( id ) || id.toDouble() < 0
         || id.toDouble() > 9007199254740991.0
         || std::floor( id.toDouble() ) != id.toDouble() )
    {
        return Result<StudioSubmission>::failure( studioError(
            QStringLiteral( "experiment_studio.submission_bad_id" ),
            QStringLiteral( "submission id must be an exact non-negative integer" ) ) );
    }
    s.submissionId = static_cast<quint64>( id.toDouble() );
    s.storePath = json.value( QStringLiteral( "store_path" ) ).toString();
    s.studyId = json.value( QStringLiteral( "study_id" ) ).toString();
    s.experimentId = json.value( QStringLiteral( "experiment_id" ) ).toString();
    s.algorithmId = json.value( QStringLiteral( "algorithm_id" ) ).toString();
    s.specDigest = json.value( QStringLiteral( "spec_digest" ) ).toString();
    s.specJson = json.value( QStringLiteral( "spec" ) ).toObject();
    s.designerVm = json.value( QStringLiteral( "designer_vm" ) ).toObject();
    if ( s.specDigest.isEmpty() || s.specJson.isEmpty() )
    {
        return Result<StudioSubmission>::failure( studioError(
            QStringLiteral( "experiment_studio.submission_missing_identity" ),
            QStringLiteral( "a submission needs a spec digest and a normalized spec" ) ) );
    }
    return Result<StudioSubmission>::success( s );
}

bool studioSubmissionMatchesReport( const StudioSubmission &submission,
                                    const QJsonObject &studyReportJson,
                                    QString *why )
{
    const auto refuse = [why]( const QString &code ) {
        if ( why ) *why = code;
        return false;
    };
    const QJsonObject reportSpec = studyReportJson.value( QStringLiteral( "spec" ) ).toObject();
    if ( reportSpec.isEmpty() )
        return refuse( QStringLiteral( "experiment_studio.submission_report_without_spec" ) );
    if ( studioSpecDigest( reportSpec ) != submission.specDigest )
        return refuse( QStringLiteral( "experiment_studio.submission_spec_mismatch" ) );
    const QString reportStudy = studyReportJson.value( QStringLiteral( "study_id" ) ).toString();
    const QString reportExperiment =
        studyReportJson.value( QStringLiteral( "experiment_id" ) ).toString();
    if ( !submission.studyId.isEmpty() && !reportStudy.isEmpty()
         && reportStudy != submission.studyId )
        return refuse( QStringLiteral( "experiment_studio.submission_study_mismatch" ) );
    if ( !submission.experimentId.isEmpty() && !reportExperiment.isEmpty()
         && reportExperiment != submission.experimentId )
        return refuse( QStringLiteral( "experiment_studio.submission_experiment_mismatch" ) );
    return true;
}

bool studioSubmissionIsStale( quint64 deliveredId,
                              const std::optional<StudioSubmission> &active,
                              const QString &currentStorePath,
                              QString *why )
{
    const auto refuse = [why]( const QString &code ) {
        if ( why ) *why = code;
        return true;
    };
    if ( !active.has_value() )
        return refuse( QStringLiteral( "experiment_studio.submission_no_active" ) );
    if ( active->submissionId != deliveredId )
        return refuse( QStringLiteral( "experiment_studio.submission_expired_id" ) );
    if ( !active->storePath.isEmpty() && !currentStorePath.isEmpty()
         && active->storePath != currentStorePath )
        return refuse( QStringLiteral( "experiment_studio.submission_store_changed" ) );
    return false;
}

} // namespace sicnu::experiment_studio
