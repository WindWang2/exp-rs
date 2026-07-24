#pragma once

#include <memory>

#include "data/data_manager.h"
#include "display/qgis_display_manager.h"

class QgsProject;

namespace sicnu::app {

/**
 * Owns the project-scoped Data and Display authorities.
 *
 * The QGIS project remains the standard presentation container, while this
 * module explicitly owns SICNU Data Asset identity and the main Display View.
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

private:
  ProjectContext();

  data::DataManager m_dataManager;
  display::QgisDisplayManager m_displayManager;
  display::DisplayViewId m_mainViewId;
};

} // namespace sicnu::app
