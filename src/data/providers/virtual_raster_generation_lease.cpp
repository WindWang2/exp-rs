/***************************************************************************
  providers/virtual_raster_generation_lease.cpp — R33-T03 WP06: managed
  retention of published virtual-raster warp-intermediate generations.
  ---------------------------
  Begin                : 2026-11
  Copyright            : (C) 2026 SICNU GEO RS
 ***************************************************************************/

#include "virtual_raster_generation_lease.h"

#include "virtual_raster_source_provider.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QStringList>
#include <QXmlStreamReader>

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

struct VrtIntermediateReferences
{
    bool readable = false;
    QStringList fileNames;
    QList<QString> generationTokens;   // distinct, parsed from the names
};

VrtIntermediateReferences scanVrtIntermediates( const QString &vrtPath )
{
    VrtIntermediateReferences result;
    const QFileInfo info( vrtPath );
    QFile file( info.absoluteFilePath() );
    if ( !file.open( QIODevice::ReadOnly ) || file.size() > 256LL * 1024 * 1024 )
        return result;

    // The XML decoder preserves file names containing entities such as &amp;.
    // Read only SourceFilename entries beside this VRT, without opening GDAL.
    QXmlStreamReader xml( &file );
    const QDir directory = info.absoluteDir();
    while ( !xml.atEnd() )
    {
        if ( xml.readNext() != QXmlStreamReader::StartElement
             || xml.name() != QLatin1String( "SourceFilename" ) )
            continue;
        const bool relative = xml.attributes().value( QLatin1String( "relativeToVRT" ) ) == QLatin1String( "1" );
        const QString name = xml.readElementText();
        const QFileInfo source( relative ? directory.absoluteFilePath( name ) : name );
        if ( !source.isAbsolute() || source.absoluteDir() != directory )
            continue;
        const QString fileName = source.fileName();
        const QString token = generationTokenOfFileName( fileName, info.fileName() );
        if ( token.isEmpty() )
            continue;
        result.fileNames.append( fileName );
        if ( !result.generationTokens.contains( token ) )
            result.generationTokens.append( token );
    }
    if ( xml.hasError() )
        return {};
    result.readable = true;
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
    // Only the released generations can become stale here. Other files may
    // have been staged by a publisher that has yet to replace the VRT.
    const VrtIntermediateReferences live = scanVrtIntermediates( control.vrtPath );
    if ( !live.readable )
        return;
    QStringList staleTokens;
    for ( const QString &token : control.tokens )
        if ( !live.generationTokens.contains( token ) )
            staleTokens.append( token );
    if ( !staleTokens.isEmpty() )
        providers::sweepStaleWarpIntermediates( control.vrtPath, live.fileNames, staleTokens );
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

void removeUnretainedVirtualRasterIntermediate( const QString &vrtPath,
                                               const QString &filePath,
                                               const QString &generationToken )
{
    RetentionRegistry &reg = registry();
    QMutexLocker lock( &reg.mutex );
    const auto pathIt = reg.counts.constFind( QFileInfo( vrtPath ).absoluteFilePath() );
    if ( pathIt == reg.counts.constEnd() || pathIt->value( generationToken, 0 ) == 0 )
        QFile::remove( filePath );
}

QStringList virtualRasterReferencedIntermediates( const QString &vrtPath )
{
    return scanVrtIntermediates( QFileInfo( vrtPath ).absoluteFilePath() ).fileNames;
}

} // namespace sicnu::data
