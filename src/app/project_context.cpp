#include "project_context.h"

#include <utility>

#include <QObject>

#include <qgsmaplayer.h>
#include <qgsproject.h>
#include <qgsproviderregistry.h>

namespace sicnu::app {

namespace {

/// True when a GDAL/OGR source string refers to a remote or virtual-streamed
/// dataset rather than a local file. Remote sources are not adopted as local
/// raster/vector assets; remote map providers are deferred (Wave 5).
bool isRemoteSource(const QString &source) {
  return source.startsWith(QStringLiteral("/vsicurl/")) ||
         source.startsWith(QStringLiteral("/vsis3/")) ||
         source.startsWith(QStringLiteral("/vsigs/")) ||
         source.startsWith(QStringLiteral("/vsiaz/")) ||
         source.startsWith(QStringLiteral("http://")) ||
         source.startsWith(QStringLiteral("https://"));
}

/// Builds a non-secret source descriptor for a local GDAL raster or OGR vector
/// layer, or returns nullopt for unsupported or remote sources.
std::optional<data::SourceDescriptor> localSourceForLayer(QgsMapLayer &layer) {
  const QString providerKey = layer.providerType();
  if (providerKey != QStringLiteral("gdal") &&
      providerKey != QStringLiteral("ogr"))
    return std::nullopt;

  const QVariantMap decoded =
      QgsProviderRegistry::instance()->decodeUri(providerKey, layer.source());
  const QString path =
      decoded.value(QStringLiteral("path"), layer.source()).toString();
  if (isRemoteSource(path) || isRemoteSource(layer.source()))
    return std::nullopt;

  data::SourceDescriptor source;
  source.providerKey = providerKey;
  source.canonicalSource = path;
  source.authConfigId = decoded.value(QStringLiteral("authcfg")).toString();
  if (providerKey == QStringLiteral("ogr"))
    source.subdataset = decoded.value(QStringLiteral("layerName")).toString();
  return source;
}

} // namespace

ProjectContext::ProjectContext() : m_displayManager(&m_dataManager) {}

ProjectContext::~ProjectContext() {
  // App exit / context teardown may not have run clearProject (e.g. the user
  // just quits). Reap session temporaries so scratch outputs do not leak onto
  // disk. Leased assets are skipped - they cannot be safely deleted out from
  // under a holder during teardown - and their ids are logged here so they are
  // not silently dropped. (emit from the destructor is safe because the
  // DisplayManager is a member destroyed in the same step as the DataManager.)
  const data::TemporaryReapResult reaped = closeSession();
  for ( const data::AssetId &id : reaped.skippedLeased )
  {
    qWarning( "ProjectContext: SessionTemporary asset %s still held a lease at "
              "teardown and was not reaped",
              qPrintable( id.toString() ) );
  }
}

data::Result<std::unique_ptr<ProjectContext>>
ProjectContext::create(const display::DisplayViewSpec &mainViewSpec) {
  auto context = std::unique_ptr<ProjectContext>(new ProjectContext);
  const data::Result<display::DisplayViewId> createdView =
      context->m_displayManager.createView(mainViewSpec);
  if (!createdView)
    return data::Result<std::unique_ptr<ProjectContext>>::failure(
        createdView.diagnostics());

  context->m_mainViewId = createdView.value();
  context->installAdoptionSafetyNet(*QgsProject::instance());
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

void ProjectContext::installAdoptionSafetyNet(QgsProject &project) {
  // The Data Manager is a QObject owned by this context; using it as the
  // connection context keeps the slot alive exactly as long as the context.
  QObject::connect(&project, &QgsProject::layersAdded, &m_dataManager,
                   [this](const QList<QgsMapLayer *> &layers) {
                     for (QgsMapLayer *layer : layers)
                       adoptExternalLayer(layer);
                   });
}

void ProjectContext::adoptExternalLayer(QgsMapLayer *layer) {
  if (!layer)
    return;

  // Non-recursive: layers the Display Manager materialized already carry a
  // Data Asset identity and are adopted through the Data Manager seam, so a
  // layersAdded notification for them must not register a second asset.
  if (!layer->customProperty(QStringLiteral("sicnu/assetId"))
           .toString()
           .isEmpty())
    return;

  const std::optional<data::SourceDescriptor> source =
      localSourceForLayer(*layer);
  if (!source)
    return; // Remote or unsupported provider: stays an External Display Layer.

  const data::RegisterResult registered =
      m_dataManager.registerSource(data::RegisterRequest{*source});
  if (registered.assetId.isNull())
    return;

  // Adopt the live QGIS layer as a Display Layer. Adoption is idempotent for
  // layers the Display Manager already owns.
  (void)m_displayManager.adoptLayer(m_mainViewId, registered.assetId, layer);
}

data::TemporaryReapResult ProjectContext::closeSession() {
  // Reap idle SessionTemporary assets (catalog removal + DeletableSource file
  // deletion). Leased ones are skipped and reported; ProjectPersistent and
  // TaskTemporary are untouched. This runs on explicit session close and on
  // destruction, so scratch outputs never leak past the session.
  return m_dataManager.reapSessionTemporaries();
}

data::Result<void> ProjectContext::clearProject(QgsProject &project) {
  // Reap scratch outputs first so their files are deleted before the catalog
  // is torn down. ProjectPersistent assets are unloaded below (and re-read by
  // the project on open); TaskTemporary are left for their own task-scope reap.
  closeSession();

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

  // Remove collection nodes (children are already unloaded above; non-cascade
  // so the orphaned nodes are simply dropped).
  const QVector<data::CollectionId> collections = m_dataManager.collections();
  for (const data::CollectionId &cid : collections) {
    const data::Result<void> removed = m_dataManager.unloadCollection(cid, false);
    if (!removed)
      return data::Result<void>::failure(removed.diagnostics());
  }

  project.clear();
  return data::Result<void>::success();
}

} // namespace sicnu::app
