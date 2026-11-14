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
    // Bounded streaming hash: a batch report can be large; never slurp it
    // into memory with QFile::readAll().
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) )
        return {};
    constexpr qint64 kMaxReportBytes = 64 * 1024 * 1024;
    if ( file.size() > kMaxReportBytes )
        return {};
    QCryptographicHash hasher( QCryptographicHash::Sha256 );
    char buffer[64 * 1024];
    while ( !file.atEnd() )
    {
        const qint64 got = file.read( buffer, static_cast<qint64>( sizeof( buffer ) ) );
        if ( got <= 0 )
            return {};
        hasher.addData( buffer, got );
    }
    return QString::fromLatin1( hasher.result().toHex() );
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
    // Real source digest: the report file the batch actually published.
    // Validate it BEFORE creating any destination directory so a refused
    // delivery leaves no empty folder behind. Nothing about a score or
    // finding is synthesized here.
    if ( !request.batchReportPath.isEmpty() )
    {
        result.batchReportDigest = fileDigest( request.batchReportPath );
        if ( result.batchReportDigest.size() != 64 )
        {
            result.code = QStringLiteral( "feedback.delivery_bad_source" );
            result.message = QStringLiteral(
                "the real batch report cannot be read/hashed; nothing was written" );
            return result;
        }
    }
    const QDir dir( request.destinationDir );
    if ( !dir.exists() && !QDir().mkpath( request.destinationDir ) )
    {
        result.code = QStringLiteral( "feedback.delivery_mkdir_failed" );
        result.message = QStringLiteral( "cannot create the local feedback destination" );
        return result;
    }

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
    if ( QFileInfo::exists( result.summaryPath ) && !request.overwriteExisting )
    {
        result.conflicts.append( result.summaryPath );
        result.summaryPath.clear(); // never overwrite the earlier class summary
    }
    if ( !result.summaryPath.isEmpty() )
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
    const bool manifestConflict =
        QFileInfo::exists( result.manifestPath ) && !request.overwriteExisting;
    if ( manifestConflict )
    {
        // A previous successful delivery's manifest is evidence; never
        // silently replace it with a later, possibly partial attempt.
        result.conflicts.append( result.manifestPath );
        result.manifestPath.clear();
    }
    QJsonObject manifest = result.toJson();
    manifest.insert( QStringLiteral( "course_id" ), request.courseId );
    manifest.insert( QStringLiteral( "lab_id" ), request.labId );
    manifest.insert( QStringLiteral( "batch_report_path" ), request.batchReportPath );
    manifest.insert( QStringLiteral( "batch_config_digest" ), request.batchConfigDigest );
    manifest.insert( QStringLiteral( "overwrite_existing" ), request.overwriteExisting );
    manifest.insert( QStringLiteral( "no_upload" ), true );
    QString error;
    bool manifestFailed = false;
    if ( manifestConflict )
    {
        manifestFailed = true; // no write attempted: prior evidence preserved
    }
    else if ( !publishBytes( result.manifestPath,
                             QJsonDocument( manifest ).toJson( QJsonDocument::Indented ),
                             &error ) )
    {
        manifestFailed = true;
        result.manifestPath.clear();
        result.writeFailures.append( QStringLiteral( "feedback manifest: " ) + error );
    }
    // R30-T07 R1 (P1-3): "ok" means EVERY guarded pack, the summary AND the
    // manifest landed. A conflict-only re-run (zero packs written) is NOT a
    // success; guard refusals and partials are named explicitly.
    result.partial = !result.writeFailures.isEmpty() || !result.conflicts.isEmpty()
                     || !blockedStudents.isEmpty()
                     || result.writtenPacks.size() != result.guardedIn;
    result.ok = !result.partial && result.writeFailures.isEmpty()
                && !result.summaryPath.isEmpty() && !result.manifestPath.isEmpty()
                && result.guardedIn > 0;
    result.code = manifestFailed
        ? QStringLiteral( "feedback.delivery_manifest_failed" )
        : result.partial ? QStringLiteral( "feedback.delivery_partial" )
                         : QStringLiteral( "feedback.delivery_ok" );
    result.message = manifestFailed
        ? QStringLiteral( "some packs were written but the delivery manifest failed" )
        : result.partial
            ? QStringLiteral( "feedback delivered with explicit partial/conflict status" )
            : QStringLiteral( "feedback packs and class summary delivered locally" );
    return result;
}

} // namespace sicnu::teaching_admin
