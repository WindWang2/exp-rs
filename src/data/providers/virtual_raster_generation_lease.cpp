/***************************************************************************
  providers/virtual_raster_generation_lease.cpp — R33-T03 WP06: managed
  retention of published virtual-raster warp-intermediate generations.
  ---------------------------
  Begin                : 2026-11
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#include "virtual_raster_generation_lease.h"

#include "virtual_raster_source_provider.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QStringList>

#include <cstring>

namespace sicnu::data
{

namespace
{

/// Defensive bound: distinct retained generations per VRT. Readers hold
/// leases for the (normally single) generation the current publish
/// references; anything near this bound is a leak or a pathological
/// publisher, and further acquires degrade to the close/reopen contract
/// instead of growing without bound.
constexpr int kMaxRetainedGenerationsPerVrt = 4096;

struct RetentionRegistry
{
    QMutex mutex;
    // absolute VRT path -> generation token -> active lease count
    QHash<QString, QHash<QString, int>> counts;
};

RetentionRegistry &registry()
{
    // Deliberately leaked: the registry is process-wide and outlives every
    // lease (a lease destroyed during static destruction must still find a
    // live mutex — destroying it at exit would make late destructors touch
    // a dead lock).
    static RetentionRegistry *instance = new RetentionRegistry();
    return *instance;
}

bool isHexToken( const QString &token )
{
    if ( token.size() != 8 )
        return false;
    for ( const QChar ch : token )
    {
        const char16_t c = ch.unicode();
        const bool hex = ( c >= u'0' && c <= u'9' ) || ( c >= u'a' && c <= u'f' ) ||
                         ( c >= u'A' && c <= u'F' );
        if ( !hex )
            return false;
    }
    return true;
}

/// The 8-hex generation token of a staged warp-intermediate filename
/// (`<vrt>.warp-<gen8>-<identity>.tif`); empty for foreign spellings the
/// provider never generated (those were never retainable).
QString generationTokenOfFileName( const QString &fileName, const QString &vrtFileName )
{
    const QString prefix = vrtFileName + QStringLiteral( ".warp-" );
    if ( !fileName.startsWith( prefix ) )
        return QString();
    const int tokenStart = prefix.size();
    const int dash = fileName.indexOf( QLatin1Char( '-' ), tokenStart );
    if ( dash < 0 )
        return QString();
    const QString token = fileName.mid( tokenStart, dash - tokenStart );
    return isHexToken( token ) ? token : QString();
}

/// Scans the VRT's own XML for staged warp-intermediate references and
/// returns them (filenames, and their generation tokens). The XML is text
/// the provider itself generated; references appear as plain filenames
/// (relativeToVRT), so a prefix scan is exact — no GDAL open, no network.
/// Heuristic by design (NIT R1-7): the scan looks for `<vrtbase>.warp-`
/// ANYWHERE in the document, so an XML comment or a metadata echo can only
/// WIDEN the live set (harmless over-retention), never lose a reference;
/// the sweep's deletion decision always cross-checks the on-disk names.
struct VrtIntermediateReferences
{
    QStringList fileNames;
    QList<QString> generationTokens;   // distinct, parsed from the names
};

VrtIntermediateReferences scanVrtIntermediates( const QString &vrtPath )
{
    VrtIntermediateReferences result;
    const QFileInfo info( vrtPath );
    // Scan the raw bytes in place (R33-T03 R1-5): a composed VRT can be
    // large (the provider caps payloads at 256 MiB), and converting the
    // whole document to UTF-16 just to find ASCII filenames doubles the
    // transient footprint on the algorithm hot path.
    QFile file( info.absoluteFilePath() );
    if ( !file.open( QIODevice::ReadOnly ) )
        return result;
    const QByteArray bytes = file.readAll();
    const QByteArray prefix = info.fileName().toUtf8() + QByteArrayLiteral( ".warp-" );
    const char nameChars[] = "0123456789abcdefghijklmnopqrstuvwxyz"
                             "ABCDEFGHIJKLMNOPQRSTUVWXYZ.-_";
    int at = 0;
    while ( true )
    {
        at = bytes.indexOf( prefix, at );
        if ( at < 0 )
            break;
        const int nameStart = at;
        int end = at + prefix.size();
        while ( end < bytes.size() )
        {
            const unsigned char c = static_cast<unsigned char>( bytes.at( end ) );
            if ( c == 0 || std::strchr( nameChars, c ) == nullptr )
                break;
            ++end;
        }
        const QByteArray fileName = bytes.mid( nameStart, end - nameStart );
        if ( !fileName.isEmpty() )
        {
            result.fileNames.append( QString::fromUtf8( fileName ) );
            const QString token = generationTokenOfFileName(
              QString::fromUtf8( fileName ), info.fileName() );
            if ( !token.isEmpty() && !result.generationTokens.contains( token ) )
                result.generationTokens.append( token );
        }
        at = end;
    }
    result.fileNames.removeDuplicates();
    return result;
}

} // namespace

QString virtualRasterIntermediateGenerationToken( const QString &vrtPath,
                                                  const QString &fileName )
{
    return generationTokenOfFileName( fileName, QFileInfo( vrtPath ).fileName() );
}

VirtualRasterGenerationLease::VirtualRasterGenerationLease( Control control )
  : m_control( std::make_unique<Control>( std::move( control ) ) )
{
}

VirtualRasterGenerationLease::VirtualRasterGenerationLease(
  VirtualRasterGenerationLease &&other ) noexcept
  : m_control( std::move( other.m_control ) )
{
}

VirtualRasterGenerationLease &VirtualRasterGenerationLease::operator=(
  VirtualRasterGenerationLease &&other ) noexcept
{
    if ( this != &other )
    {
        // Release the CURRENT retention first: the incoming Control takes
        // over the registry registrations, and the old ones must decrement
        // (and possibly trigger their deferred deletion) before that.
        release();
        m_control = std::move( other.m_control );
    }
    return *this;
}

VirtualRasterGenerationLease::~VirtualRasterGenerationLease()
{
    release();
}

QString VirtualRasterGenerationLease::vrtPath() const
{
    return m_control ? m_control->vrtPath : QString();
}

QString VirtualRasterGenerationLease::generationToken() const
{
    return ( m_control && !m_control->tokens.isEmpty() ) ? m_control->tokens.front()
                                                         : QString();
}

void VirtualRasterGenerationLease::release()
{
    if ( !m_control )
        return;
    const Control control = *m_control;
    m_control.reset();

    {
        RetentionRegistry &reg = registry();
        QMutexLocker lock( &reg.mutex );
        const auto pathIt = reg.counts.find( control.vrtPath );
        if ( pathIt != reg.counts.end() )
        {
            for ( const QString &token : control.tokens )
            {
                const auto tokenIt = pathIt->find( token );
                if ( tokenIt != pathIt->end() && --tokenIt.value() <= 0 )
                    pathIt->erase( tokenIt );
            }
            if ( pathIt->isEmpty() )
                reg.counts.erase( pathIt );
        }
    }
    // Deferred deletion attempt (idempotent, cheap): everything the CURRENT
    // publish still references and every generation another active lease
    // retains stays; only truly stale files go. When the released
    // generations are STILL referenced by the current VRT bytes there is
    // nothing this lease's release could make stale — skip the directory
    // scan entirely (the common single-reader case pays no I/O).
    const QStringList stillReferenced = virtualRasterReferencedIntermediates( control.vrtPath );
    QSet<QString> stillReferencedTokens;
    for ( const QString &fileName : stillReferenced )
    {
        const QString token =
          generationTokenOfFileName( fileName, QFileInfo( control.vrtPath ).fileName() );
        if ( !token.isEmpty() )
            stillReferencedTokens.insert( token );
    }
    bool anythingStale = false;
    for ( const QString &token : control.tokens )
    {
        if ( !stillReferencedTokens.contains( token ) )
        {
            anythingStale = true;
            break;
        }
    }
    if ( anythingStale )
        providers::sweepStaleWarpIntermediates( control.vrtPath, stillReferenced );
}

VirtualRasterGenerationLease acquireVirtualRasterGenerationLease( const QString &vrtPath )
{
    const QFileInfo info( vrtPath );
    const QString absolute = info.absoluteFilePath();
    const VrtIntermediateReferences references = scanVrtIntermediates( absolute );
    if ( references.generationTokens.isEmpty() )
        return VirtualRasterGenerationLease{};   // non-warp VRT: nothing to retain

    RetentionRegistry &reg = registry();
    {
        QMutexLocker lock( &reg.mutex );
        QHash<QString, int> &perPath = reg.counts[ absolute ];
        int distinctGenerations = perPath.size();
        for ( const QString &token : references.generationTokens )
            if ( !perPath.contains( token ) )
                ++distinctGenerations;
        if ( distinctGenerations > kMaxRetainedGenerationsPerVrt )
        {
            // Registry full: degrade honestly (no retention, close/reopen
            // contract) instead of refusing the read or growing unbounded.
            // Only drop the bucket this call created — existing leases of
            // OTHER generations keep their registrations.
            if ( perPath.isEmpty() )
                reg.counts.remove( absolute );
            return VirtualRasterGenerationLease{};
        }
        // R1-4 (TOCTOU): the scan ran outside the lock, so a concurrent
        // release's deferred sweep may have deleted the referenced files in
        // between. Re-verify existence under the lock and degrade to the
        // close/reopen contract rather than registering ghosts.
        const QString directory = info.absolutePath();
        for ( const QString &fileName : references.fileNames )
        {
            if ( !QFileInfo::exists( directory + QLatin1Char( '/' ) + fileName ) )
            {
                if ( perPath.isEmpty() )
                    reg.counts.remove( absolute );
                return VirtualRasterGenerationLease{};
            }
        }
        // One lease covers every referenced generation atomically: a
        // multi-generation VRT (unchanged identities reused across
        // publishes) keeps all of them alive for the reader's lifetime.
        for ( const QString &token : references.generationTokens )
            ++perPath[ token ];
        VirtualRasterGenerationLease::Control control;
        control.vrtPath = absolute;
        control.tokens = references.generationTokens;
        return VirtualRasterGenerationLease( std::move( control ) );
    }
}

int virtualRasterGenerationLeaseCount( const QString &vrtPath )
{
    RetentionRegistry &reg = registry();
    QMutexLocker lock( &reg.mutex );
    const auto pathIt = reg.counts.constFind( QFileInfo( vrtPath ).absoluteFilePath() );
    if ( pathIt == reg.counts.constEnd() )
        return 0;
    int total = 0;
    for ( auto it = pathIt->cbegin(); it != pathIt->cend(); ++it )
        total += it.value();
    return total;
}

bool virtualRasterGenerationRetained( const QString &vrtPath, const QString &generationToken )
{
    RetentionRegistry &reg = registry();
    QMutexLocker lock( &reg.mutex );
    const auto pathIt = reg.counts.constFind( QFileInfo( vrtPath ).absoluteFilePath() );
    if ( pathIt == reg.counts.constEnd() )
        return false;
    return pathIt->value( generationToken, 0 ) > 0;
}

QStringList virtualRasterReferencedIntermediates( const QString &vrtPath )
{
    return scanVrtIntermediates( QFileInfo( vrtPath ).absoluteFilePath() ).fileNames;
}

} // namespace sicnu::data
