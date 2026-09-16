// sar_unwrap_provider.cpp — see sar_unwrap_provider.h
#include "sar_unwrap_provider.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <cmath>
#include <limits>

namespace sicnu::sar
{

namespace
{
constexpr float kNanFloat = std::numeric_limits<float>::quiet_NaN();

bool writeRawFloat32( const QString &path, const float *values, size_t count )
{
    QFile file( path );
    if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
        return false;
    const qint64 bytes = static_cast<qint64>( count ) * static_cast<qint64>( sizeof( float ) );
    const qint64 written =
        file.write( reinterpret_cast<const char *>( values ), bytes );
    return written == bytes;
}

bool readRawFloat32( const QString &path, std::vector<float> &out )
{
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) )
        return false;
    const qint64 bytes = file.size();
    if ( bytes <= 0 || bytes % static_cast<qint64>( sizeof( float ) ) != 0 )
        return false;
    out.resize( static_cast<size_t>( bytes / static_cast<qint64>( sizeof( float ) ) ) );
    return file.read( reinterpret_cast<char *>( out.data() ), bytes ) == bytes;
}

QString envVarForProvider( const QString &name )
{
    return QStringLiteral( "SICNU_SAR_UNWRAP_%1_BIN" ).arg( name.toUpper() );
}
} // namespace

bool isValidProviderName( const QString &name )
{
    if ( name.isEmpty() || name == QLatin1String( "builtin" ) )
        return false;
    for ( const QChar &ch : name )
    {
        const char c = ch.toLatin1();
        const bool ok = ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' )
                        || ( c >= '0' && c <= '9' ) || c == '_' || c == '-';
        if ( !ok )
            return false;
    }
    return true;
}

