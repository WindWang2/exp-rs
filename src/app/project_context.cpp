#include "project_context.h"

#include <utility>

#include <QObject>

#include <qgsmaplayer.h>
#include <qgsproject.h>
#include <qgsproviderregistry.h>

namespace sicnu::app {

namespace {

/// Builds a project-scoped error diagnostic for a failed host operation.
data::Diagnostic projectDiagnostic(const QString &code, const QString &message) {
  return data::Diagnostic{code, message, data::DiagnosticSeverity::Error};
}

/// True when a GDAL/OGR source string refers to a remote or virtual-streamed
/// dataset rather than a local file. This guards the LEGACY QGIS-layer adoption
/// path only (localSourceForLayer): a raw-URI remote raster/vector layer that
/// bypasses the catalog is not adopted as a local asset. It does NOT gate
/// registration — a Remote Map Asset (whose canonicalSource is a URL, registered
/// through the catalog's remote-map providers) is unaffected, because
/// registration never routes through this function.
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

QVector<display::DisplayViewId> ProjectContext::views() const {
  // The engine is the source of truth for the live view record: a view only
  // leaves listViews() once it has been explicitly removed via removeView()
  // (canvas destruction nulls the record's QPointers but does not drop it — a
  // latent engine gap, out of scope for this wave). Filtering against
  // listViews() keeps views() consistent with the engine when a view was
  // removed through the host's removeView() path, and orders the result main
  // first, then secondaries in creation order.
  const QVector<display::DisplayViewId> live = m_displayManager.listViews();
  QVector<display::DisplayViewId> result;
  result.reserve( live.size() );
  // Main first, if still live.
  if ( live.contains( m_mainViewId ) )
    result.append( m_mainViewId );
  // Then secondaries in creation order, only those still live.
  for ( const display::DisplayViewId &id : m_secondaryViews )
  {
    if ( id != m_mainViewId && live.contains( id ) )
      result.append( id );
  }
  return result;
}

data::Result<display::DisplayViewId>
ProjectContext::createSecondaryView( const display::DisplayViewSpec &spec ) {
  const data::Result<display::DisplayViewId> created =
      m_displayManager.createView( spec );
  if ( !created )
    return data::Result<display::DisplayViewId>::failure( created.diagnostics() );

  const display::DisplayViewId id = created.value();
  m_secondaryViews.append( id );
  return data::Result<display::DisplayViewId>::success( id );
}

data::Result<void>
ProjectContext::removeView( display::DisplayViewId viewId ) {
  // The main view is the QGIS-interop view: its layer tree is the project's
  // layerTreeRoot() and ordinary QGIS reads it. It cannot be torn down through
  // the secondary-view path.
  if ( viewId == m_mainViewId )
    return data::Result<void>::failure( projectDiagnostic(
        QStringLiteral( "project.main_view_not_removable" ),
        QStringLiteral( "The main (QGIS-interop) Display View cannot be "
                        "removed through the secondary-view path." ) ) );

  const data::Result<void> removed = m_displayManager.removeView( viewId );
  if ( !removed )
    return removed;

  // Drop our bookkeeping entry (removeAll matches at most one id).
  m_secondaryViews.removeAll( viewId );
  return data::Result<void>::success();
}

data::Result<void> ProjectContext::removeAllDisplayLayers() {
  // Iterate every live view (main + secondaries) so a secondary view's layers
  // and the leases they hold are torn down here — not leaked to the destructor.
  // removeView-then-erase per view would also work, but dropping layers keeps
  // the view records intact (the host may reuse them) and is the minimal fix
  // for the clearProject leak.
  const QVector<display::DisplayViewId> live = m_displayManager.listViews();
  for ( const display::DisplayViewId &viewId : live )
  {
    const std::optional<display::DisplayViewSnapshot> snapshot =
        m_displayManager.view( viewId );
    if ( !snapshot )
      continue;
    for ( const display::DisplayLayerId layerId : snapshot->layerIds() )
    {
      const data::Result<void> removed = m_displayManager.removeLayer( layerId );
      if ( !removed )
        return data::Result<void>::failure( removed.diagnostics() );
    }
  }
  return data::Result<void>::success();
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
  //
  // This reaps only catalog-side temporaries; it does NOT touch display layers
  // or iterate views. That is intentional: the two call sites are the
  // destructor (where the display manager is destroyed in the same step, so its
  // layers/leases are reaped by destruction anyway) and clearProject (which
  // tears down all display layers via removeAllDisplayLayers BEFORE unloading
  // assets). There is no standalone host path that closes a session without
  // also clearing the project, so no secondary-view leak path remains.
  return m_dataManager.reapSessionTemporaries();
}

data::Result<void> ProjectContext::clearProject(QgsProject &project) {
  // Reap scratch outputs first so their files are deleted before the catalog
  // is torn down. ProjectPersistent assets are unloaded below (and re-read by
  // the project on open); TaskTemporary are left for their own task-scope reap.
  closeSession();

  // Tear down display layers across ALL views (main + secondaries). Pre-fix,
  // only the main view was cleared, so a secondary view's layers and the asset
  // leases they held survived clearProject — blocking asset unload and leaking
  // to the destructor. removeAllDisplayLayers closes that leak.
  const data::Result<void> layersCleared = removeAllDisplayLayers();
  if ( !layersCleared )
    return layersCleared;

  const QVector<data::AssetSnapshot> assets = m_dataManager.assets();
  for ( const data::AssetSnapshot &asset : assets ) {
    // A cascade unload (from a strong-dependency edge, e.g. a virtual raster
    // consuming an input) may have removed a later asset already; skip it so
    // the clear is idempotent across the cascade.
    if ( !m_dataManager.asset( asset.id() ) )
      continue;
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
