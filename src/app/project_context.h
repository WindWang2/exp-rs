#pragma once

#include <memory>

#include "data/data_manager.h"
#include "display/qgis_display_manager.h"

class QgsMapLayer;
class QgsProject;

namespace sicnu::app {

/**
 * Owns the project-scoped Data and Display authorities.
 *
 * The QGIS project remains the standard presentation container, while this
 * module explicitly owns SICNU Data Asset identity and the main Display View.
 *
 * It also adopts legacy layers that enter the QGIS project outside the Data
 * Manager seam (e.g. from independent windows), so they receive a Data Asset
 * identity without each call site needing a ProjectContext reference.
 */
class ProjectContext {
public:
  static data::Result<std::unique_ptr<ProjectContext>>
  create(const display::DisplayViewSpec &mainViewSpec);

  ~ProjectContext();

  ProjectContext(const ProjectContext &) = delete;
  ProjectContext &operator=(const ProjectContext &) = delete;

  data::DataManager &dataManager();
  const data::DataManager &dataManager() const;

  display::QgisDisplayManager &displayManager();
  const display::QgisDisplayManager &displayManager() const;

  display::DisplayViewId mainViewId() const;

  /**
   * Explicitly starts a new project: removes Display Layers, unloads Data
   * Assets, then clears the standard QGIS project state.
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

  /// Adopts a layer that entered the QGIS project outside the Data Manager
  /// seam. Local GDAL rasters and OGR vectors are registered and adopted;
  /// remote and unsupported layers are left as External Display Layers. Layers
  /// that already carry a Data Asset identity are skipped (adoption is
  /// non-recursive).
  void adoptExternalLayer(QgsMapLayer *layer);

  /// Connects the QGIS project's layersAdded signal to adoptExternalLayer.
  void installAdoptionSafetyNet(QgsProject &project);

  data::DataManager m_dataManager;
  display::QgisDisplayManager m_displayManager;
  display::DisplayViewId m_mainViewId;
};

} // namespace sicnu::app
