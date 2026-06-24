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

/**
 * \brief 10-column GCP table styled per design.html ArtboardGeoref.
 *
 * Columns: 启用 / # / X 源 (px) / Y 源 (px) / X 参 (m) / Y 参 (m) / ΔX / ΔY /
 *          RMS (px) / 类型.
 *
 * Row height defaults to 26 px; column widths default to
 * {40, 40, 120, 120, 140, 140, 80, 80, 90, 120}.
 *
 * Column 9 uses an editable RsGcpTypeDelegate combobox; column 0 is a
 * Qt::CheckStateRole enable toggle handled by the model.
 *
 * The widget bubbles model-level edits up as `pointEnabled`,
 * `pointTypeChanged` and `deleteRowsRequested` signals so the main window
 * (Task 11.4.6) can persist and the parameter dock (Task 11.4.7) can refresh.
 */
class QgsGCPListWidget : public QTableView
{
    Q_OBJECT

  public:
    explicit QgsGCPListWidget( QWidget *parent = nullptr );

    /// Attach a non-owning GCP list. Recreates the model bindings.
    void setGCPList( QgsGCPList *list );

  signals:
    /// Emitted when the user toggles a row's enable checkbox.
    void pointEnabled( int row, bool enabled );

    /// Emitted when the user changes a row's type label via the combobox.
    void pointTypeChanged( int row, const QString &type );

    /// Emitted when the user requests deletion of the given rows.
    void deleteRowsRequested( const QList<int> &rows );

  protected:
    void keyPressEvent( QKeyEvent *event ) override;

  private slots:
    void onModelDataChanged( const QModelIndex &topLeft, const QModelIndex &bottomRight, const QList<int> &roles );

  private:
    QgsGCPListModel *mModel = nullptr;
    QgsGCPList *mList = nullptr;
};

#endif
