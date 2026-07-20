// rs_class_quick_list.h — Phase 10A Task 10.3 left-side compact class list.
#pragma once

#include <QWidget>

class QListWidget;
class RsRoiCollection;

/**
 * \brief Compact list view of classes for the left "类别快览" dock.
 *
 * Shows a 14×14 color swatch + "<name>  (<roi count>)" per class, sorted by
 * class id. Rebuilds on RsRoiCollection::changed and ::classDefChanged.
 * Emits currentClassChanged when the user picks a row (synced with the table).
 */
class RsClassQuickList : public QWidget
{
    Q_OBJECT

  public:
    explicit RsClassQuickList( QWidget *parent = nullptr );

    void setRoiCollection( RsRoiCollection *col );
    int rowCount() const;
    void setCurrentClassId( int classId );
    int currentClassId() const;

  signals:
    void currentClassChanged( int classId );

  private slots:
    void rebuild();
    void onItemActivated();

  private:
    QListWidget *mList = nullptr;
    RsRoiCollection *mRois = nullptr;
    int mStickyClassId = 0;
};
