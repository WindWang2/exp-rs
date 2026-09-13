/***************************************************************************
 * temporal_scene_model.cpp — see temporal_scene_model.h
 ***************************************************************************/
#include "temporal_scene_model.h"

namespace sicnu::app
{

TemporalSceneModel::TemporalSceneModel( QObject *parent )
    : QAbstractTableModel( parent )
{
}

void TemporalSceneModel::setScenes( const QVector<sicnu::temporal::TemporalSceneRef> &scenes )
{
    beginResetModel();
    m_scenes = scenes;
    m_page = 0;
    refilter();
    endResetModel();
}

void TemporalSceneModel::setDateFilter( const QDate &from, const QDate &to )
{
    if ( m_from == from && m_to == to )
        return;
    beginResetModel();
    m_from = from;
    m_to = to;
    m_page = 0;
    refilter();
    endResetModel();
}

int TemporalSceneModel::pageCount() const
{
    if ( m_filtered.isEmpty() )
        return 1;
    return ( m_filtered.size() + kPageSize - 1 ) / kPageSize;
}

void TemporalSceneModel::setPage( int page )
{
    const int clamped = qBound( 0, page, pageCount() - 1 );
    if ( clamped == m_page )
        return;
    beginResetModel();
    m_page = clamped;
    endResetModel();
    emit pageChanged( m_page, pageCount() );
}

int TemporalSceneModel::sceneIndexAtRow( int row ) const
{
    const int offset = m_page * kPageSize;
    const int index = offset + row;
    if ( index < 0 || index >= m_filtered.size() )
        return -1;
    return m_filtered[index];
}

int TemporalSceneModel::rowForSceneIndex( int globalIndex ) const
{
    if ( globalIndex < 0 || globalIndex >= m_scenes.size() )
        return -1;
    return m_filtered.indexOf( globalIndex );
}

const sicnu::temporal::TemporalSceneRef *TemporalSceneModel::sceneAtRow( int row ) const
{
    const int index = sceneIndexAtRow( row );
    if ( index < 0 )
        return nullptr;
    return &m_scenes[index];
}

int TemporalSceneModel::rowCount( const QModelIndex &parent ) const
{
    if ( parent.isValid() )
        return 0;
    const int offset = m_page * kPageSize;
    return qBound( 0, m_filtered.size() - offset, kPageSize );
}

int TemporalSceneModel::columnCount( const QModelIndex &parent ) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant TemporalSceneModel::data( const QModelIndex &index, int role ) const
{
    const sicnu::temporal::TemporalSceneRef *scene = sceneAtRow( index.row() );
    if ( !scene )
        return {};
    if ( role == Qt::TextAlignmentRole && index.column() == Cloud )
        return Qt::AlignCenter;
    if ( role != Qt::DisplayRole && role != Qt::ToolTipRole )
        return {};
    if ( role == Qt::ToolTipRole )
        return scene->path;

    switch ( index.column() )
    {
        case Date:
            return scene->time.valid ? scene->time.dateString() : tr( "Unknown date" );
        case Platform:
            return scene->platform.isEmpty() ? QStringLiteral( "—" ) : scene->platform;
        case Modality:
            return scene->modality.isEmpty() ? QStringLiteral( "—" ) : scene->modality;
        case Cloud:
            if ( scene->cloudCoverPercent < 0 )
                return tr( "Not reported" );
            return QStringLiteral( "%1%" ).arg( scene->cloudCoverPercent, 0, 'f', 0 );
        case Bands:
        {
            if ( scene->maskBand > 0 )
                return tr( "QA Mask (%1)" ).arg( scene->maskBand );
            if ( scene->qualityBand > 0 )
                return tr( "Quality Band (%1)" ).arg( scene->qualityBand );
            return QStringLiteral( "—" );
        }
        case Path:
            return scene->path;
    }
    return {};
}

QVariant TemporalSceneModel::headerData( int section, Qt::Orientation orientation, int role ) const
{
    if ( orientation != Qt::Horizontal || role != Qt::DisplayRole )
        return QAbstractTableModel::headerData( section, orientation, role );
    switch ( section )
    {
        case Date:
            return tr( "Date" );
        case Platform:
            return tr( "Platform" );
        case Modality:
            return tr( "Modality" );
        case Cloud:
            return tr( "Cloud Cover" );
        case Bands:
            return tr( "QA" );
        case Path:
            return tr( "Path" );
    }
    return {};
}

void TemporalSceneModel::refilter()
{
    m_filtered.clear();
    m_filtered.reserve( m_scenes.size() );
    for ( int i = 0; i < m_scenes.size(); ++i )
    {
        const sicnu::temporal::TemporalSceneRef &scene = m_scenes[i];
        if ( ( m_from.isValid() || m_to.isValid() ) && !scene.time.valid )
            continue; // filtered views can only place scenes with a real date
        if ( m_from.isValid() || m_to.isValid() )
        {
            const QDate date = scene.time.dateString().isEmpty()
                                   ? QDate()
                                   : QDate::fromString( scene.time.dateString(), Qt::ISODate );
            // A scene whose date cannot be parsed must never silently pass a
            // date window it cannot be placed in.
            if ( !date.isValid() )
                continue;
            if ( m_from.isValid() && date < m_from )
                continue;
            if ( m_to.isValid() && date > m_to )
                continue;
        }
        m_filtered.append( i );
    }
}

} // namespace sicnu::app
