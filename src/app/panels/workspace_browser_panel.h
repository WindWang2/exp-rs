// workspace_browser_panel.h — Workspace Governance 3.0 browser dock
// (Platform 3.0 Phase S).
//
// High-performance model/view browser over the governance index: paged
// (fetchMore) catalog queries with text/facet filters. Never creates one
// widget per asset — 100k+ assets stay virtualized through the model.
#pragma once

#include "data/governance/governance_types.h"

#include <QAbstractTableModel>
#include <QWidget>

class QLabel;
class QLineEdit;
class QComboBox;
class QTableView;
class QTextEdit;
class QPushButton;

namespace sicnu::workspace
{
class WorkspaceService;
}

namespace sicnu::app
{

/// Paged table model over GovernanceStore::query(). fetchMore pulls the next
/// bounded page; memory stays O(pageSize), not O(assetCount).
class WorkspaceGovernanceModel : public QAbstractTableModel
{
    Q_OBJECT

  public:
    static constexpr int kPageSize = 200;

    explicit WorkspaceGovernanceModel( QObject *parent = nullptr );

    void setWorkspaceService( sicnu::workspace::WorkspaceService *service );
    void applyFilters( const QString &text, const QString &entitySet, const QString &kind,
                       const QString &state, const QString &sensor );

    int rowCount( const QModelIndex &parent = QModelIndex() ) const override;
    int columnCount( const QModelIndex &parent = QModelIndex() ) const override;
    QVariant data( const QModelIndex &index, int role = Qt::DisplayRole ) const override;
    QVariant headerData( int section, Qt::Orientation orientation, int role ) const override;
    bool canFetchMore( const QModelIndex &parent = QModelIndex() ) const override;
    void fetchMore( const QModelIndex &parent = QModelIndex() ) override;

    /// Entity id (assetId/resultId/...) of a row, empty when out of range.
    QString entityId( int row ) const;

  private:
    void resetQuery();

    sicnu::workspace::WorkspaceService *m_service = nullptr;
    sicnu::workspace::WorkspaceQuery m_query;
    QVector<QVariantMap> m_rows;
    qint64 m_total = 0;
};

/// The dock body: filters + paged table + health summary.
class WorkspaceBrowserPanel : public QWidget
{
    Q_OBJECT

  public:
    explicit WorkspaceBrowserPanel( QWidget *parent = nullptr );
    void setWorkspaceService( sicnu::workspace::WorkspaceService *service );

  private slots:
    void refresh();
    void refreshSensorFacet();
    void runHealthCheck();
    void showDetails( const QModelIndex &index );

  private:
    sicnu::workspace::WorkspaceService *m_service = nullptr;
    QLineEdit *m_search = nullptr;
    QComboBox *m_entitySet = nullptr;
    QComboBox *m_kind = nullptr;
    QComboBox *m_state = nullptr;
    QComboBox *m_sensor = nullptr;
    QTableView *m_table = nullptr;
    WorkspaceGovernanceModel *m_model = nullptr;
    QTextEdit *m_details = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_healthButton = nullptr;
};

} // namespace sicnu::app
