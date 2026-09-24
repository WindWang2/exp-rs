/***************************************************************************
 * mission_timeline_panel.h — desktop surface over the mission task space
 *
 * Track: glm53-mission-runtime-13 (Mission Runtime 13.0)
 *
 * A thin client over the paged MissionTimelineModel: it renders the same
 * projection the MCP/Pi tools render (missionTaskProjectionJson) and owns no
 * mission state itself — the shell drives setTimeline() from the
 * single-authority runtime store. Selection is pushed into SelectionContext
 * so the mission.* commands derive their availability from the same snapshot
 * every other workbench command uses.
 *
 * Incremental updates go through MissionTimelineModel::applyEvents (row-scoped
 * dataChanged), so a 10k-event mission never triggers a full repaint.
 ***************************************************************************/
#pragma once

#include "app/workbench/mission_timeline_model.h"
#include "app/workbench/mission_stage.h"

#include <qgsdockwidget.h>

class QLabel;
class QPushButton;
class QTableView;

namespace sicnu::app
{

class MissionTimelinePanel : public QgsDockWidget
{
    Q_OBJECT
  public:
    explicit MissionTimelinePanel( QWidget *parent = nullptr );

    MissionTimelineModel *model() const { return m_model; }

    /// Replace the rendered timeline (full reset path — project open).
    void setTimeline( const MissionTimeline &timeline );

    /// Apply an incremental event batch after a mutation (no full reset).
    void applyEvents( const MissionTimeline &timeline, quint64 sinceSeq );

    /// Header projection: mission id, current stage, revision, event cursor.
    void setMissionHeader( const QString &missionId, MissionStage stage, quint64 revision,
                           quint64 lastEventSeq );

  signals:
    /// The selected mission task changed (empty when the selection cleared).
    void taskSelected( const QString &taskId, MissionTaskStatus status );
    /// The user asked for a fresh read of the authority (shell reloads).
    void refreshRequested();
    /// Retry / resume requested for the selected task (shell routes through
    /// the same applyMissionAction the agent surface uses).
    void retryRequested( const QString &taskId );
    void resumeRequested( const QString &taskId );

  private slots:
    void onSelectionChanged();
    void onRefreshClicked();
    void onRetryClicked();
    void onResumeClicked();

  private:
    QString selectedTaskId() const;
    MissionTaskStatus selectedTaskStatus( const QString &taskId ) const;
    void updateActionStates();
    /// Re-derives the selection from the model and pushes it (clears when the
    /// selection is gone). Connected to the model's reset and dataChanged so
    /// the pushed task id/status always mirrors the authority.
    void repushSelection();

    MissionTimelineModel *m_model = nullptr;
    QTableView *m_table = nullptr;
    QLabel *m_header = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QPushButton *m_retryButton = nullptr;
    QPushButton *m_resumeButton = nullptr;
};

} // namespace sicnu::app
