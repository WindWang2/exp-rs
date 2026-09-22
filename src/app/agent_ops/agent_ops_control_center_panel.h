/***************************************************************************
 * agent_ops_control_center_panel.h — Control Center UI (Feature H)
 *
 * Session header, timeline stages, current decision, resources, evidence,
 * controls (pause/cancel/resume/approve repair/export). Cannot bypass the
 * AgentLoop state machine — all mutations go through OperationsCoordinator.
 * Visual language mirrors MissionTimelinePanel (header + table + action row).
 ***************************************************************************/
#pragma once

#include "agent_ops/ops_projection.h"
#include "agent_ops/ops_types.h"

#include <QWidget>

class QLabel;
class QPushButton;
class QTableWidget;
class QTextEdit;

namespace sicnu::app::agent_ops {

class AgentOpsControlCenterPanel : public QWidget
{
    Q_OBJECT
  public:
    explicit AgentOpsControlCenterPanel(QWidget *parent = nullptr);

    void setProjection(const sicnu::agent_ops::OpsProjection &projection);
    void setDelivery(const sicnu::agent_ops::FinalDelivery &delivery);
    void setCurrentDecisionJson(const QString &json);

  signals:
    void pauseRequested();
    void cancelRequested();
    void resumeRequested();
    void approveRepairRequested();
    void exportRequested();

  private:
    void updateControls(const Json::Value &controls);

    QLabel *m_header = nullptr;
    QTableWidget *m_timeline = nullptr;
    QTextEdit *m_decision = nullptr;
    QTextEdit *m_resources = nullptr;
    QTextEdit *m_evidence = nullptr;
    QPushButton *m_pause = nullptr;
    QPushButton *m_cancel = nullptr;
    QPushButton *m_resume = nullptr;
    QPushButton *m_approve = nullptr;
    QPushButton *m_export = nullptr;
};

} // namespace sicnu::app::agent_ops
