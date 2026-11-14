#include "teaching_admin/feedback_delivery.h"

#include "platform/durable_sidecar.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

namespace sicnu::teaching_admin
{
namespace
{

QString safeStudentName( const QString &studentId )
{
    QString safe;
    bool rewritten = false;
    for ( QChar ch : studentId )
    {
        if ( ch.isLetterOrNumber() || ch == QLatin1Char( '_' )
             || ch == QLatin1Char( '-' ) )
            safe.append( ch );
        else
        {
            safe.append( QLatin1Char( '_' ) );
            rewritten = true;
        }
    }
    if ( safe.isEmpty() )
        safe = QStringLiteral( "student" );
    if ( rewritten )
        safe += QLatin1Char( '-' )
                + QString::fromLatin1(
                    QCryptographicHash::hash( studentId.toUtf8(),
                                             QCryptographicHash::Sha256 )
                      .toHex().left( 12 ) );
    return safe;
}

QString fileDigest( const QString &path )
{
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) )
        return {};
    return QString::fromLatin1(
        QCryptographicHash::hash( file.readAll(), QCryptographicHash::Sha256 ).toHex() );
}

bool publishBytes( const QString &path, const QByteArray &bytes, QString *error )
{
    sicnu::platform::sidecar::WriteRequest request;
    request.targetPath = path.toUtf8().constData();
    request.bytes = std::string( bytes.constData(), static_cast<std::size_t>( bytes.size() ) );
    request.lastGoodSuffix = "";
    request.maxBytes = 8u * 1024u * 1024u;
    const sicnu::platform::sidecar::WriteResult result =
        sicnu::platform::sidecar::write( request );
    if ( !result )
    {
        *error = QStringLiteral( "%1: %2" )
                     .arg( QString::fromUtf8(
                               sicnu::platform::sidecar::writeStatusName( result.status ) ),
                           QString::fromUtf8( result.error.c_str() ) );
        return false;
    }
    return true;
}

} // namespace

QJsonObject FeedbackDeliveryResult::toJson() const
{
    QJsonObject o;
    o.insert( QStringLiteral( "schema" ),
              QStringLiteral( "sicnu.teaching.feedback_delivery/1" ) );
    o.insert( QStringLiteral( "ok" ), ok );
    o.insert( QStringLiteral( "partial" ), partial );
    o.insert( QStringLiteral( "code" ), code );
    o.insert( QStringLiteral( "message" ), message );
    o.insert( QStringLiteral( "manifest_path" ), manifestPath );
    o.insert( QStringLiteral( "summary_path" ), summaryPath );
    o.insert( QStringLiteral( "batch_report_digest" ), batchReportDigest );
    o.insert( QStringLiteral( "guarded_in" ), guardedIn );
    o.insert( QStringLiteral( "written_packs" ), QJsonArray::fromStringList( writtenPacks ) );
    o.insert( QStringLiteral( "conflicts" ), QJsonArray::fromStringList( conflicts ) );
    o.insert( QStringLiteral( "write_failures" ), QJsonArray::fromStringList( writeFailures ) );
    o.insert( QStringLiteral( "blocked_students" ),
              QJsonArray::fromStringList( blockedStudents ) );
    o.insert( QStringLiteral( "refusals" ), QJsonArray::fromStringList( refusals ) );
    return o;
}

