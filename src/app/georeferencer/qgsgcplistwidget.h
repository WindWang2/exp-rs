/***************************************************************************
    qgsgcplistwidget.h - SICNU GCP table view (design.html ArtboardGeoref)
     --------------------------------------
    Date                 : 2026-06-02 (SICNU port)
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
#ifndef QGS_GCP_LIST_WIDGET_H
#define QGS_GCP_LIST_WIDGET_H

#include <QTableView>

class QgsGCPList;
class QgsGCPListModel;
class QContextMenuEvent;
class QMouseEvent;

/**
 * \brief 10-column GCP table styled per design.html ArtboardGeoref.
 *
 * Columns: 启用 / # / X 源 (px) / Y 源 (px) / X 参 (m) / Y 参 (m) / ΔX / ΔY /
 *          RMS (px) / 类型.
 *
 * Supports Delete key, right-click context menu (delete / enable / edit /
 * zoom), and double-click to locate on both canvases.
 */
class QgsGCPListWidget : public QTableView
{
    Q_OBJECT

  public:
    explicit QgsGCPListWidget( QWidget *parent = nullptr );

    /// Attach a non-owning GCP list. Recreates the model bindings.
    void setGCPList( QgsGCPList *list );

    QgsGCPListModel *gcpModel() const { return mModel; }

    /// Currently selected row indices (sorted ascending).
    QList<int> selectedRows() const;

  signals:
    /// Emitted when the user toggles a row's enable checkbox.
    void pointEnabled( int row, bool enabled );

    /// Emitted when the user changes a row's type label via the combobox.
    void pointTypeChanged( int row, const QString &type );

    /// Emitted when the user requests deletion of the given rows.
    void deleteRowsRequested( const QList<int> &rows );

    /// Zoom / locate requests (row is list index).
    void zoomToSourceRequested( int row );
    void zoomToDestRequested( int row );
    void zoomToBothRequested( int row );

    /// Row selection changed (for canvas marker highlight). -1 if none.
    void currentGcpRowChanged( int row );

  protected:
    void keyPressEvent( QKeyEvent *event ) override;
    void contextMenuEvent( QContextMenuEvent *event ) override;
    void mouseDoubleClickEvent( QMouseEvent *event ) override;

  private slots:
    void onModelDataChanged( const QModelIndex &topLeft, const QModelIndex &bottomRight, const QList<int> &roles );
    void onSelectionChanged();

  private:
    QgsGCPListModel *mModel = nullptr;
    QgsGCPList *mList = nullptr;
};

#endif
