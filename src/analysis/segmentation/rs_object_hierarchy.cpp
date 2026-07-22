// rs_object_hierarchy.cpp — Hierarchy store + buildLevels orchestration.
#include "rs_object_hierarchy.h"

#include "sicnu_logging.h"

#include <QPair>

#include <algorithm>

const RsSegmentMap RsObjectHierarchy::sEmptyMap;
const QMap<quint32, quint32> RsObjectHierarchy::sEmptyParentTable;

void RsObjectHierarchy::clear()
{
    mLevels.clear();
    mParents.clear();
    mChildren.clear();
}

const RsSegmentMap &RsObjectHierarchy::level( int i ) const
{
    if ( i < 0 || i >= mLevels.size() )
        return sEmptyMap;
    return mLevels[i];
}

const QMap<quint32, quint32> &RsObjectHierarchy::parentTable( int fineLevel ) const
{
    if ( fineLevel < 0 || fineLevel >= mParents.size() )
        return sEmptyParentTable;
    return mParents[fineLevel];
}

quint32 RsObjectHierarchy::parentOf( int fineLevel, quint32 fineId ) const
{
    if ( fineId == 0 || fineLevel < 0 || fineLevel >= mParents.size() )
        return 0;
    return mParents[fineLevel].value( fineId, 0 );
}

QVector<quint32> RsObjectHierarchy::childrenOf( int coarseLevel, quint32 coarseId ) const
{
    if ( coarseId == 0 || coarseLevel < 1 || coarseLevel >= mLevels.size() )
        return {};
    const int edge = coarseLevel - 1;
    if ( edge < 0 || edge >= mChildren.size() )
        return {};
    return mChildren[edge].value( coarseId );
}

int RsObjectHierarchy::childCount( int level, quint32 segmentId ) const
{
    if ( level <= 0 || segmentId == 0 )
        return 0;
    return childrenOf( level, segmentId ).size();
}

double RsObjectHierarchy::areaRatioToParent( int level, quint32 segmentId ) const
{
    if ( segmentId == 0 || level < 0 || level + 1 >= mLevels.size() )
        return 0.0;

    const quint32 parentId = parentOf( level, segmentId );
    if ( parentId == 0 )
        return 0.0;

    const int childArea = mLevels[level].pixelCount( segmentId );
    const int parentArea = mLevels[level + 1].pixelCount( parentId );
    if ( parentArea <= 0 || childArea <= 0 )
        return 0.0;
    return static_cast<double>( childArea ) / static_cast<double>( parentArea );
}

bool RsObjectHierarchy::validateGridSizes( QString *error ) const
{
    if ( mLevels.isEmpty() )
        return true;
    const int w = mLevels[0].width();
    const int h = mLevels[0].height();
    for ( int i = 1; i < mLevels.size(); ++i )
    {
        if ( mLevels[i].width() != w || mLevels[i].height() != h )
        {
            if ( error )
            {
                *error = QStringLiteral(
                             "Hierarchy grid size mismatch at level %1: %2x%3 vs level0 %4x%5" )
                           .arg( i )
                           .arg( mLevels[i].width() )
                           .arg( mLevels[i].height() )
                           .arg( w )
                           .arg( h );
            }
            return false;
        }
    }
    return true;
}

void RsObjectHierarchy::rebuildChildrenIndex()
{
    mChildren.clear();
    mChildren.resize( mParents.size() );
    for ( int e = 0; e < mParents.size(); ++e )
    {
        for ( auto it = mParents[e].constBegin(); it != mParents[e].constEnd(); ++it )
        {
            const quint32 fineId = it.key();
            const quint32 parentId = it.value();
            if ( parentId == 0 )
                continue;
            mChildren[e][parentId].append( fineId );
        }
        // Stable order for deterministic tests
        for ( auto it = mChildren[e].begin(); it != mChildren[e].end(); ++it )
            std::sort( it.value().begin(), it.value().end() );
    }
}

bool RsObjectHierarchy::setLevels( QVector<RsSegmentMap> levels,
                                   QVector<RsParentTable> parentTables,
                                   QString *error )
{
    if ( levels.isEmpty() )
    {
        if ( error )
            *error = QStringLiteral( "Hierarchy requires at least one level" );
        return false;
    }
    if ( parentTables.size() != levels.size() - 1 )
    {
        if ( error )
        {
            *error = QStringLiteral(
                         "parentTables size (%1) must be levels.size()-1 (%2)" )
                       .arg( parentTables.size() )
                       .arg( levels.size() - 1 );
        }
        return false;
    }

    for ( int i = 0; i < parentTables.size(); ++i )
    {
        if ( !parentTables[i].ok )
        {
            if ( error )
            {
                *error = parentTables[i].errorMessage.isEmpty()
                           ? QStringLiteral( "Parent table %1 is not ok" ).arg( i )
                           : parentTables[i].errorMessage;
            }
            return false;
        }
    }

    mLevels = std::move( levels );
    if ( !validateGridSizes( error ) )
    {
        clear();
        return false;
    }

    mParents.clear();
    mParents.reserve( parentTables.size() );
    for ( const RsParentTable &t : parentTables )
        mParents.append( t.fineToParent );

    rebuildChildrenIndex();
    return true;
}

