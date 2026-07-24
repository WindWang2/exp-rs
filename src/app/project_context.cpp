#include "project_context.h"

#include <utility>

#include <qgsproject.h>

namespace sicnu::app {

ProjectContext::ProjectContext() : m_displayManager(&m_dataManager) {}

ProjectContext::~ProjectContext() = default;

data::Result<std::unique_ptr<ProjectContext>>
ProjectContext::create(const display::DisplayViewSpec &mainViewSpec) {
  auto context = std::unique_ptr<ProjectContext>(new ProjectContext);
  const data::Result<display::DisplayViewId> createdView =
      context->m_displayManager.createView(mainViewSpec);
  if (!createdView)
    return data::Result<std::unique_ptr<ProjectContext>>::failure(
        createdView.diagnostics());

  context->m_mainViewId = createdView.value();
  return data::Result<std::unique_ptr<ProjectContext>>::success(
      std::move(context));
}

data::DataManager &ProjectContext::dataManager() { return m_dataManager; }

const data::DataManager &ProjectContext::dataManager() const {
  return m_dataManager;
}

display::QgisDisplayManager &ProjectContext::displayManager() {
  return m_displayManager;
}

const display::QgisDisplayManager &ProjectContext::displayManager() const {
  return m_displayManager;
}

display::DisplayViewId ProjectContext::mainViewId() const {
  return m_mainViewId;
}

data::Result<void> ProjectContext::clearProject(QgsProject &project) {
  const std::optional<display::DisplayViewSnapshot> mainView =
      m_displayManager.view(m_mainViewId);
  if (mainView) {
    const QVector<display::DisplayLayerId> displayLayers = mainView->layerIds();
    for (const display::DisplayLayerId layerId : displayLayers) {
      const data::Result<void> removed = m_displayManager.removeLayer(layerId);
      if (!removed)
        return data::Result<void>::failure(removed.diagnostics());
    }
  }

  const QVector<data::AssetSnapshot> assets = m_dataManager.assets();
  for (const data::AssetSnapshot &asset : assets) {
    const data::UnloadPlan plan =
        m_dataManager.planUnload(asset.id()).confirmedCascade();
    const data::Result<void> unloaded = m_dataManager.unload(plan);
    if (!unloaded)
      return data::Result<void>::failure(unloaded.diagnostics());
  }

  project.clear();
  return data::Result<void>::success();
}

} // namespace sicnu::app
