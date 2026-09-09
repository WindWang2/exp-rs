/***************************************************************************
 * temporal_workbench_panel.h — Workbench 7.0 temporal workbench (§D)
 *
 * Timeline + paginated scene/date browser over project temporal collections
 * (DataManager records → sicnu::temporal typed parse). The panel is a thin
 * client: no second catalog, no raster I/O — the current-timestep preview
 * and the date comparison route through the shell's existing Data/Display
 * and comparison seams.
 ***************************************************************************/
#pragma once

#include <QPointer>
#include <qgsdockwidget.h>

#include <QDate>
#include <QJsonParseError>

#include "temporal_scene_model.h"

class QComboBox;
class QDateEdit;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableView;

namespace sicnu::data
{
class DataManager;
}

namespace sicnu::app
{

/// Minimal timeline strip: paints every scene's acquisition instant along a
/// horizontal axis, the filter window and the current selection. Clicking a
/// tick selects that scene (keyboard: left/right moves the selection).
class TemporalTimelineBar : public QWidget
{
    Q_OBJECT
  public:
    explicit TemporalTimelineBar( QWidget *parent = nullptr );

    void setScenes( const QVector<qint64> &epochMillis );
    void setSelectedIndex( int index );
    void setWindow( qint64 fromMs, qint64 toMs );

    QSize minimumSizeHint() const override { return { 120, 44 }; }

  signals:
    void sceneClicked( int index );

  protected:
    void paintEvent( QPaintEvent *event ) override;
    void mousePressEvent( QMouseEvent *event ) override;
    void keyPressEvent( QKeyEvent *event ) override;

  private:
    int nearestIndex( int x ) const;

    QVector<qint64> m_times;
    int m_selected = -1;
    qint64 m_fromMs = 0;
    qint64 m_toMs = 0;
};

class TemporalWorkbenchPanel : public QgsDockWidget
{
    Q_OBJECT
  public:
    using DataManagerProvider = std::function<sicnu::data::DataManager *()>;

    TemporalWorkbenchPanel( DataManagerProvider provider, QWidget *parent = nullptr );

    /// Re-reads the collection list from the DataManager (metadata only).
    void refreshCollections();

  signals:
    /// Current-timestep quick preview: shell loads the scene path on the map.
    void previewRequested( const QString &path );
    /// Compare two dates through the shell's comparison seam.
    void compareRequested( const QString &pathA, const QString &pathB );

  private slots:
    void onCollectionSelected( int index );
    void onFilterChanged();
    void onSelectionChanged();
    void onPrevPage();
    void onNextPage();

  private:
    void rebuildSceneTable();
    QString selectedScenePath( int *sceneIndexOut = nullptr ) const;

    DataManagerProvider m_dataManager;
    QComboBox *m_collectionCombo = nullptr;
    QLineEdit *m_collectionSummary = nullptr;
    QDateEdit *m_fromEdit = nullptr;
    QDateEdit *m_toEdit = nullptr;
    TemporalTimelineBar *m_timeline = nullptr;
    TemporalSceneModel *m_model = nullptr;
    QTableView *m_view = nullptr;
    QLabel *m_pageLabel = nullptr;
    QPushButton *m_prevPage = nullptr;
    QPushButton *m_nextPage = nullptr;
    QPushButton *m_previewBtn = nullptr;
    QPushButton *m_compareBtn = nullptr;
    QLabel *m_qaLabel = nullptr;
    QJsonParseError m_parseError{};
};

} // namespace sicnu::app
