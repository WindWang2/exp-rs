// rs_result_summary.h — shared structured-result renderer (UX 4.0, ADR 0108+)
//
// One result view for operator/workflow outputs, replacing the per-dialog
// hand-rolled "success" labels. Renders from the operator result JSON:
//   status line · key metrics · warnings · output artifacts (open-on-map)
//   elapsed/cache context · collapsible raw JSON.
// Consumed by RasterProcessingDialogBase, TaskPanelHost and RsJobPanel.
#pragma once

#include <QWidget>
#include <json/json.h>

class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;

class RsResultSummary : public QWidget
{
    Q_OBJECT
  public:
    explicit RsResultSummary( QWidget *parent = nullptr );

    /** Render an operator result JSON object (no-op on null). */
    void setResult( const Json::Value &result );

    /** Optional provenance context shown next to the status line. */
    void setContext( const QString &operatorId, qint64 elapsedMs = -1,
                     bool fromCache = false );

    /** Reset to the empty state. */
    void clear();

    /** True when a result is currently rendered. */
    bool hasResult() const { return m_hasResult; }

  signals:
    /** User asked to add an output artifact to the map. */
    void openPathRequested( const QString &path );

  private:
    void rebuildUi();
    static QString prettyKey( const std::string &key );

    QLabel *m_statusLine = nullptr;
    QLabel *m_metrics = nullptr;
    QLabel *m_warnings = nullptr;
    QListWidget *m_artifacts = nullptr;
    QPushButton *m_rawToggle = nullptr;
    QPlainTextEdit *m_rawJson = nullptr;
    Json::Value m_result;
    QString m_operatorId;
    qint64 m_elapsedMs = -1;
    bool m_fromCache = false;
    bool m_hasResult = false;
};
