/***************************************************************************
 * provenance_section.h — Workbench 7.0 provenance inspector section (§B)
 *
 * Shows HOW the selection was produced, projected from the authoritative
 * services only: DataManager (assets, DerivationRecords, lineage edges) and
 * the project WorkspaceService (governance verification/health enrichment).
 * The section owns no provenance store and never re-derives lineage — it is
 * a read-only rendering.
 *
 * Selections resolved (bounded, in this order):
 *   1. Data Manager asset selection (snapshot.selectedAssetIds),
 *   2. governance Results/History selection (snapshot.selectedResultIds →
 *      GovernedAsset → DataManager asset),
 *   3. map layer selection (layer source path → registered asset).
 *
 * Unknown/absent provenance renders truthfully (无派生记录/未知) — nothing is
 * invented. All queries are bounded indexed lookups, so population is
 * synchronous; cancelPending() stays the default no-op.
 ***************************************************************************/
#pragma once

#include "inspector_host.h"

#include <functional>

class QLabel;

namespace sicnu::data
{
class DataManager;
}

namespace sicnu::workspace
{
class WorkspaceService;
}

namespace sicnu::app
{

class ProvenanceSection : public InspectorSection
{
    Q_OBJECT
  public:
    using DataManagerProvider = std::function<sicnu::data::DataManager *()>;
    using WorkspaceServiceProvider = std::function<sicnu::workspace::WorkspaceService *()>;

    ProvenanceSection( DataManagerProvider dataManager,
                       WorkspaceServiceProvider workspace, QWidget *parent = nullptr );

    QString sectionId() const override { return QStringLiteral( "provenance" ); }
    QString title() const override { return tr( "Provenance" ); }
    int order() const override { return 40; }
    bool supports( const SelectionContextSnapshot &snapshot ) const override;
    void populate( const SelectionContextSnapshot &snapshot ) override;

  private:
    QLabel *m_body = nullptr;
    DataManagerProvider m_dataManager;
    WorkspaceServiceProvider m_workspace;
};

} // namespace sicnu::app
