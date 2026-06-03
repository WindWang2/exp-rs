// rs_class_table_widget.h — Phase 10A Task 10.3 class table dock widget.
#pragma once

#include <QWidget>
#include <QtGlobal>

class QTableWidget;
class RsRoiCollection;

/**
 * \brief Compact class table for the right-side "类别管理" dock.
 *
 * Lists classes (sorted by id) with 4 columns: color swatch, name, ROI count,
 * pixel count. Reacts live to RsRoiCollection::changed and ::classDefChanged.
 * Emits currentClassChanged(int classId) on row selection — used by Task 10.4
 * to bind the active ROI tool to the currently selected class.
 */
class RsClassTableWidget : public QWidget
{
    Q_OBJECT

  public:
    explicit RsClassTableWidget( QWidget *parent = nullptr );

    void setRoiCollection( RsRoiCollection *col );

    int rowCount() const;
    int roiCountForRow( int row ) const;
    quint64 pixelCountForRow( int row ) const;

    void setCurrentRow( int row );
    int currentClassId() const;

  signals:
    void currentClassChanged( int classId );

  private slots:
    void rebuild();
    void onSelectionChanged();

  private:
    QTableWidget *mTable = nullptr;
    RsRoiCollection *mRois = nullptr;
};