bool RsObjectHierarchy::buildLevels( const QString &rasterPath,
                                     const QVector<RsLevelSpec> &levelSpecs,
                                     RsSegmenterPort &segmenter,
                                     const RsParentLinkStrategy &linker,
                                     QString *error,
                                     const std::function<bool()> &isCanceled )
{
    clear();

    if ( rasterPath.isEmpty() )
    {
        if ( error )
            *error = QStringLiteral( "buildLevels: empty raster path" );
        return false;
    }
    if ( levelSpecs.isEmpty() )
    {
        if ( error )
            *error = QStringLiteral( "buildLevels: no level specs" );
        return false;
    }

    QVector<RsSegmentMap> levels;
    levels.reserve( levelSpecs.size() );

    for ( int i = 0; i < levelSpecs.size(); ++i )
    {
        if ( isCanceled && isCanceled() )
        {
            if ( error )
                *error = QStringLiteral( "buildLevels canceled" );
            clear();
            return false;
        }

        SICNU_LOG_INFO( SicnuLogTags::Segmentation,
                        QStringLiteral( "buildLevels: segmenting level %1" ).arg( i ) );

        RsSegmenterResult seg = segmenter.segment( rasterPath, levelSpecs[i], isCanceled );
        if ( !seg.ok || seg.segMap.isEmpty() || seg.segMap.segmentCount() == 0 )
        {
            if ( error )
            {
                if ( seg.ok && !seg.segMap.isEmpty() && seg.segMap.segmentCount() == 0 )
                {
                    *error = QStringLiteral(
                                 "Level %1: segmentation produced no objects (all labels are 0/nodata)" )
                               .arg( i );
                }
                else
                {
                    *error = seg.errorMessage.isEmpty()
                               ? QStringLiteral( "Segmentation failed at level %1" ).arg( i )
                               : QStringLiteral( "Level %1: %2" ).arg( i ).arg( seg.errorMessage );
                }
            }
            clear();
            return false;
        }
        levels.append( std::move( seg.segMap ) );
    }

    // Grid size check before linking
    mLevels = levels;
    if ( !validateGridSizes( error ) )
    {
        clear();
        return false;
    }

    QVector<RsParentTable> tables;
    tables.reserve( levels.size() - 1 );
    for ( int i = 0; i + 1 < levels.size(); ++i )
    {
        if ( isCanceled && isCanceled() )
        {
            if ( error )
                *error = QStringLiteral( "buildLevels canceled during parent-link" );
            clear();
            return false;
        }

        SICNU_LOG_INFO( SicnuLogTags::Segmentation,
                        QStringLiteral( "buildLevels: linking level %1 → %2" ).arg( i ).arg( i + 1 ) );

        RsParentTable edge = linker.link( levels[i], levels[i + 1] );
        if ( !edge.ok )
        {
            if ( error )
            {
                *error = edge.errorMessage.isEmpty()
                           ? QStringLiteral( "Parent-link failed for edge %1→%2" ).arg( i ).arg( i + 1 )
                           : edge.errorMessage;
            }
            clear();
            return false;
        }
        tables.append( std::move( edge ) );
    }

    // mLevels already set; install parent tables without re-copying maps
    mParents.clear();
    mParents.reserve( tables.size() );
    for ( const RsParentTable &t : tables )
        mParents.append( t.fineToParent );
    rebuildChildrenIndex();

    SICNU_LOG_SUCCESS( SicnuLogTags::Segmentation,
                       QStringLiteral( "buildLevels complete: %1 levels" ).arg( mLevels.size() ) );
    return true;
}

bool RsObjectHierarchy::relinkEdgesTouching( int changedLevel,
                                             const RsParentLinkStrategy &linker,
                                             QString *error )
{
    if ( mLevels.size() < 2 )
    {
        if ( error )
            *error = QStringLiteral( "relinkEdgesTouching requires at least 2 levels" );
        return false;
    }
    if ( changedLevel < 0 || changedLevel >= mLevels.size() )
    {
        if ( error )
            *error = QStringLiteral( "changedLevel out of range" );
        return false;
    }

    // Edges touching changedLevel: (changedLevel-1 → changedLevel) and (changedLevel → changedLevel+1).
    // Stage updates so a later edge failure does not leave partial parent tables.
    QVector<QPair<int, QMap<quint32, quint32>>> staged;
    const int edges[] = { changedLevel - 1, changedLevel };
    for ( int edge : edges )
    {
        if ( edge < 0 || edge >= mParents.size() )
            continue;

        RsParentTable t = linker.link( mLevels[edge], mLevels[edge + 1] );
        if ( !t.ok )
        {
            if ( error )
                *error = t.errorMessage;
            return false;
        }
        staged.append( qMakePair( edge, std::move( t.fineToParent ) ) );
    }

    for ( auto &pair : staged )
        mParents[pair.first] = std::move( pair.second );

    rebuildChildrenIndex();
    return true;
}