FeedbackDeliveryResult deliverGuardedFeedback(
    const QVector<QJsonObject> &guardedPacks,
    const QJsonObject &classSummary,
    const QStringList &blockedStudents,
    const QStringList &refusals,
    const FeedbackDeliveryRequest &request )
{
    FeedbackDeliveryResult result;
    result.guardedIn = guardedPacks.size();
    result.blockedStudents = blockedStudents;
    result.refusals = refusals;
    if ( request.destinationDir.isEmpty() )
    {
        result.code = QStringLiteral( "feedback.delivery_no_destination" );
        result.message = QStringLiteral( "no local destination chosen; nothing was written" );
        return result;
    }
    const QDir dir( request.destinationDir );
    if ( !dir.exists() && !QDir().mkpath( request.destinationDir ) )
    {
        result.code = QStringLiteral( "feedback.delivery_mkdir_failed" );
        result.message = QStringLiteral( "cannot create the local feedback destination" );
        return result;
    }
    // Real source digest: the report file the batch actually published.
    // Nothing about a score/finding is synthesized here.
    if ( !request.batchReportPath.isEmpty() )
        result.batchReportDigest = fileDigest( request.batchReportPath );

    for ( const QJsonObject &pack : guardedPacks )
    {
        const QString studentId = pack.value( QStringLiteral( "student_id" ) ).toString();
        if ( studentId.isEmpty() )
        {
            result.writeFailures.append(
                QStringLiteral( "unnamed pack refused (missing student_id)" ) );
            continue;
        }
        if ( pack.value( QStringLiteral( "schema" ) ).toString()
             != QString::fromLatin1( kFeedbackPackSchema ) )
        {
            result.writeFailures.append( studentId
                                        + QStringLiteral( ": foreign pack schema refused" ) );
            continue;
        }
        const QString path = dir.filePath(
            QStringLiteral( "feedback-%1.json" ).arg( safeStudentName( studentId ) ) );
        if ( QFileInfo::exists( path ) && !request.overwriteExisting )
        {
            result.conflicts.append( path );
            continue;
        }
        const QByteArray bytes = QJsonDocument( pack ).toJson( QJsonDocument::Indented );
        QString error;
        if ( !publishBytes( path, bytes, &error ) )
        {
            result.writeFailures.append( studentId + QStringLiteral( ": " ) + error );
            continue;
        }
        result.writtenPacks.append( path );
    }

    result.summaryPath = dir.filePath( QStringLiteral( "class-summary.json" ) );
    {
        QJsonObject summary = classSummary;
        summary.insert( QStringLiteral( "delivery_course_id" ), request.courseId );
        summary.insert( QStringLiteral( "delivery_lab_id" ), request.labId );
        summary.insert( QStringLiteral( "batch_report_digest" ), result.batchReportDigest );
        summary.insert( QStringLiteral( "batch_config_digest" ), request.batchConfigDigest );
        summary.insert( QStringLiteral( "blocked_students" ),
                        QJsonArray::fromStringList( blockedStudents ) );
        summary.insert( QStringLiteral( "written_pack_count" ), result.writtenPacks.size() );
        const QByteArray bytes = QJsonDocument( summary ).toJson( QJsonDocument::Indented );
        QString error;
        if ( !publishBytes( result.summaryPath, bytes, &error ) )
        {
            result.writeFailures.append( QStringLiteral( "class summary: " ) + error );
            result.summaryPath.clear();
        }
    }

    // Manifest is published LAST: it is the truthful, atomic record of what
    // actually landed (including conflicts, guard refusals and partials).
    result.manifestPath = dir.filePath( QStringLiteral( "feedback-manifest.json" ) );
    QJsonObject manifest = result.toJson();
    manifest.insert( QStringLiteral( "course_id" ), request.courseId );
    manifest.insert( QStringLiteral( "lab_id" ), request.labId );
    manifest.insert( QStringLiteral( "batch_report_path" ), request.batchReportPath );
    manifest.insert( QStringLiteral( "batch_config_digest" ), request.batchConfigDigest );
    manifest.insert( QStringLiteral( "overwrite_existing" ), request.overwriteExisting );
    manifest.insert( QStringLiteral( "no_upload" ), true );
    QString error;
    if ( !publishBytes( result.manifestPath,
                        QJsonDocument( manifest ).toJson( QJsonDocument::Indented ), &error ) )
    {
        result.code = QStringLiteral( "feedback.delivery_manifest_failed" );
        result.message = error;
        result.manifestPath.clear();
        return result;
    }
    result.partial = !result.writeFailures.isEmpty() || !result.conflicts.isEmpty()
                     || !blockedStudents.isEmpty();
    result.ok = result.writeFailures.isEmpty() && !result.summaryPath.isEmpty();
    result.code = result.partial ? QStringLiteral( "feedback.delivery_partial" )
                                 : QStringLiteral( "feedback.delivery_ok" );
    result.message = result.partial
        ? QStringLiteral( "feedback delivered with explicit partial/conflict status" )
        : QStringLiteral( "feedback packs and class summary delivered locally" );
    return result;
}

} // namespace sicnu::teaching_admin
