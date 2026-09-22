/***************************************************************************
 * agent_ops_control_center_panel.cpp
 ***************************************************************************/
#include "app/agent_ops/agent_ops_control_center_panel.h"

#include <QHBoxLayout>
#include <QAbstractItemView>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTextEdit>
#include <QVBoxLayout>

#include <json/json.h>

namespace sicnu::app::agent_ops {
namespace {

QString jsonToQString(const Json::Value &value)
{
    Json::StreamWriterBuilder b;
    b["indentation"] = "  ";
    return QString::fromStdString(Json::writeString(b, value));
}

} // namespace

AgentOpsControlCenterPanel::AgentOpsControlCenterPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    m_header = new QLabel(tr("Agent Ops — no session"), this);
    m_header->setObjectName(QStringLiteral("agentOpsHeader"));
    layout->addWidget(m_header);

    m_timeline = new QTableWidget(0, 4, this);
    m_timeline->setHorizontalHeaderLabels({tr("Seq"), tr("Stage"), tr("Event"), tr("Summary")});
    m_timeline->horizontalHeader()->setStretchLastSection(true);
    m_timeline->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_timeline->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(m_timeline, 2);

    auto *mid = new QHBoxLayout;
    m_decision = new QTextEdit(this);
    m_decision->setReadOnly(true);
    m_decision->setPlaceholderText(tr("Current decision"));
    m_resources = new QTextEdit(this);
    m_resources->setReadOnly(true);
    m_resources->setPlaceholderText(tr("Resources / budgets"));
    m_evidence = new QTextEdit(this);
    m_evidence->setReadOnly(true);
    m_evidence->setPlaceholderText(tr("Evidence"));
    mid->addWidget(m_decision);
    mid->addWidget(m_resources);
    mid->addWidget(m_evidence);
    layout->addLayout(mid, 1);

    auto *row = new QHBoxLayout;
    m_pause = new QPushButton(tr("Pause"), this);
    m_cancel = new QPushButton(tr("Cancel"), this);
    m_resume = new QPushButton(tr("Resume"), this);
    m_approve = new QPushButton(tr("Approve repair"), this);
    m_export = new QPushButton(tr("Export"), this);
    row->addWidget(m_pause);
    row->addWidget(m_cancel);
    row->addWidget(m_resume);
    row->addWidget(m_approve);
    row->addWidget(m_export);
    row->addStretch(1);
    layout->addLayout(row);

    connect(m_pause, &QPushButton::clicked, this, &AgentOpsControlCenterPanel::pauseRequested);
    connect(m_cancel, &QPushButton::clicked, this, &AgentOpsControlCenterPanel::cancelRequested);
    connect(m_resume, &QPushButton::clicked, this, &AgentOpsControlCenterPanel::resumeRequested);
    connect(m_approve, &QPushButton::clicked, this,
            &AgentOpsControlCenterPanel::approveRepairRequested);
    connect(m_export, &QPushButton::clicked, this, &AgentOpsControlCenterPanel::exportRequested);
}

void AgentOpsControlCenterPanel::updateControls(const Json::Value &controls)
{
    auto en = [&](const char *key, bool fallback) {
        return controls.isObject() && controls.isMember(key) ? controls[key].asBool() : fallback;
    };
    m_pause->setEnabled(en("pause", false));
    m_cancel->setEnabled(en("cancel", false));
    m_resume->setEnabled(en("resume", false));
    m_approve->setEnabled(en("approve_repair", false));
    m_export->setEnabled(en("export", true));
}

void AgentOpsControlCenterPanel::setProjection(const sicnu::agent_ops::OpsProjection &projection)
{
    m_header->setText(tr("Session %1 — stage %2 — terminal %3")
                          .arg(QString::fromStdString(projection.sessionId),
                               QString::fromStdString(projection.currentStage),
                               QString::fromStdString(projection.terminalState.empty()
                                                          ? "running"
                                                          : projection.terminalState)));
    m_timeline->setRowCount(0);
    for (const auto &row : projection.timeline)
    {
        const int r = m_timeline->rowCount();
        m_timeline->insertRow(r);
        m_timeline->setItem(r, 0,
                            new QTableWidgetItem(QString::number(row.get("seq", 0).asInt64())));
        m_timeline->setItem(r, 1,
                            new QTableWidgetItem(QString::fromStdString(row.get("stage", "").asString())));
        m_timeline->setItem(r, 2,
                            new QTableWidgetItem(QString::fromStdString(row.get("event", "").asString())));
        m_timeline->setItem(
            r, 3, new QTableWidgetItem(QString::fromStdString(row.get("summary", "").asString())));
    }
    m_decision->setPlainText(jsonToQString(projection.currentDecision));
    m_resources->setPlainText(jsonToQString(projection.resources));
    m_evidence->setPlainText(jsonToQString(projection.evidence));
    updateControls(projection.controls);
}

void AgentOpsControlCenterPanel::setDelivery(const sicnu::agent_ops::FinalDelivery &delivery)
{
    m_evidence->setPlainText(jsonToQString(delivery.toJson()));
}

void AgentOpsControlCenterPanel::setCurrentDecisionJson(const QString &json)
{
    m_decision->setPlainText(json);
}

} // namespace sicnu::app::agent_ops
