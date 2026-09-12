/***************************************************************************
 * processing_history_model.cpp — see processing_history_model.h
 ***************************************************************************/
#include "processing_history_model.h"

#include <QFileInfo>

#include <algorithm>

namespace sicnu::app
{

ProcessingHistoryModel::ProcessingHistoryModel( QObject *parent )
    : QAbstractTableModel( parent )
{
}

void ProcessingHistoryModel::setEntries( const QVector<HistoryEntry> &entries )
{
    beginResetModel();
    // Newest first. The caller supplies fresh re-queries of the authoritative
    // sources, so ordering is ours to define: started time desc, then task id.
    QVector<HistoryEntry> sorted = entries;
    std::stable_sort( sorted.begin(), sorted.end(),
                      []( const HistoryEntry &a, const HistoryEntry &b ) {
                          if ( a.started == b.started )
                              return a.taskId > b.taskId;
                          return a.started > b.started;
                      } );
    const bool droppedRows = sorted.size() > kMaxRows;
    if ( droppedRows )
    {
        m_droppedCount += sorted.size() - kMaxRows;
        sorted.resize( kMaxRows );
    }
    m_entries = sorted;
    refilter();
    endResetModel();
    // Emit only once the model is no longer mid-reset: a listener that
    // re-queries rowCount/data in response must see the settled model.
    if ( droppedRows )
        emit droppedCountChanged( m_droppedCount );
}

void ProcessingHistoryModel::setStateFilter( const QString &stateSubstring )
{
    if ( m_stateFilter == stateSubstring )
        return;
    beginResetModel();
    m_stateFilter = stateSubstring;
    refilter();
    endResetModel();
}

void ProcessingHistoryModel::setSearchText( const QString &textSubstring )
{
    if ( m_searchText == textSubstring )
        return;
    beginResetModel();
    m_searchText = textSubstring;
    refilter();
    endResetModel();
}

const HistoryEntry *ProcessingHistoryModel::entryAtRow( int row ) const
{
    if ( row < 0 || row >= m_visible.size() )
        return nullptr;
    return &m_entries[m_visible[row]];
}

int ProcessingHistoryModel::rowCount( const QModelIndex &parent ) const
{
    return parent.isValid() ? 0 : m_visible.size();
}

int ProcessingHistoryModel::columnCount( const QModelIndex &parent ) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant ProcessingHistoryModel::data( const QModelIndex &index, int role ) const
{
    const HistoryEntry *entry = entryAtRow( index.row() );
    if ( !entry )
        return {};
    if ( role == Qt::TextAlignmentRole && index.column() == Progress )
        return Qt::AlignCenter;

    if ( role != Qt::DisplayRole && role != Qt::ToolTipRole )
        return {};

    switch ( index.column() )
    {
        case Title:
            return entry->title;
        case Source:
            return entry->source;
        case State:
            return entry->stateText;
        case Progress:
            if ( entry->progress < 0 )
                return QStringLiteral( "—" );
            return QStringLiteral( "%1%" ).arg( entry->progress, 0, 'f', 0 );
        case Started:
            return entry->started.isValid() ? entry->started.toLocalTime().toString( Qt::ISODate )
                                            : QStringLiteral( "—" );
        case Duration:
        {
            if ( !entry->started.isValid() )
                return QStringLiteral( "—" );
            const QDateTime end =
                entry->ended.isValid() ? entry->ended : QDateTime::currentDateTime();
            const qint64 secs = entry->started.secsTo( end );
            if ( secs < 0 )
                return QStringLiteral( "—" );
            if ( secs < 60 )
                return QStringLiteral( "%1s" ).arg( secs );
            if ( secs < 3600 )
                return QStringLiteral( "%1m%2s" ).arg( secs / 60 ).arg( secs % 60 );
            return QStringLiteral( "%1h%2m" ).arg( secs / 3600 ).arg( ( secs % 3600 ) / 60 );
        }
        case Output:
            if ( entry->outputPaths.isEmpty() )
                return QStringLiteral( "—" );
            if ( entry->outputPaths.size() == 1 )
                return QFileInfo( entry->outputPaths.first() ).fileName();
            return tr( "%1 artifacts (%2 ...)" )
                .arg( entry->outputPaths.size() )
                .arg( QFileInfo( entry->outputPaths.first() ).fileName() );
    }
    return {};
}

QVariant ProcessingHistoryModel::headerData( int section, Qt::Orientation orientation, int role ) const
{
    if ( orientation != Qt::Horizontal || role != Qt::DisplayRole )
        return QAbstractTableModel::headerData( section, orientation, role );
    switch ( section )
    {
        case Title:
            return tr( "Tasks" );
        case Source:
            return tr( "Source" );
        case State:
            return tr( "Status" );
        case Progress:
            return tr( "Progress" );
        case Started:
            return tr( "Start Time" );
        case Duration:
            return tr( "Elapsed" );
        case Output:
            return tr( "Artifacts" );
    }
    return {};
}

void ProcessingHistoryModel::refilter()
{
    m_visible.clear();
    m_visible.reserve( m_entries.size() );
    const QString stateNeedle = m_stateFilter.trimmed();
    const QString textNeedle = m_searchText.trimmed();
    for ( int i = 0; i < m_entries.size(); ++i )
    {
        const HistoryEntry &entry = m_entries[i];
        if ( !stateNeedle.isEmpty() && !entry.stateText.contains( stateNeedle, Qt::CaseInsensitive ) )
            continue;
        if ( !textNeedle.isEmpty() )
        {
            const bool match =
                entry.title.contains( textNeedle, Qt::CaseInsensitive ) ||
                entry.source.contains( textNeedle, Qt::CaseInsensitive ) ||
                entry.algorithmId.contains( textNeedle, Qt::CaseInsensitive ) ||
                entry.runId.contains( textNeedle, Qt::CaseInsensitive ) ||
                QString::number( entry.taskId ) == textNeedle;
            if ( !match )
                continue;
        }
        m_visible.append( i );
    }
}

} // namespace sicnu::app
