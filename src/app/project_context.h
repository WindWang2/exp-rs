#pragma once

#include <memory>

#include <QVector>

#include "data/data_manager.h"
#include "display/network_probe.h"
#include "display/qgis_display_manager.h"

class QgsMapLayer;
class QgsProject;

namespace sicnu::app {

/**
 * Owns the project-scoped Data and Display authorities.
 *
 * The QGIS project remains the standard presentation container, while this
 * module explicitly owns SICNU Data Asset identity and a set of Display Views.
 * The main view is the QGIS-interop view (its layer tree is the project's
 * layerTreeRoot()); secondary views are engine-only, managed through the
 * createSecondaryView/removeView pair below.
 *
 * It also adopts legacy layers that enter the QGIS project outside the Data
 * Manager seam (e.g. from independent windows), so they receive a Data Asset
 * identity without each call site needing a ProjectContext reference.
 */
class ProjectContext {
public:
  static data::Result<std::unique_ptr<ProjectContext>>
  create(const display::DisplayViewSpec &mainViewSpec);

  /// Test seam: like create(), but injects a NetworkProbe into the DataManager's
  /// remote-map providers (the production create() builds its own probe-backed
  /// fetcher). The caller owns `probe` (and any fetcher backing it); it must
  /// outlive the returned context. Used by #66's end-to-end test to assert a
  /// remote-map asset registers Ready through the host wiring.
  static data::Result<std::unique_ptr<ProjectContext>>
  createForTesting(const display::DisplayViewSpec &mainViewSpec,
                   const data::internal::NetworkProbe *probe);

  /// Headless seam for CLI/tests (ADR 0023, TICKET-14): constructs the context
  /// (DataManager + QgisDisplayManager) without creating any Display View, so
  /// no QgsMapCanvas/QWidget is required. mainViewId() stays null on the
  /// returned context and the adoption safety net is not installed (adoption
  /// targets the main view); callers must not assume a main view exists.
  static data::Result<std::unique_ptr<ProjectContext>> createHeadless();

  ~ProjectContext();

  ProjectContext(const ProjectContext &) = delete;
  ProjectContext &operator=(const ProjectContext &) = delete;

  data::DataManager &dataManager();
  const data::DataManager &dataManager() const;

  display::QgisDisplayManager &displayManager();
  const display::QgisDisplayManager &displayManager() const;

  /// The main (QGIS-interop) view id. Null when the context was created
  /// through createHeadless() — headless contexts have no display view.
  display::DisplayViewId mainViewId() const;

  /// The live Display View ids: the main view first, then secondaries in
  /// creation order. Re-query for freshness (the engine is the source of
  /// truth; this is a host-side snapshot).
  QVector<display::DisplayViewId> views() const;

  /// Creates a secondary (engine-only) Display View backed by the host-supplied
  /// {canvas, layerTree, layerStore}. Does NOT auto-add any layers — the host
  /// decides what to show. Returns the new view id (distinct from the main
  /// view).
  data::Result<display::DisplayViewId>
  createSecondaryView(const display::DisplayViewSpec &spec);

  /// Removes a secondary view: drops every Display Layer in it (releasing each
  /// lease) and drops the view record. Refuses the main view (the QGIS-interop
  /// view is owned by the project and cannot be destroyed out from under it);
  /// a refusal is reported as a diagnostic, not a crash.
  data::Result<void> removeView(display::DisplayViewId viewId);

  /**
   * Explicitly starts a new project: removes Display Layers across ALL views,
   * unloads Data Assets, then clears the standard QGIS project state.
   */
  data::Result<void> clearProject(QgsProject &project);

  /**
   * Closes the session: reaps every idle SessionTemporary asset (removing it
   * from the catalog and deleting its DeletableSource file) and reports any
   * SessionTemporary asset still holding a lease that could not be reaped.
   * ProjectPersistent and TaskTemporary assets are left untouched. Called by
   * the host on session close; also run by the destructor so app exit reaps
   * scratch outputs even when clearProject was not invoked.
   */
  data::TemporaryReapResult closeSession();

private:
  ProjectContext();

  /// Test seam: constructs with an injected NetworkProbe (the host's
  /// production path uses the default ctor, which builds its own probe-backed
  /// fetcher). The probe (and any owning fetcher) must outlive the DataManager.
  explicit ProjectContext( const data::internal::NetworkProbe *probe );

  /// Removes every Display Layer in every live view. Used by clearProject so a
  /// secondary view's layers/leases never survive a project clear (the engine's
  /// removeLayer releases each lease; a view whose asset is also shown elsewhere
  /// keeps the asset loaded via that other lease).
  data::Result<void> removeAllDisplayLayers();

  /// Adopts a layer that entered the QGIS project outside the Data Manager
  /// seam. Local GDAL rasters and OGR vectors are registered and adopted;
  /// remote and unsupported layers are left as External Display Layers. Layers
  /// that already carry a Data Asset identity are skipped (adoption is
  /// non-recursive).
  void adoptExternalLayer(QgsMapLayer *layer);

  /// Connects the QGIS project's layersAdded signal to adoptExternalLayer.
  void installAdoptionSafetyNet(QgsProject &project);

  // Members initialize in declaration order. The fetcher + probe are owned
  // here and must outlive the DataManager (which holds a non-owning pointer to
  // the probe), so they are declared FIRST. The test-seam ctor injects a probe
  // without an owning fetcher (the test owns both).
  std::unique_ptr<display::CapabilitiesFetcher> m_fetcher;
  std::unique_ptr<display::QgisNetworkProbe> m_probe;
  data::DataManager m_dataManager;
  display::QgisDisplayManager m_displayManager;
  display::DisplayViewId m_mainViewId;
  /// Secondary view ids in creation order. The main view is tracked separately
  /// (m_mainViewId) because of its QGIS-interop role and removeView refusal.
  QVector<display::DisplayViewId> m_secondaryViews;
};

} // namespace sicnu::app
