#include "data_project_serializer.h"

#include <optional>
#include <utility>

#include <QDomDocument>
#include <QJsonDocument>
#include <QMap>
#include <QSet>
#include <QVariantMap>

#include <providers/qgsproviderregistry.h>
#include <qgslayertree.h>
#include <qgsmaplayer.h>
#include <qgsproject.h>

#include "project_context.h"

#include "data/governance/governance_types.h"

namespace sicnu::app {

namespace {

constexpr auto extensionName = "sicnuDataManager";
constexpr auto extensionVersion = "1";

data::Diagnostic projectDiagnostic(const QString &code,
                                   const QString &message) {
  return data::Diagnostic{code, message, data::DiagnosticSeverity::Error};
}

bool isSensitiveOption(const QString &key) {
  const QString normalized = key.toLower();
  return normalized.contains(QStringLiteral("password")) ||
         normalized.contains(QStringLiteral("passwd")) ||
         normalized.contains(QStringLiteral("token")) ||
         normalized.contains(QStringLiteral("secret")) ||
         normalized.contains(QStringLiteral("credential")) ||
         normalized.contains(QStringLiteral("apikey")) ||
         normalized.contains(QStringLiteral("api_key"));
}

QString persistenceToString(data::PersistencePolicy policy) {
  switch (policy) {
  case data::PersistencePolicy::ProjectPersistent:
    return QStringLiteral("persistent");
  case data::PersistencePolicy::SessionTemporary:
    return QStringLiteral("session");
  case data::PersistencePolicy::TaskTemporary:
    return QStringLiteral("task");
  }
  return QStringLiteral("persistent");
}

std::optional<data::PersistencePolicy> persistenceFromString(const QString &text) {
  if (text == QStringLiteral("persistent"))
    return data::PersistencePolicy::ProjectPersistent;
  if (text == QStringLiteral("session"))
    return data::PersistencePolicy::SessionTemporary;
  if (text == QStringLiteral("task"))
    return data::PersistencePolicy::TaskTemporary;
  return std::nullopt;
}

std::optional<data::SourceDescriptor> sourceForLayer(QgsMapLayer &layer) {
  const QString providerKey = layer.providerType();
  if (providerKey != QStringLiteral("gdal") &&
      providerKey != QStringLiteral("ogr"))
    return std::nullopt;

  const QVariantMap decoded =
      QgsProviderRegistry::instance()->decodeUri(providerKey, layer.source());
  data::SourceDescriptor source;
  source.providerKey = providerKey;
  source.canonicalSource =
      decoded.value(QStringLiteral("path"), layer.source()).toString();
  source.authConfigId = decoded.value(QStringLiteral("authcfg")).toString();
  if (providerKey == QStringLiteral("ogr"))
    source.subdataset = decoded.value(QStringLiteral("layerName")).toString();
  return source;
}

QList<QgsMapLayer *> orderedProjectLayers(QgsProject &project) {
  QList<QgsMapLayer *> ordered = project.layerTreeRoot()->layerOrder();
  QSet<QString> seen;
  for (QgsMapLayer *layer : std::as_const(ordered)) {
    if (layer)
      seen.insert(layer->id());
  }
  for (QgsMapLayer *layer : project.mapLayers()) {
    if (layer && !seen.contains(layer->id()))
      ordered.append(layer);
  }
  return ordered;
}

} // namespace

data::Result<void>
DataProjectSerializer::write(QDomDocument &document,
                             const ProjectContext &context) const {
  QDomElement root = document.documentElement();
  if (root.isNull()) {
    return data::Result<void>::failure(projectDiagnostic(
        QStringLiteral("project.missing_root"),
        QStringLiteral("The QGIS project document has no root element")));
  }

  for (QDomElement existing =
           root.firstChildElement(QString::fromLatin1(extensionName));
       !existing.isNull();) {
    const QDomElement next =
        existing.nextSiblingElement(QString::fromLatin1(extensionName));
    root.removeChild(existing);
    existing = next;
  }

  QDomElement extension =
      document.createElement(QString::fromLatin1(extensionName));
  // Format v3 (Workspace Governance 3.0): the legacy v1 blocks below stay
  // byte-compatible; the governed workspace state is ADDED as an extra block.
  // A store-less context keeps writing plain v1 (backward compatible always).
  // Downgrade guard (review P0): with the store unavailable but a governed
  // document cached from a previous read, re-persist THAT instead of silently
  // dropping all governed state by rewriting a v1 file.
  const bool writeV3 = context.workspaceService().isStoreOpen()
                       || context.workspaceService().hasCachedProjectJson();
  extension.setAttribute(QStringLiteral("version"),
                         writeV3 ? QStringLiteral("3")
                                 : QString::fromLatin1(extensionVersion));
  QDomElement assetsElement = document.createElement(QStringLiteral("assets"));

  for (const data::AssetSnapshot &asset : context.dataManager().assets()) {
    if (asset.persistence() != data::PersistencePolicy::ProjectPersistent)
      continue;
    // Virtual rasters are persisted as identity-bearing recipes in the
    // <virtualRasters> block below, NOT as <asset> rows: their <source>
    // canonical path is a session-local scratch .vrt that must never be
    // persisted (the artifact is regenerated on restore from the recipe).
    if (asset.kind() == data::AssetKind::VirtualRaster)
      continue;

    QDomElement assetElement = document.createElement(QStringLiteral("asset"));
    assetElement.setAttribute(QStringLiteral("id"), asset.id().toString());
    assetElement.setAttribute(QStringLiteral("revision"),
                              QString::number(asset.revision().value()));
    if (const std::optional<QDateTime> &acqTime = asset.acquisitionTime())
      assetElement.setAttribute(QStringLiteral("acquisitionTime"),
                                acqTime->toString(Qt::ISODateWithMs));

    const data::SourceDescriptor &source = asset.source();
    QDomElement sourceElement =
        document.createElement(QStringLiteral("source"));
    sourceElement.setAttribute(QStringLiteral("provider"), source.providerKey);
    sourceElement.setAttribute(QStringLiteral("canonical"),
                               source.canonicalSource);
    if (!source.subdataset.isEmpty())
      sourceElement.setAttribute(QStringLiteral("subdataset"),
                                 source.subdataset);
    if (!source.authConfigId.isEmpty())
      sourceElement.setAttribute(QStringLiteral("authConfigId"),
                                 source.authConfigId);

    for (auto option = source.dataOptions.cbegin();
         option != source.dataOptions.cend(); ++option) {
      if (isSensitiveOption(option.key()))
        continue;
      QDomElement optionElement =
          document.createElement(QStringLiteral("option"));
      optionElement.setAttribute(QStringLiteral("key"), option.key());
      optionElement.setAttribute(QStringLiteral("value"), option.value());
      sourceElement.appendChild(optionElement);
    }

    assetElement.appendChild(sourceElement);

    // Persist the Derivation Record (provenance) when the asset carries one,
    // so promoted algorithm outputs keep their provenance across save/reopen.
    const std::optional<data::DerivationRecord> provenance =
        context.dataManager().provenance(asset.id());
    if (provenance) {
      QDomElement derivationElement =
          document.createElement(QStringLiteral("derivation"));
      derivationElement.appendChild(
          document.createTextNode(QString::fromUtf8(
              QJsonDocument(provenance->toJson()).toJson(QJsonDocument::Compact))));
      assetElement.appendChild(derivationElement);
    }

    assetsElement.appendChild(assetElement);
  }

  extension.appendChild(assetsElement);

  // Persist Data Collections so their grouping and product metadata survive
  // save/reopen. Children carry their parent collection id on their own record;
  // this element records the collection node + its ordered child list.
  QDomElement collectionsElement =
      document.createElement(QStringLiteral("collections"));
  for (const data::CollectionId &cid : context.dataManager().collections()) {
    const auto snapshot = context.dataManager().collection(cid);
    if (!snapshot)
      continue;
    QDomElement collectionElement =
        document.createElement(QStringLiteral("collection"));
    collectionElement.setAttribute(QStringLiteral("id"),
                                   snapshot->id.toString());
    collectionElement.setAttribute(QStringLiteral("name"),
                                   snapshot->displayName);
    collectionElement.setAttribute(QStringLiteral("platform"),
                                   snapshot->metadata.platform);
    collectionElement.setAttribute(QStringLiteral("sensor"),
                                   snapshot->metadata.sensor);
    collectionElement.setAttribute(QStringLiteral("productLevel"),
                                   snapshot->metadata.productLevel);
    collectionElement.setAttribute(QStringLiteral("acquisitionDate"),
                                   snapshot->metadata.acquisitionDate);
    collectionElement.setAttribute(QStringLiteral("processingLevel"),
                                   snapshot->metadata.processingLevel);
    for (auto it = snapshot->metadata.attributes.cbegin();
         it != snapshot->metadata.attributes.cend(); ++it) {
      QDomElement attrElement =
          document.createElement(QStringLiteral("attribute"));
      attrElement.setAttribute(QStringLiteral("key"), it.key());
      attrElement.setAttribute(QStringLiteral("value"), it.value());
      collectionElement.appendChild(attrElement);
    }
    for (const data::AssetId &childId : snapshot->childAssetIds) {
      QDomElement childElement =
          document.createElement(QStringLiteral("child"));
      childElement.setAttribute(QStringLiteral("assetId"),
                                childId.toString());
      collectionElement.appendChild(childElement);
    }
    collectionsElement.appendChild(collectionElement);
  }
  extension.appendChild(collectionsElement);

  // Persist Virtual Rasters as identity-bearing recipes (NOT their scratch .vrt
  // paths, which are regenerated on restore). Each entry carries the saved
  // AssetId/revision/persistence so the restored asset preserves identity, and
  // the recipe JSON as a single text node (mirrors the <derivation> payload).
  QDomElement virtualsElement =
      document.createElement(QStringLiteral("virtualRasters"));
  const data::AssetQuery virtualQuery{data::AssetKind::VirtualRaster};
  for (const data::AssetSnapshot &virtualAsset :
       context.dataManager().assets(virtualQuery)) {
    if (virtualAsset.persistence() != data::PersistencePolicy::ProjectPersistent)
      continue;
    const std::optional<data::VirtualRasterRecipe> recipe =
        context.dataManager().virtualRasterRecipe(virtualAsset.id());
    if (!recipe)
      continue;
    QDomElement virtualElement =
        document.createElement(QStringLiteral("virtualRaster"));
    virtualElement.setAttribute(QStringLiteral("id"),
                                virtualAsset.id().toString());
    virtualElement.setAttribute(QStringLiteral("revision"),
                                QString::number(virtualAsset.revision().value()));
    virtualElement.setAttribute(
        QStringLiteral("persistence"),
        persistenceToString(virtualAsset.persistence()));
    QDomElement recipeElement =
        document.createElement(QStringLiteral("recipe"));
    recipeElement.appendChild(document.createTextNode(QString::fromUtf8(
        QJsonDocument(recipe->toJson()).toJson(QJsonDocument::Compact))));
    virtualElement.appendChild(recipeElement);
    virtualsElement.appendChild(virtualElement);
  }
  extension.appendChild(virtualsElement);

  // Persist Temporal Collections: identity (id + revision) + the canonical
  // descriptor document as a single text node (mirrors the <recipe>/<derivation>
  // payloads). Scene → Data Asset bindings live INSIDE the descriptor
  // (per-scene assetId/revision); the scene assets themselves are persisted by
  // the <assets> block above when they are project-persistent.
  QDomElement temporalElement =
      document.createElement(QStringLiteral("temporalCollections"));
  for (const data::TemporalCollectionRecord &record :
       context.dataManager().temporalCollections()) {
    QDomElement recordElement =
        document.createElement(QStringLiteral("temporalCollection"));
    recordElement.setAttribute(QStringLiteral("id"), record.id.toString());
    recordElement.setAttribute(QStringLiteral("revision"),
                               QString::number(record.revision));
    recordElement.setAttribute(QStringLiteral("name"), record.displayName);
    QDomElement descriptorElement =
        document.createElement(QStringLiteral("descriptor"));
    descriptorElement.appendChild(document.createTextNode(record.descriptor));
    recordElement.appendChild(descriptorElement);
    temporalElement.appendChild(recordElement);
  }
  extension.appendChild(temporalElement);

  // Governed workspace state (v3): datasets/results/runs/experiments/smart
  // collections/exports/mappings. One JSON document, one text node — mirrors
  // the <derivation>/<recipe>/<descriptor> payload pattern above.
  if (writeV3) {
    const QJsonObject workspaceDocument =
        context.workspaceService().isStoreOpen()
            ? context.workspaceService().toProjectJson()
            : context.workspaceService().cachedProjectJson();
    QDomElement workspaceElement =
        document.createElement(QStringLiteral("workspace"));
    workspaceElement.setAttribute(QStringLiteral("schemaVersion"),
                                  QStringLiteral("1"));
    QDomElement documentElement =
        document.createElement(QStringLiteral("document"));
    documentElement.appendChild(document.createTextNode(QString::fromUtf8(
        QJsonDocument(workspaceDocument).toJson(QJsonDocument::Compact))));
    workspaceElement.appendChild(documentElement);
    extension.appendChild(workspaceElement);
  }

  root.appendChild(extension);
  return data::Result<void>::success();
}

data::Result<void> DataProjectSerializer::read(const QDomDocument &document,
                                               QgsProject &project,
                                               ProjectContext &context) const {
  const QDomElement extension = document.documentElement().firstChildElement(
      QString::fromLatin1(extensionName));
  QVector<data::Diagnostic> diagnostics;
  bool failed = false;
  bool isV1 = false;
  bool isV3 = false;

  if (!extension.isNull()) {
    // Version dispatch: "1" (legacy) and "3" (Workspace Governance) are
    // accepted; "3" is a strict superset of "1". Anything else refuses the
    // custom-data restore (QGIS layers still load) — same failure contract
    // as before, now with two supported versions.
    const QString versionText = extension.attribute(QStringLiteral("version"));
    isV1 = versionText == QString::fromLatin1(extensionVersion);
    isV3 = versionText == QStringLiteral("3");
    if (!isV1 && !isV3) {
      return data::Result<void>::failure(projectDiagnostic(
          QStringLiteral("project.unsupported_data_version"),
          QStringLiteral("The SICNU Data extension version is unsupported")));
    }

    const QDomElement assets =
        extension.firstChildElement(QStringLiteral("assets"));
    for (QDomElement asset = assets.firstChildElement(QStringLiteral("asset"));
         !asset.isNull();
         asset = asset.nextSiblingElement(QStringLiteral("asset"))) {
      const std::optional<data::AssetId> assetId =
          data::AssetId::fromString(asset.attribute(QStringLiteral("id")));
      bool revisionOk = false;
      const quint64 revisionValue =
          asset.attribute(QStringLiteral("revision"), QStringLiteral("1"))
              .toULongLong(&revisionOk);
      const QDomElement sourceElement =
          asset.firstChildElement(QStringLiteral("source"));
      if (!assetId || sourceElement.isNull() || !revisionOk) {
        diagnostics.append(projectDiagnostic(
            QStringLiteral("project.invalid_asset"),
            QStringLiteral("A persisted Data Asset description is invalid")));
        failed = true;
        continue;
      }

      data::SourceDescriptor source;
      source.providerKey = sourceElement.attribute(QStringLiteral("provider"));
      source.canonicalSource =
          sourceElement.attribute(QStringLiteral("canonical"));
      source.subdataset = sourceElement.attribute(QStringLiteral("subdataset"));
      source.authConfigId =
          sourceElement.attribute(QStringLiteral("authConfigId"));
      for (QDomElement option =
               sourceElement.firstChildElement(QStringLiteral("option"));
           !option.isNull();
           option = option.nextSiblingElement(QStringLiteral("option"))) {
        const QString key = option.attribute(QStringLiteral("key"));
        if (!key.isEmpty() && !isSensitiveOption(key))
          source.dataOptions.insert(key,
                                    option.attribute(QStringLiteral("value")));
      }

      // Restore the optional acquisition time when it was persisted. A missing
      // attribute yields an empty optional (legacy projects, or assets whose
      // source never carried one). A present-but-unparseable value is a corrupt
      // project: surface a Warning and proceed with an empty optional rather
      // than failing the whole read.
      std::optional<QDateTime> acquisitionTime;
      const QString acqText = asset.attribute(QStringLiteral("acquisitionTime"));
      if (!acqText.isEmpty()) {
        const QDateTime parsed = QDateTime::fromString(acqText, Qt::ISODateWithMs);
        if (parsed.isValid())
          acquisitionTime = parsed;
        else
          diagnostics.append(data::Diagnostic{
              QStringLiteral("project.invalid_acquisition_time"),
              QStringLiteral("A persisted acquisition time could not be parsed "
                             "and was ignored"),
              data::DiagnosticSeverity::Warning});
      }

      const data::Result<data::AssetId> restored =
          context.dataManager().restoreSource(data::RestoreRequest{
              *assetId, data::AssetRevision::fromValue(revisionValue), source,
              data::PersistencePolicy::ProjectPersistent, acquisitionTime});
      diagnostics += restored.diagnostics();
      if (!restored)
        failed = true;

      // Restore the Derivation Record (provenance) if it was persisted.
      const QDomElement derivationElement =
          asset.firstChildElement(QStringLiteral("derivation"));
      if (!derivationElement.isNull() && restored) {
        const QJsonDocument parsed = QJsonDocument::fromJson(
            derivationElement.text().toUtf8());
        const data::Result<data::DerivationRecord> derivation =
            data::DerivationRecord::fromJson(parsed.object());
        if (derivation) {
          context.dataManager().attachDerivationRecord(*assetId,
                                                       derivation.value());
        } else {
          diagnostics += derivation.diagnostics();
        }
      }
    }
  }

  // Restore Virtual Rasters. Inputs were already restored as assets above, so
  // a recipe referencing a missing input can be detected and recorded as a
  // non-Ready asset with a Warning - the asset is NOT dropped (spec: missing
  // dependencies remain in the project). The recipe - not any scratch .vrt
  // path - is the identity, so restore rebuilds from the recipe.
  const QDomElement virtuals =
      extension.firstChildElement(QStringLiteral("virtualRasters"));
  for (QDomElement virt =
           virtuals.firstChildElement(QStringLiteral("virtualRaster"));
       !virt.isNull();
       virt = virt.nextSiblingElement(QStringLiteral("virtualRaster"))) {
    const std::optional<data::AssetId> virtualId =
        data::AssetId::fromString(virt.attribute(QStringLiteral("id")));
    const QDomElement recipeElement =
        virt.firstChildElement(QStringLiteral("recipe"));
    if (!virtualId || recipeElement.isNull()) {
      diagnostics.append(projectDiagnostic(
          QStringLiteral("project.invalid_virtual_raster"),
          QStringLiteral("A persisted virtual raster description is invalid")));
      failed = true;
      continue;
    }

    const QJsonDocument parsed =
        QJsonDocument::fromJson(recipeElement.text().toUtf8());
    const data::Result<data::VirtualRasterRecipe> recipe =
        data::VirtualRasterRecipe::fromJson(parsed.object());
    if (!recipe) {
      diagnostics.append(projectDiagnostic(
          QStringLiteral("project.invalid_virtual_raster"),
          QStringLiteral("A persisted virtual raster recipe could not be parsed")));
      failed = true;
      continue;
    }

    // Mirror the <asset> revision validation: a corrupt/missing revision is a
    // corrupt description, not a silently-coerced revision 0.
    bool revisionOk = false;
    const quint64 revisionValue =
        virt.attribute(QStringLiteral("revision"), QStringLiteral("1"))
            .toULongLong(&revisionOk);
    if (!revisionOk) {
      diagnostics.append(projectDiagnostic(
          QStringLiteral("project.invalid_virtual_raster"),
          QStringLiteral("A persisted virtual raster description is invalid")));
      failed = true;
      continue;
    }
    const data::AssetRevision revision =
        data::AssetRevision::fromValue(revisionValue);
    const data::PersistencePolicy persistence =
        persistenceFromString(virt.attribute(QStringLiteral("persistence")))
            .value_or(data::PersistencePolicy::ProjectPersistent);

    const data::Result<data::AssetId> restored =
        context.dataManager().restoreVirtualRaster(
            data::RestoreVirtualRasterRequest{*virtualId, recipe.value(),
                                              revision, persistence});
    diagnostics += restored.diagnostics();
    if (!restored)
      failed = true;
  }

  // Restore Data Collections. Children are already restored as assets above;
  // this recreates the collection nodes and re-binds the children.
  const QDomElement collections =
      extension.firstChildElement(QStringLiteral("collections"));
  for (QDomElement coll =
           collections.firstChildElement(QStringLiteral("collection"));
       !coll.isNull();
       coll = coll.nextSiblingElement(QStringLiteral("collection"))) {
    const std::optional<data::CollectionId> collectionId =
        data::CollectionId::fromString(coll.attribute(QStringLiteral("id")));
    if (!collectionId) {
      diagnostics.append(projectDiagnostic(
          QStringLiteral("project.invalid_collection"),
          QStringLiteral("A persisted collection description is invalid")));
      failed = true;
      continue;
    }

    data::ProductMetadata metadata;
    metadata.platform = coll.attribute(QStringLiteral("platform"));
    metadata.sensor = coll.attribute(QStringLiteral("sensor"));
    metadata.productLevel = coll.attribute(QStringLiteral("productLevel"));
    metadata.acquisitionDate = coll.attribute(QStringLiteral("acquisitionDate"));
    metadata.processingLevel = coll.attribute(QStringLiteral("processingLevel"));
    for (QDomElement attr = coll.firstChildElement(QStringLiteral("attribute"));
         !attr.isNull();
         attr = attr.nextSiblingElement(QStringLiteral("attribute"))) {
      const QString key = attr.attribute(QStringLiteral("key"));
      if (!key.isEmpty())
        metadata.attributes.insert(key, attr.attribute(QStringLiteral("value")));
    }

    const data::CollectionCreateResult restored = context.dataManager()
        .restoreCollection(*collectionId,
                           {coll.attribute(QStringLiteral("name")), metadata});
    diagnostics += restored.diagnostics;
    if (restored.collectionId.isNull())
      failed = true;

    // Re-bind the persisted children (in order). Duplicated persisted entries
    // (#703) are collapsed on read-back: the manager refuses to double-add, and
    // here the repeat entry is reported and skipped instead of silently
    // re-binding (and the catalog never holds the duplicate).
    QSet<QString> boundChildren;
    for (QDomElement child = coll.firstChildElement(QStringLiteral("child"));
         !child.isNull();
         child = child.nextSiblingElement(QStringLiteral("child"))) {
      const std::optional<data::AssetId> childId =
          data::AssetId::fromString(child.attribute(QStringLiteral("assetId")));
      if (!childId)
        continue;
      const QString childKey = childId->toString();
      if (boundChildren.contains(childKey)) {
        diagnostics.append(data::Diagnostic{
            QStringLiteral("project.duplicate_collection_child"),
            QStringLiteral("A persisted collection lists child %1 more than "
                           "once; the duplicate entry was skipped")
                .arg(childKey),
            data::DiagnosticSeverity::Warning});
        continue;
      }
      boundChildren.insert(childKey);
      const data::Result<void> added = context.dataManager().addChildToCollection(
          *collectionId, *childId);
      if (!added) {
        diagnostics += added.diagnostics();
        failed = true;
      }
    }
  }

  // Restore Temporal Collections (additive block: absent in projects saved
  // before the temporal workspace existed — the loop simply never runs).
  // Scene → asset bindings are re-validated lazily by the temporal layer
  // (bindCollectionAssets) after the scene assets were restored above.
  const QDomElement temporals =
      extension.firstChildElement(QStringLiteral("temporalCollections"));
  for (QDomElement tempRec =
           temporals.firstChildElement(QStringLiteral("temporalCollection"));
       !tempRec.isNull();
       tempRec = tempRec.nextSiblingElement(QStringLiteral("temporalCollection"))) {
    const std::optional<data::CollectionId> recordId =
        data::CollectionId::fromString(tempRec.attribute(QStringLiteral("id")));
    const QDomElement descriptorElement =
        tempRec.firstChildElement(QStringLiteral("descriptor"));
    if (!recordId || descriptorElement.isNull() ||
        descriptorElement.text().trimmed().isEmpty()) {
      diagnostics.append(projectDiagnostic(
          QStringLiteral("project.invalid_temporal_collection"),
          QStringLiteral("A persisted temporal collection description is invalid")));
      failed = true;
      continue;
    }

    bool revisionOk = false;
    const quint64 revisionValue =
        tempRec.attribute(QStringLiteral("revision"), QStringLiteral("1"))
            .toULongLong(&revisionOk);
    if (!revisionOk) {
      diagnostics.append(projectDiagnostic(
          QStringLiteral("project.invalid_temporal_collection"),
          QStringLiteral("A persisted temporal collection description is invalid")));
      failed = true;
      continue;
    }

    const data::TemporalCollectionCreateResult restored =
        context.dataManager().restoreTemporalCollection(
            *recordId, revisionValue,
            {tempRec.attribute(QStringLiteral("name")),
             descriptorElement.text()});
    diagnostics += restored.diagnostics;
    if (restored.collectionId.isNull())
      failed = true;
  }

  for (QgsMapLayer *layer : orderedProjectLayers(project)) {
    if (!layer)
      continue;

    std::optional<data::AssetId> assetId = data::AssetId::fromString(
        layer->customProperty(QStringLiteral("sicnu/assetId")).toString());
    if (!assetId || !context.dataManager().asset(*assetId)) {
      const std::optional<data::SourceDescriptor> source =
          sourceForLayer(*layer);
      if (!source)
        continue;

      if (assetId) {
        const data::Result<data::AssetId> restored =
            context.dataManager().restoreSource(data::RestoreRequest{
                *assetId, data::AssetRevision::initial(), *source,
                data::PersistencePolicy::ProjectPersistent});
        diagnostics += restored.diagnostics();
        if (!restored) {
          failed = true;
          continue;
        }
      } else {
        const data::RegisterResult registered =
            context.dataManager().registerSource(
                data::RegisterRequest{*source});
        diagnostics += registered.diagnostics;
        if (registered.assetId.isNull()) {
          failed = true;
          continue;
        }
        assetId = registered.assetId;
      }
    }

    display::AdoptLayerOptions options;
    options.displayLayerId = display::DisplayLayerId::fromMapLayer(layer);
    const data::Result<display::DisplayLayerId> adopted =
        context.displayManager().adoptLayer(context.mainViewId(), *assetId,
                                            layer, options);
    diagnostics += adopted.diagnostics();
    if (!adopted)
      failed = true;
  }

  // Workspace Governance state: v3 restores the governed entities; a v1 file
  // migrates in memory (M1) by mirroring the restored assets into the
  // governance store — the original file is never rewritten during read.
  // Projects without the SICNU extension element never touch this path.
  sicnu::workspace::WorkspaceService &workspace = context.workspaceService();
  if (extension.isNull()) {
    return failed ? data::Result<void>::failure( std::move( diagnostics ) )
                  : data::Result<void>::success( std::move( diagnostics ) );
  }
  if (!workspace.isStoreOpen()) {
    diagnostics.append(data::Diagnostic{
        QStringLiteral("workspace.store_unavailable"),
        QStringLiteral("governance store is not open; governed state was not "
                       "restored and the project stays legacy v1 on next save"),
        data::DiagnosticSeverity::Warning});
  } else if (isV3) {
    const QDomElement workspaceElement =
        extension.firstChildElement(QStringLiteral("workspace"));
    if (!workspaceElement.isNull()) {
      const QDomElement documentElement =
          workspaceElement.firstChildElement(QStringLiteral("document"));
      const QJsonDocument parsed =
          QJsonDocument::fromJson(documentElement.text().toUtf8());
      if (!parsed.object().isEmpty()) {
        const QVector<data::Diagnostic> restoreDiagnostics =
            workspace.fromProjectJson(parsed.object());
        diagnostics += restoreDiagnostics;
      } else {
        diagnostics.append(data::Diagnostic{
            QStringLiteral("workspace.block_unparseable"),
            QStringLiteral("the v3 workspace block could not be parsed and "
                           "was skipped; governed state was NOT restored"),
            data::DiagnosticSeverity::Warning});
      }
    }
    // Assets mirror last so governed state (datasets etc.) sees stable rows.
    workspace.mirrorAllAssets();
  } else if (isV1) {
    const qint64 mirrored = workspace.mirrorAllAssets();
    diagnostics.append(data::Diagnostic{
        QStringLiteral("workspace.migrated_from_v1"),
        QStringLiteral("project migrated to workspace format v3 in memory "
                       "(%1 assets indexed; the file upgrades on next save)")
            .arg(mirrored),
        data::DiagnosticSeverity::Info});
    workspace.audit(QStringLiteral("migration"),
                    QStringLiteral("project.migrate_v1_v3"),
                    QStringLiteral("workspace"), QString(),
                    QJsonObject{{QStringLiteral("mirroredAssets"), mirrored}});
  }

  if (failed)
    return data::Result<void>::failure(std::move(diagnostics));
  return data::Result<void>::success(std::move(diagnostics));
}

} // namespace sicnu::app
