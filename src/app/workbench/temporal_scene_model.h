/***************************************************************************
 * temporal_scene_model.h — Workbench 7.0 temporal scene browser (§D)
 *
 * A paged, metadata-only projection of one TemporalCollection's scenes
 * (parsed from the DataManager descriptor through the authoritative
 * sicnu::temporal typed layer). Rasters are never read here — pixels stay
 * with the canvas/preview seams — so memory is bounded by scene COUNT, and
 * the visible page bounds the render cost no matter how large the
 * collection is (goal §D: keep large collections paginated).
 ***************************************************************************/
#pragma once

#include <QAbstractTableModel>
#include <QDate>
#include <QVector>

#include "processing/algorithms/temporal/temporal_collection.h"

namespace sicnu::app
{

class TemporalSceneModel : public QAbstractTableModel
{
    Q_OBJECT
  public:
    enum Column
    {
        Date = 0,
        Platform,
        Modality,
        Cloud,
        Bands,
        Path,
        ColumnCount
    };

    static constexpr int kPageSize = 200;

    explicit TemporalSceneModel( QObject *parent = nullptr );

    /// Replaces the scene set (metadata refs only) and resets to page 0.
    void setScenes( const QVector<sicnu::temporal::TemporalSceneRef> &scenes );

    // Date filtering (inclusive). Empty = unbounded.
    void setDateFilter( const QDate &from, const QDate &to );

    // Paging over the FILTERED set (goal §D: large collections stay paginated).
    int page() const { return m_page; }
    int pageCount() const;
    int totalScenes() const { return m_filtered.size(); }
    void setPage( int page );

    /// Global (unpaged) index of a row, or -1; used for preview/compare.
    int sceneIndexAtRow( int row ) const;
    /// Inverse mapping: global scene index → visible row, or -1 when the
    /// scene is hidden by the active date filter. Keeps the timeline strip
    /// (full collection) and the paged table (filtered) in ONE index story.
    int rowForSceneIndex( int globalIndex ) const;
    const sicnu::temporal::TemporalSceneRef *sceneAtRow( int row ) const;

    // QAbstractTableModel
    int rowCount( const QModelIndex &parent = QModelIndex() ) const override;
    int columnCount( const QModelIndex &parent = QModelIndex() ) const override;
    QVariant data( const QModelIndex &index, int role = Qt::DisplayRole ) const override;
    QVariant headerData( int section, Qt::Orientation orientation,
                         int role = Qt::DisplayRole ) const override;

  signals:
    void pageChanged( int page, int pageCount );

  private:
    void refilter();

    QVector<sicnu::temporal::TemporalSceneRef> m_scenes;
    QVector<int> m_filtered; ///< indices into m_scenes passing the date filter
    QDate m_from;
    QDate m_to;
    int m_page = 0;
};

} // namespace sicnu::app
