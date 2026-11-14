// studio_session_io.cpp — see studio_session_io.h for the contract.
#include "experiment_studio/studio_session_io.h"

#include "experiment_studio/studio_json_numeric.h"
#include "platform/durable_sidecar.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>

#include <cmath>
#include <limits>

namespace sicnu::experiment_studio
{

namespace
{

// isNumericJsonValue lives in studio_json_numeric.h so all three T07 JSON
// consumers (session IO, spec digest, metric cells) share ONE version-independent
// numeric shape test.

} // namespace

const char *studioSessionLoadStatusName( StudioSessionLoadStatus status )
{
    switch ( status )
    {
        case StudioSessionLoadStatus::Ok: return "ok";
        case StudioSessionLoadStatus::Missing: return "missing";
        case StudioSessionLoadStatus::Unreadable: return "unreadable";
        case StudioSessionLoadStatus::Oversize: return "oversize";
        case StudioSessionLoadStatus::DamagedJson: return "damaged_json";
        case StudioSessionLoadStatus::ForeignSchema: return "foreign_schema";
        case StudioSessionLoadStatus::InvalidFields: return "invalid_fields";
        case StudioSessionLoadStatus::RecoveredLastGood: return "recovered_last_good";
    }
    return "unknown";
}

bool studioSessionIntValue( const QJsonValue &value, int minValue, int maxValue,
                            int *out, QString *why )
{
    const auto refuse = [why]( const QString &reason ) {
        if ( why ) *why = reason;
        return false;
    };
    // Qt6 QJson has one numeric Type (Double); `isDouble()` is true for
    // both integer and fractional JSON numbers. Other shapes (string, bool,
    // object, array, null, undefined) are refused before toDouble().
    if ( !isNumericJsonValue( value ) )
        return refuse( QStringLiteral( "expected a JSON number" ) );
    const double d = value.toDouble();
    if ( !std::isfinite( d ) )
        return refuse( QStringLiteral( "number is not finite" ) );
    if ( std::floor( d ) != d )
        return refuse( QStringLiteral( "number is fractional, not an integer" ) );
    if ( d < static_cast<double>( minValue ) || d > static_cast<double>( maxValue ) )
        return refuse( QStringLiteral( "number %1 is outside [%2,%3]" )
                           .arg( d ).arg( minValue ).arg( maxValue ) );
    if ( out ) *out = static_cast<int>( d );
    return true;
}

bool studioSessionInt64Value( const QJsonValue &value, qint64 minValue, qint64 maxValue,
                              qint64 *out, QString *why )
{
    const auto refuse = [why]( const QString &reason ) {
        if ( why ) *why = reason;
        return false;
    };
    if ( !isNumericJsonValue( value ) )
        return refuse( QStringLiteral( "expected a JSON number" ) );
    const double d = value.toDouble();
    if ( !std::isfinite( d ) )
        return refuse( QStringLiteral( "number is not finite" ) );
    if ( std::floor( d ) != d )
        return refuse( QStringLiteral( "number is fractional, not an integer" ) );
    // 2^53 is the last integer a JSON double represents exactly; casting a
    // larger double to qint64 is UB. Refuse instead of clamping.
    if ( d < -9007199254740992.0 || d > 9007199254740992.0 )
        return refuse( QStringLiteral( "number is outside exact JSON integer range" ) );
    const auto integral = static_cast<qint64>( d );
    if ( integral < minValue || integral > maxValue )
        return refuse( QStringLiteral( "number %1 is outside [%2,%3]" )
                           .arg( integral ).arg( minValue ).arg( maxValue ) );
    if ( out ) *out = integral;
    return true;
}

bool studioSessionDoubleValue( const QJsonValue &value, double minValue, double maxValue,
                               double *out, QString *why )
{
    const auto refuse = [why]( const QString &reason ) {
        if ( why ) *why = reason;
        return false;
    };
    if ( !isNumericJsonValue( value ) )
        return refuse( QStringLiteral( "expected a JSON number" ) );
    const double d = value.toDouble();
    if ( !std::isfinite( d ) || d < minValue || d > maxValue )
        return refuse( QStringLiteral( "number is not finite inside [%1,%2]" )
                           .arg( minValue ).arg( maxValue ) );
    if ( out ) *out = d;
    return true;
}

StudioSessionLoadResult loadStudioSessionFile( const QString &path, qint64 maxBytes )
{
    StudioSessionLoadResult result;
    if ( path.isEmpty() || maxBytes <= 0 )
    {
        result.status = StudioSessionLoadStatus::Unreadable;
        result.code = QStringLiteral( "experiment_studio.session_bad_arguments" );
        result.message = QStringLiteral( "no session path / invalid read budget" );
        return result;
    }

    const QString lastGood = path + QString::fromUtf8( kStudioSessionLastGoodSuffix );
    // Pre-flight size check on BOTH candidates. The sidecar reader also
    // bounds its reads; checking here first gives an explicit OVERSIZE
    // verdict instead of folding a hostile file into "unreadable".
    const auto oversized = [maxBytes]( const QString &candidate ) {
        const QFileInfo info( candidate );
        return info.exists() && info.isFile() && info.size() > maxBytes;
    };
    const auto readableLastGood = [maxBytes, &lastGood]() {
        const QFileInfo info( lastGood );
        return info.exists() && info.isFile() && info.size() <= maxBytes;
    };
    // An oversize main file is an explicit OVERSIZE refusal UNLESS a
    // size-bounded last-good generation can still be recovered. Requiring
    // both candidates to be oversize would mislabel a lone oversize main as
    // merely "unreadable".
    if ( oversized( path ) && !readableLastGood() )
    {
        result.status = StudioSessionLoadStatus::Oversize;
        result.code = QStringLiteral( "experiment_studio.session_oversize" );
        result.message = QStringLiteral( "the saved studio session exceeds the read budget and was not restored" );
        return result;
    }

    const sicnu::platform::sidecar::ReadResult read = sicnu::platform::sidecar::read(
        path.toUtf8().constData(), kStudioSessionLastGoodSuffix,
        static_cast<std::size_t>( maxBytes ) );
    using sicnu::platform::sidecar::ReadSource;
    if ( read.source == ReadSource::Missing )
    {
        result.status = StudioSessionLoadStatus::Missing;
        result.code = QStringLiteral( "experiment_studio.session_missing" );
        return result;
    }
    if ( read.source == ReadSource::Corrupt )
    {
        result.status = StudioSessionLoadStatus::Unreadable;
        result.code = QStringLiteral( "experiment_studio.session_unreadable" );
        result.message = QStringLiteral( "a previous studio session exists but cannot be read; it was not restored." );
        result.issues.append( QString::fromUtf8( read.error.c_str() ) );
        return result;
    }

    // R30-T07 R1 (P1-5): ONE typed parse for both the primary document and
    // an explicit last-good fallback. The sidecar resolves main → last-good
    // only for UNREADABLE/OVERSIZE main files; a torn write that produced
    // readable-but-invalid JSON (or a foreign / invalid-field document) must
    // also be recoverable when a good generation exists.
    struct ParsedState
    {
        bool ok = false;
        StudioSessionState state;
        StudioSessionLoadStatus status = StudioSessionLoadStatus::InvalidFields;
        QString code;
        QString message;
        QStringList issues;
    };
    const auto parse = []( const std::string &bytes ) -> ParsedState {
        ParsedState out;
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(
            QByteArray( bytes.data(), static_cast<qsizetype>( bytes.size() ) ), &parseError );
        if ( parseError.error != QJsonParseError::NoError || !doc.isObject() )
        {
            out.status = StudioSessionLoadStatus::DamagedJson;
            out.code = QStringLiteral( "experiment_studio.session_damaged" );
            out.message = QStringLiteral( "The saved studio session is damaged and was not restored. "
                                          "Design anew or re-open your experiment store." );
            out.issues.append( parseError.errorString() );
            return out;
        }
        const QJsonObject object = doc.object();
        const QString schema = object.value( QStringLiteral( "schema" ) ).toString();
        if ( schema != QString::fromUtf8( kStudioSessionSchema ) )
        {
            out.status = StudioSessionLoadStatus::ForeignSchema;
            out.code = QStringLiteral( "experiment_studio.session_foreign_schema" );
            const bool future =
                schema.startsWith( QStringLiteral( "sicnu.experiment_studio.session/" ) );
            out.message = future
                ? QStringLiteral( "The saved studio session was written by a newer version and was not restored." )
                : QStringLiteral( "The saved studio session was written by another version and was not restored." );
            out.issues.append( QStringLiteral( "schema=%1" ).arg( schema ) );
            return out;
        }
        StudioSessionState state = StudioSessionState::fromJson( object );
        if ( !state.ok || !state.issues.isEmpty() )
        {
            out.status = StudioSessionLoadStatus::InvalidFields;
            out.code = QStringLiteral( "experiment_studio.session_invalid_fields" );
            out.message = QStringLiteral( "The saved studio session has invalid fields and was not restored." );
            out.issues = state.issues;
            return out;
        }
        state.schema = schema;
        out.ok = true;
        out.state = state;
        return out;
    };

    ParsedState primary = parse( read.bytes );
    if ( !primary.ok )
    {
        const sicnu::platform::sidecar::ReadResult fallback =
            sicnu::platform::sidecar::read( lastGood.toUtf8().constData(), "",
                                            static_cast<std::size_t>( maxBytes ) );
        if ( fallback.source == ReadSource::Main )
        {
            ParsedState recovered = parse( fallback.bytes );
            if ( recovered.ok )
            {
                result.state = recovered.state;
                result.status = StudioSessionLoadStatus::RecoveredLastGood;
                result.code = QStringLiteral( "experiment_studio.session_recovered" );
                result.message = QStringLiteral(
                    "The main studio session could not be validated; the last good copy was restored." );
                result.issues = primary.issues;
                result.issues.append( primary.code );
                return result;
            }
        }
        result.status = primary.status;
        result.code = primary.code;
        result.message = primary.message;
        result.issues = primary.issues;
        return result;
    }

    result.state = primary.state;
    result.status = read.source == ReadSource::LastGood
                        ? StudioSessionLoadStatus::RecoveredLastGood
                        : StudioSessionLoadStatus::Ok;
    result.code = result.status == StudioSessionLoadStatus::RecoveredLastGood
                      ? QStringLiteral( "experiment_studio.session_recovered" )
                      : QStringLiteral( "experiment_studio.session_ok" );
    if ( result.status == StudioSessionLoadStatus::RecoveredLastGood )
    {
        result.message = QStringLiteral( "The main studio session was unreadable; the last good copy was restored." );
        result.issues.append( QString::fromUtf8( read.error.c_str() ) );
    }
    return result;
}

StudioSessionWriteResult saveStudioSessionFile( const QString &path,
                                                const StudioSessionState &state,
                                                qint64 maxBytes )
{
    StudioSessionWriteResult result;
    if ( path.isEmpty() || maxBytes <= 0 )
    {
        result.code = QStringLiteral( "experiment_studio.session_bad_arguments" );
        result.message = QStringLiteral( "no session path / invalid write budget" );
        return result;
    }
    const QFileInfo info( path );
    if ( info.exists() && !info.isFile() )
    {
        result.code = QStringLiteral( "experiment_studio.session_not_a_file" );
        result.message = QStringLiteral( "the session path is not a regular file" );
        return result;
    }
    if ( !QDir().mkpath( info.absolutePath() ) )
    {
        result.code = QStringLiteral( "experiment_studio.session_mkdir_failed" );
        result.message = QStringLiteral( "cannot create the studio session directory" );
        return result;
    }

    const QByteArray bytes = QJsonDocument( state.toJson() ).toJson( QJsonDocument::Indented );
    if ( bytes.size() > maxBytes )
    {
        result.code = QStringLiteral( "experiment_studio.session_oversize" );
        result.message = QStringLiteral( "the session document exceeds the write budget; previous document kept" );
        result.bytes = bytes.size();
        return result;
    }

    sicnu::platform::sidecar::WriteRequest request;
    request.targetPath = path.toUtf8().constData();
    request.bytes = std::string( bytes.constData(), static_cast<std::size_t>( bytes.size() ) );
    request.lastGoodSuffix = kStudioSessionLastGoodSuffix;
    request.maxBytes = static_cast<std::size_t>( maxBytes );
    const sicnu::platform::sidecar::WriteResult written =
        sicnu::platform::sidecar::write( request );
    result.bytes = bytes.size();
    if ( !written )
    {
        using sicnu::platform::sidecar::WriteStatus;
        // VerifyFailed / LastGoodFailed happen AFTER the atomic rename: the
        // main target now holds the NEW document. Claiming "previous document
        // left intact" there would be a lie; the last-good generation is the
        // recovery path instead.
        const bool published =
            written.status == WriteStatus::VerifyFailed
            || written.status == WriteStatus::LastGoodFailed;
        result.code = published
            ? QStringLiteral( "experiment_studio.session_publish_verify_failed" )
            : QStringLiteral( "experiment_studio.session_write_failed" );
        result.message =
            QStringLiteral( "the studio session could not be saved (%1): %2; %3" )
                .arg( QString::fromUtf8(
                          sicnu::platform::sidecar::writeStatusName( written.status ) ),
                      QString::fromUtf8( written.error.c_str() ),
                      published
                          ? QStringLiteral( "the new document was published but its verification "
                                            "failed — re-open to recover from the last-good copy" )
                          : QStringLiteral( "the previous document was left intact" ) );
        return result;
    }
    result.ok = true;
    result.code = QStringLiteral( "experiment_studio.session_saved" );
    if ( !written.lastGoodWritten )
    {
        // Main is committed and verified; the redundant copy lagged. Report
        // it instead of pretending both generations are current.
        result.code = QStringLiteral( "experiment_studio.session_saved_last_good_failed" );
        result.message = QStringLiteral( "the studio session was saved, but its last-good copy failed to rotate" );
    }
    return result;
}

} // namespace sicnu::experiment_studio
