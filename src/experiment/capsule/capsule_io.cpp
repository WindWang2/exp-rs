// capsule_io.cpp — see capsule_io.h.
#include "capsule_io.h"

#include "data/execution_fingerprint.h"
#include "experiment/experiment_types.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>

namespace sicnu::experiment::capsule
{

namespace
{

Diagnostic failure( const QString &code, const QString &message )
{
    return Diagnostic{ code, message, sicnu::dataset::DiagnosticSeverity::Error };
}

CapsuleIssue contentIssue( const QString &code, const QString &section, const QString &message )
{
    return CapsuleIssue{ code, section, message };
}

/// A value that embeds a machine-local absolute path (POSIX root or Windows
/// drive). Portable refs (workspace:/external:) do not match: the drive
/// pattern requires a slash right after the colon.
const QRegularExpression &absolutePathPattern()
{
    static const QRegularExpression pattern{ QStringLiteral( "^[A-Za-z]:[\\\\/]" ) };
    return pattern;
}

bool looksAbsolutePath( const QString &value )
{
    if ( value.startsWith( QLatin1Char( '/' ) ) )
        return true;
    return absolutePathPattern().match( value ).hasMatch();
}

void scanTree( const QJsonValue &value, const QString &section, CapsuleValidation &validation )
{
    if ( value.isObject() )
    {
        const QJsonObject object = value.toObject();
        for ( auto it = object.begin(); it != object.end(); ++it )
        {
            const QString childSection =
                section.isEmpty() ? it.key() : section + QLatin1Char( '.' ) + it.key();
            if ( RunEnvironment::nameLooksSecret( it.key() ) )
            {
                validation.issues.append( contentIssue(
                    QStringLiteral( "capsule.secret-detected" ), childSection,
                    QStringLiteral( "member name matches the secret denylist" ) ) );
                continue; // do not recurse into a refused member
            }
            scanTree( it.value(), childSection, validation );
        }
        return;
    }
    if ( value.isArray() )
    {
        const QJsonArray array = value.toArray();
        for ( const auto &item : array )
            scanTree( item, section, validation );
        return;
    }
    if ( value.isString() )
    {
        const QString text = value.toString();
        if ( RunEnvironment::valueLooksSecret( text ) )
        {
            validation.issues.append( contentIssue(
                QStringLiteral( "capsule.secret-detected" ), section,
                QStringLiteral( "string value matches a credential shape" ) ) );
        }
        else if ( looksAbsolutePath( text ) )
        {
            validation.issues.append( contentIssue(
                QStringLiteral( "capsule.absolute-path" ), section,
                QStringLiteral( "string value is a machine-local absolute path" ) ) );
        }
    }
}

} // namespace

QJsonObject CapsuleExportReport::toJson() const
{
    QJsonObject json;
    json.insert( QStringLiteral( "ok" ), ok );
    json.insert( QStringLiteral( "path" ), path );
    json.insert( QStringLiteral( "bytes" ), bytes );
    return json;
}

Result<CapsuleExportReport> CapsuleIO::exportCapsule( const CapsuleDocument &doc,
                                                      const QString &path )
{
    if ( !doc.digestValid() )
    {
        return Result<CapsuleExportReport>::failure( failure(
            QStringLiteral( "capsule.digest-mismatch" ),
            QStringLiteral( "refusing to export: the document's self digest does not verify" ) ) );
    }
    const QByteArray bytes = doc.canonicalBytes();
    const QDir dir = QFileInfo( path ).dir();
    if ( !dir.mkpath( QStringLiteral( "." ) ) )
    {
        return Result<CapsuleExportReport>::failure( failure(
            QStringLiteral( "capsule.unwritable" ),
            QStringLiteral( "cannot create parent directory of %1" ).arg( path ) ) );
    }
    // QSaveFile (same doctrine as the study report writer): a capsule is a
    // complete, verifiable byte sequence — a failed write must leave the
    // previous capsule intact instead of truncating it to a partial file.
    QSaveFile file( path );
    if ( !file.open( QIODevice::WriteOnly ) || file.write( bytes ) != bytes.size()
         || !file.commit() )
    {
        return Result<CapsuleExportReport>::failure( failure(
            QStringLiteral( "capsule.unwritable" ),
            QStringLiteral( "cannot write %1: %2" ).arg( path, file.errorString() ) ) );
    }
    CapsuleExportReport report;
    report.ok = true;
    report.path = path;
    report.bytes = bytes.size();
    return Result<CapsuleExportReport>::success( report );
}

Result<CapsuleDocument> CapsuleIO::fromBytes( const QByteArray &bytes )
{
    const QJsonDocument parsed = QJsonDocument::fromJson( bytes );
    if ( parsed.isNull() || !parsed.isObject() )
    {
        return Result<CapsuleDocument>::failure( failure(
            QStringLiteral( "capsule.parse-error" ),
            QStringLiteral( "capsule file is not a JSON object document" ) ) );
    }
    const QJsonObject root = parsed.object();
    // Canonical-form gate: the wire form of a capsule is exactly the
    // canonical serialization. A hand-reformatted copy may be semantically
    // identical but is refused so integrity stays byte-checkable.
    if ( sicnu::data::canonicalizeJsonRfc8785( root ) != bytes )
    {
        return Result<CapsuleDocument>::failure( failure(
            QStringLiteral( "capsule.not-canonical" ),
            QStringLiteral( "file bytes are not the canonical serialization of the document; "
                            "re-export instead of reformatting" ) ) );
    }
    const CapsuleValidation shape = validateShape( root );
    if ( !shape.ok )
    {
        QVector<Diagnostic> diagnostics;
        for ( const auto &issue : shape.issues )
            diagnostics.append( failure( issue.code, issue.message ) );
        return Result<CapsuleDocument>::failure( diagnostics );
    }
    return Result<CapsuleDocument>::success( CapsuleDocument::fromRoot( root ) );
}

Result<CapsuleDocument> CapsuleIO::loadCapsule( const QString &path )
{
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) )
    {
        return Result<CapsuleDocument>::failure( failure(
            QStringLiteral( "capsule.unreadable" ),
            QStringLiteral( "cannot read %1" ).arg( path ) ) );
    }
    return fromBytes( file.readAll() );
}

CapsuleValidation CapsuleIO::validate( const CapsuleDocument &doc )
{
    CapsuleValidation validation = validateShape( doc.root() );
    const int before = validation.issues.size();
    scanTree( doc.root(), QString(), validation );
    if ( validation.issues.size() == before )
        validation.checks << QStringLiteral( "content: no secret-shaped or absolute-path values" );
    validation.ok = validation.issues.isEmpty();
    return validation;
}

} // namespace sicnu::experiment::capsule
