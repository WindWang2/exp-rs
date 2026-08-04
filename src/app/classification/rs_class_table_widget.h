// rs_class_table_widget.h — Phase 10A Task 10.3 class table dock widget.
#pragma once

#include <QColor>
#include <QWidget>
#include <QtGlobal>

class QTableWidget;
class QTableWidgetItem;
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
    /// Select the row whose class id matches; no-op if not found.
    void setCurrentClassId( int classId );
    int currentClassId() const;
    QList<int> selectedClassIds() const;

    void mergeSelectedClasses( int targetClassId, const QString &targetName, const QColor &targetColor );

  signals:
    void currentClassChanged( int classId );
    void classDefEdited( int classId, const QString &name, const QColor &color );
    void mergeClassesRequested( const QList<int> &sourceClassIds, int targetClassId, const QString &targetName, const QColor &targetColor );

  private slots:
    void rebuild();
    void onSelectionChanged();
    void onCellDoubleClicked( int row, int column );
    void onItemChanged( QTableWidgetItem *item );

  private:
    QTableWidget *mTable = nullptr;
    RsRoiCollection *mRois = nullptr;
    int mStickyClassId = 0; ///< Survives rebuild when the table temporarily clears selection.
    bool mBlockItemChanged = false;
};