UnwrapProviderStatus runExternalUnwrapProvider( const UnwrapProviderRequest &request,
                                                UnwrapProviderResult *result,
                                                QString *error )
{
    const auto fail = [error]( UnwrapProviderStatus status, const QString &code,
                               const QString &message ) {
        if ( error )
            *error = code + QStringLiteral( ": " ) + message;
        return status;
    };

    if ( result == nullptr || request.wrapped == nullptr || request.w <= 0
         || request.h <= 0 )
        return fail( UnwrapProviderStatus::Failed, QStringLiteral( "UNWRAP_PROVIDER_FAILED" ),
                     QStringLiteral( "malformed provider request" ) );
    result->commandLine.clear();
    result->validCount = 0;
    result->unwrapped.clear();

    if ( !isValidProviderName( request.providerName ) )
        return fail( UnwrapProviderStatus::Unavailable,
                     QStringLiteral( "UNWRAP_PROVIDER_UNAVAILABLE" ),
                     QStringLiteral( "'%1' is not a valid provider name (builtin is the "
                                     "built-in; external names match [A-Za-z0-9_-]+)" )
                         .arg( request.providerName ) );

    // --- Binary discovery: explicit path > env > PATH -------------------
    QString bin = request.binPath;
    if ( bin.isEmpty() )
        bin = qEnvironmentVariable( envVarForProvider( request.providerName ).toLatin1().constData() );
    if ( bin.isEmpty() )
        bin = QStandardPaths::findExecutable( request.providerName );
    if ( bin.isEmpty() || !QFile::exists( bin ) )
        return fail( UnwrapProviderStatus::Unavailable,
                     QStringLiteral( "UNWRAP_PROVIDER_UNAVAILABLE" ),
                     QStringLiteral( "no binary for unwrap provider '%1' (looked at "
                                     "binPath, %2, PATH) — the built-in is never "
                                     "silently substituted" )
                         .arg( request.providerName,
                               envVarForProvider( request.providerName ) ) );

    // --- Scratch (RAII: removed on every exit path) ---------------------
    if ( request.workDir.isEmpty()
         || !QDir( request.workDir ).exists() )
        return fail( UnwrapProviderStatus::Failed, QStringLiteral( "UNWRAP_PROVIDER_FAILED" ),
                     QStringLiteral( "workDir does not exist: %1" ).arg( request.workDir ) );
    QTemporaryDir scratch(
        QDir( request.workDir ).filePath( QStringLiteral( "unwrap_provider_%1_" )
                                              .arg( request.providerName ) ) );
    if ( !scratch.isValid() )
        return fail( UnwrapProviderStatus::Failed, QStringLiteral( "UNWRAP_PROVIDER_FAILED" ),
                     QStringLiteral( "could not create the provider scratch directory" ) );
    const QString inputPath = scratch.filePath( QStringLiteral( "wrapped.f32" ) );
    const QString outputPath = scratch.filePath( QStringLiteral( "unwrapped.f32" ) );

    // Input plane: NaN pixels are written as 0.0 and re-masked afterwards —
    // the tool never sees a NaN it could choke on, and masked pixels never
    // adopt its guess.
    const size_t n = static_cast<size_t>( request.w ) * request.h;
    std::vector<float> input( n );
    long finiteInput = 0;
    for ( size_t i = 0; i < n; ++i )
    {
        const double phase = request.wrapped[i];
        if ( std::isfinite( phase ) )
        {
            input[i] = static_cast<float>( phase );
            ++finiteInput;
        }
        else
        {
            input[i] = 0.0f;
        }
    }
    if ( finiteInput == 0 )
        return fail( UnwrapProviderStatus::InvalidOutput,
                     QStringLiteral( "UNWRAP_PROVIDER_INVALID_OUTPUT" ),
                     QStringLiteral( "the wrapped phase plane holds no valid samples" ) );
    if ( !writeRawFloat32( inputPath, input.data(), n ) )
        return fail( UnwrapProviderStatus::Failed, QStringLiteral( "UNWRAP_PROVIDER_FAILED" ),
                     QStringLiteral( "could not stage the provider input plane" ) );

    // --- Command line from the template ---------------------------------
    if ( request.argsTemplate.empty() )
        return fail( UnwrapProviderStatus::Failed, QStringLiteral( "UNWRAP_PROVIDER_FAILED" ),
                     QStringLiteral( "empty provider args template (cannot address the "
                                     "data)" ) );
    QStringList args;
    bool referencesOutput = false;
    for ( const QString &rawToken : request.argsTemplate )
    {
        QString token = rawToken;
        token.replace( QLatin1String( "{input}" ), inputPath );
        token.replace( QLatin1String( "{output}" ), outputPath );
        token.replace( QLatin1String( "{width}" ), QString::number( request.w ) );
        token.replace( QLatin1String( "{height}" ), QString::number( request.h ) );
        if ( rawToken.contains( QLatin1String( "{output}" ) ) )
            referencesOutput = true;
        args << token;
    }
    if ( !referencesOutput )
        return fail( UnwrapProviderStatus::Failed, QStringLiteral( "UNWRAP_PROVIDER_FAILED" ),
                     QStringLiteral( "provider args template never references {output}" ) );
    result->commandLine = bin + QLatin1Char( ' ' ) + args.join( QLatin1Char( ' ' ) );

    // --- Run (poll-wait: timeout + cooperative cancel) ------------------
    QProcess process;
    process.setProgram( bin );
    process.setArguments( args );
    process.setWorkingDirectory( scratch.path() );
    process.setProcessChannelMode( QProcess::MergedChannels );
    process.start();
    if ( process.state() == QProcess::NotRunning )
        return fail( UnwrapProviderStatus::Failed, QStringLiteral( "UNWRAP_PROVIDER_FAILED" ),
                     QStringLiteral( "provider could not start: %1" )
                         .arg( process.errorString() ) );

    const qint64 deadlineMs = static_cast<qint64>( request.timeoutMs );
    const QDateTime started = QDateTime::currentDateTimeUtc();
    bool timedOut = false;
    while ( process.state() != QProcess::NotRunning )
    {
        if ( request.cancelQuery && request.cancelQuery() )
        {
            process.kill();
            if ( !process.waitForFinished( 5000 ) )
                process.terminate();
            return fail( UnwrapProviderStatus::Cancelled, QStringLiteral( "CANCELLED" ),
                         QStringLiteral( "unwrap provider cancelled by the caller; scratch "
                                         "removed" ) );
        }
        if ( started.msecsTo( QDateTime::currentDateTimeUtc() ) > deadlineMs )
        {
            timedOut = true;
            process.kill();
            if ( !process.waitForFinished( 5000 ) )
                process.terminate();
            break;
        }
        process.waitForFinished( 50 );
    }

    if ( timedOut )
        return fail( UnwrapProviderStatus::Timeout,
                     QStringLiteral( "UNWRAP_PROVIDER_TIMEOUT" ),
                     QStringLiteral( "provider exceeded %1 ms and was killed (scratch "
                                     "removed)" )
                         .arg( request.timeoutMs ) );

    if ( process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 )
    {
        const QString tail = QString::fromLocal8Bit( process.readAll() ).right( 512 );
        return fail( UnwrapProviderStatus::Failed, QStringLiteral( "UNWRAP_PROVIDER_FAILED" ),
                     QStringLiteral( "provider '%1' exited abnormally (code %2)%3" )
                         .arg( request.providerName )
                         .arg( process.exitCode() )
                         .arg( tail.isEmpty() ? QString() : QStringLiteral( ": " ) + tail ) );
    }

    // --- Output validation ----------------------------------------------
    std::vector<float> raw;
    if ( !readRawFloat32( outputPath, raw ) || raw.size() != n )
        return fail( UnwrapProviderStatus::InvalidOutput,
                     QStringLiteral( "UNWRAP_PROVIDER_INVALID_OUTPUT" ),
                     QStringLiteral( "provider output is missing or not a %1x%2 Float32 "
                                     "plane" )
                         .arg( request.w )
                         .arg( request.h ) );

    result->unwrapped.resize( n );
    long valid = 0;
    for ( size_t i = 0; i < n; ++i )
    {
        // Re-apply the input validity mask; masked pixels stay NaN.
        if ( !std::isfinite( request.wrapped[i] ) )
        {
            result->unwrapped[i] = kNanFloat;
            continue;
        }
        if ( !std::isfinite( raw[i] ) )
            return fail( UnwrapProviderStatus::InvalidOutput,
                         QStringLiteral( "UNWRAP_PROVIDER_INVALID_OUTPUT" ),
                         QStringLiteral( "provider output holds a non-finite sample at "
                                         "linear index %1 (an unmasked hole — the tool did "
                                         "not solve the field)" )
                             .arg( static_cast<qint64>( i ) ) );
        result->unwrapped[i] = static_cast<double>( raw[i] );
        ++valid;
    }
    result->validCount = valid;
    return UnwrapProviderStatus::Ok;
}

} // namespace sicnu::sar
