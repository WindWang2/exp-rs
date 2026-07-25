# Spec: Remote Map Assets (WMS / WMTS / TMS / XYZ)

**Parent:** `docs/superpowers/specs/2026-07-24-data-manager-architecture-spec.md` — Asset Capabilities (lines 99-109, "WMS, WMTS, TMS, and XYZ assets may declare `Renderable` and `OfflineCacheable` but do not claim `ReadablePixels` or `BandStatistics`"), State (`Offline`, `AuthenticationRequired`, lines 111+), RemoteMap kind, and the "Deferred After the First Deliverable" list (lines 522-533: "WMS, WMTS, TMS, XYZ, and Connection Catalog UI").
**Status:** Proposed.

## Problem Statement

Web map services are first-class data sources for remote-sensing basemaps and reference imagery, but the application treats them as second-class. `AssetKind::RemoteMap` and `StorageKind::Remote` exist in the enums yet nothing produces a real remote-map asset: no remote-map source provider is registered, so `registerSource` falls through the GDAL/OGR/VRT providers and fails with `source.unsupported`. The display manager's `materializeLayer` returns `{}` for `AssetKind::RemoteMap` — a remote asset cannot render. And `SourceDescriptor::authConfigId` is a passed-through string with **no consumer**: nothing applies it to a `QgsRasterLayer`, so a credentials-protected service could never authenticate even if it registered.

Today the only way a remote layer enters the app is as a raw QGIS `QgsRasterLayer` adopted outside the Data Asset model, and `ProjectContext::isRemoteSource` *actively rejects* `http(s)://` layers from becoming Data Assets. So remote maps live entirely outside the catalog: no dedup, no project persistence of their meaning, no lease/lifecycle, no consistent credential handling, no relationship to the rest of the asset model.

The architecture spec already says what this should be (lines 99-109): a remote-map asset declares `Renderable | OfflineCacheable` and **not** `ReadablePixels`/`BandStatistics` (it is not a pixel-analysis source just because QGIS represents it with `QgsRasterLayer`); its states include `Offline` and `AuthenticationRequired`; credentials are never stored — only an `authConfigId`. None of the plumbing exists.

## Solution

Introduce the **Remote Map Asset** as a first-class catalog kind backed by per-service source providers and a managed-credential display-time injection seam — without widening the Data Manager interface (the registration path is already provider-driven and kind-agnostic).

- A **RemoteMapStructure** value type rides the existing `AssetStructure` variant alongside `RasterStructure`/`VectorStructure`. It carries the service-derived metadata a remote map can honestly report without fetching pixels: service kind, declared layer name(s), advertised CRS list, extent, pixel size / tile-matrix resolution (where the service publishes one), image format, and tile range bounds where applicable. It is the dedup- and display-relevant shape; it never claims band statistics it cannot honor.
- **Per-service source providers** (`WmsSourceProvider`, `WmtsSourceProvider`, `TmsSourceProvider`, `XyzSourceProvider`) join the provider registry and claim the `wms` / `wmts` / `tms` / `xyz` provider keys. Each `resolve()` performs a **lightweight GetCapabilities-style probe** (or, for stateless XYZ/TMS, a probe of the declared tile endpoint) sufficient to populate `RemoteMapStructure` and set state — **Ready** when the service answers, **Offline** when unreachable, **AuthenticationRequired** when the service demands credentials and none/bad are configured. Resolve is *structural* (metadata + reachability), never pixel-reading: no `ReadablePixels`, no `BandStatistics`, no histogram.
- **Registration reuses the existing pipeline.** A remote-map `SourceDescriptor` (`providerKey`, `canonicalSource` connection URI, `dataOptions` for layer/CRS/format, `authConfigId`) is registered through the unchanged `registerSource`; the matching provider returns a `ResolvedSource` and the asset dedups by SourceKey, persists via the existing project-extension round-trip, and is queryable by kind. **No new `DataManager` methods.** The "do not start later waves by widening the Data Manager interface" constraint holds: the wave is provider-shaped (new provider classes + a display arm), not catalog-API-shaped.
- **Credentials are a managed `authConfigId` injected at display time.** `SourceDescriptor` carries only the `authConfigId` (the standing constraint — never credential material in descriptors, SourceKeys, XML, or logs). A first-party **AuthResolver** seam translates the `authConfigId` into a single **configured data-source URI** (a string carrying the auth-cfg parameter) at materialization time; the QGIS layer receives credentials through the existing `QgsAuthManager` path, never through a field on the asset. Resolve that detects a credentials gate sets `AssetState::AuthenticationRequired` instead of `Offline`, so the UI can prompt for an auth config rather than report a dead service.
- **Materialization routes through `QgsRasterLayer`.** The display manager gains a `RemoteMap` arm in `materializeLayer` that, per canonical provider key, builds the QGIS provider URI (`wms`/`wmts`/`xyz`/`tms`) from the `SourceDescriptor` fields and applies the resolved auth config. The layer is added through the existing display seam (lease acquired, `assetChanged` on reload) — remote maps become display citizens identical to rasters/vectors in lifecycle, just not in pixel capabilities.
- **Serialization round-trips the descriptor, not fetched tiles.** The `.qgz` extension already persists `<source provider canonical subdataset authConfigId>` for any asset; remote maps need no new element. The `RemoteMapStructure` is re-derived on restore by re-probing the service (a remote map is *re-resolved*, like a relocated raster, not stored as a snapshot of a past fetch). Tiles/cache are never persisted.

## User Stories

1. As an analyst, I want to register a WMS service + layer as a Data Asset, so that it dedups, persists in my project, and appears in the Data Manager alongside my rasters.
2. As an analyst, I want a remote map to render in the map view like any raster, so that I can use it as a reference basemap.
3. As an analyst, I want a credentials-protected service to report `AuthenticationRequired` and let me attach an auth config, so that I am not told the service is simply "down" when it is asking for a login.
4. As an analyst, I want an unreachable service to report `Offline` rather than `Error`, so that the asset stays in my catalog and retries on reopen instead of being dropped.
5. As an analyst, I want to register TMS/XYZ tile services the same way I register WMS, so that all four web-map families are first-class and consistent.
6. As an analyst, I want my remote maps to survive save and reopen with their connection + layer + auth config intact, so that reopening my project restores my basemaps.
7. As an analyst, I want remote maps to NOT offer pixel readback or band statistics, so that the UI never implies I can run a spectral index on a web map.
8. As a developer, I want the `authConfigId` to be the *only* credential carrier and to be injected at display time, so that no credential material is ever stored in the catalog, the project, or logs.
9. As a developer, I want remote-map resolve to be a metadata + reachability probe — never a pixel fetch — so that registration is fast and never pulls gigabytes of tiles.
10. As a developer, I want the four services to share a single registration/materialization shape and differ only in their provider-specific URI encoding, so that adding a fifth service later is a small provider, not a new wave.

## Implementation Decisions

- **RemoteMapStructure is a value type, no QGIS types.** It lives in `src/data` (mirroring `RasterStructure`): service kind (`Wms`/`Wmts`/`Tms`/`Xyz`), ordered declared layer names (a WMS GetMap can request several), advertised CRS list, service-reported extent, declared image format, and tile-matrix / pixel-size + z-range for tiled services. It exposes no `Qgs*` type (the `src/data` header constraint). It carries a `toJson()`/`fromJson()` round-trip — **a deliberate new affordance** (the existing `RasterStructure`/`VectorStructure` are plain aggregates without JSON), used only for diagnostics/debugging, since **the persisted identity is the `SourceDescriptor`, not a structure snapshot** (a remote map is re-resolved on restore).
- **`AssetStructure` gains the arm; `structuresCompatible` gains the branch.** `std::variant<std::monostate, RasterStructure, VectorStructure, RemoteMapStructure>`. Two remote-map structures are compatible (for `relocate`/re-resolve) when their service kind + provider key + declared layer set match; structure content drift (a service advertising a new CRS) does not block relocate but is surfaced via `assetChanged`. The `monostate` escape hatch remains for providers that cannot probe.
- **Per-service providers are registered in `defaultProviders()`.** Each is a `SourceProvider` subclass; `supports()` claims its provider key (`wms`/`wmts`/`tms`/`xyz`); `resolve()` returns `kind=RemoteMap`, `storageKind=Remote`, `canonicalSource` normalized (service base URL for WMS/WMTS, the tile-URL template for XYZ/TMS), `canonicalProviderKey` set, capabilities `Renderable | OfflineCacheable` (**never** `ReadablePixels`/`BandStatistics`/`QueryableFeatures` — parent spec line 109), and state per the probe outcome. The providers are stateless (no back-reference needed, unlike VRT) — they probe the network directly through a thin, injectable `NetworkProbe` interface so tests do not hit the real network.
- **The probe is metadata + reachability, structured by outcome.** `Ready` when the service publishes the requested layer(s); `Offline` when the endpoint is unreachable / times out; `AuthenticationRequired` when the service returns an auth challenge and no usable auth config is supplied; `Error` only for malformed-service responses (not transient failures). State transitions are idempotent and re-runnable on reopen/reload. A configurable short timeout keeps registration responsive.
- **No Data Manager interface widening.** Registration is `registerSource({source, persistence})`; the provider does the rest. Restore is `restoreSource({id, revision, source, persistence})` — already provider-driven. No `createRemoteMap`, no `remoteMapStructure(id)` query is added *unless* a second real caller requires it (deferred — the creation UI is out of scope). The catalog already stores `RemoteMap` via the existing `AssetSnapshot`.
- **`AuthResolver` is a first-party seam, not a new asset concept.** An injectable `AuthResolver` (interface in `src/app`, since it talks to `QgsAuthManager`) maps an `authConfigId` → a single configured data-source URI string that the QGIS provider consumes. The display manager holds one (default: the real `QgsAuthManager`-backed resolver; tests inject a stub). Resolve applies it *before* constructing the `QgsRasterLayer`, so the layer is built authenticated from the start. An empty `authConfigId` with a service demanding auth → `AuthenticationRequired` state, no layer materialized. **The `authConfigId` is the sole credential carrier; the resolver never returns credential material to the caller** — it produces one configured URI string, period.
- **Materialization is one new arm + a per-service URI builder.** `materializeLayer` gains `case AssetKind::RemoteMap:` returning a `QgsRasterLayer` built with the provider key from `canonicalProviderKey` and a URI from a `buildRemoteMapUri(providerKey, canonicalSource, dataOptions, resolvedAuth)` helper (per-service encoding: WMS/WMTS `QgsDataSourceUri` with layers/crs/format; XYZ/TMS the tile template with z-range). `materializationSource` stays path/subdataset for rasters/vectors/virtual; remote maps go through the new builder. The `addLayer` Ready/Renderable guard already accepts a Ready remote map.
- **`isRemoteSource` is refined, not removed.** The current guard rejects *any* `http(s)://` QGIS layer from adoption as a Data Asset. Once remote maps register through the catalog, raw-URI remote QGIS layers that bypass the catalog are still rejected (adoption is for catalog-managed assets), but a remote-map asset's `canonicalSource` is a URL and must not be blocked at register time. The guard is narrowed to adoption-time (legacy layer adoption), not registration-time.
- **Provider-key audit (out of wave, flagged).** `output_committer.cpp::providerKeyFor` and `collection_import_service.cpp` currently map `RemoteMap → "gdal"` (treat remote maps as GDAL-openable). Those are processing-output paths and are wrong for real remote maps, but they are out of scope for a registration/display wave; this spec records the audit as a follow-up so they are not forgotten.

### Decision-rich shape (from the design, not a working demo)

```text
enum class RemoteMapService { Wms, Wmts, Tms, Xyz };

RemoteMapStructure {
  RemoteMapService service;
  QStringList layerNames;        // ordered, as the service declares them
  QStringList crsList;           // advertised CRS (authid or WKT)
  SpatialExtent extent;          // service-reported, valid=false if none
  QString imageFormat;           // "image/png" etc.; empty if N/A
  std::optional<double> pixelSizeX, pixelSizeY;  // tile-matrix / declared res
  int zMin = 0, zMax = 0;        // tiled services; 0 if non-tiled
  bool valid = false;
} // toJson()/fromJson() for diagnostics only

// Provider shape (stateless; NetworkProbe is the injectable I/O seam)
class XyzSourceProvider final : public internal::SourceProvider {
  explicit XyzSourceProvider( NetworkProbe *probe = nullptr );
  bool supports( const SourceDescriptor & ) const override;            // providerKey == "xyz"
  Result<internal::ResolvedSource> resolve( const SourceDescriptor & ) const override;
};
// WmsSourceProvider / WmtsSourceProvider / TmsSourceProvider mirror this.

// First-party auth seam (src/app; injectable for tests). Lives in src/app, so
// it qualifies the data-layer Result type as data::Result (src/app convention).
class AuthResolver {
 public:
  virtual ~AuthResolver() = default;
  // Apply the auth config to the URI; returns the single configured URI string
  // the QGIS layer is built from. Never returns credential material.
  virtual data::Result<QString> applyAuthConfig( const QString &authConfigId,
                                                 const QString &providerKey,
                                                 const QString &uri ) const = 0;
};

// Display manager gains only this (no DataManager change):
//   materializeLayer: case AssetKind::RemoteMap -> QgsRasterLayer(uri, name, providerKey)
QString buildRemoteMapUri( const QString &providerKey, const QString &canonicalSource,
                           const QMap<QString,QString> &dataOptions,
                           const QString &resolvedAuthConfiguredUri );
```

## Testing Decisions

- **The seam is the provider + the display manager, never the network.** Each provider is tested with an injected `NetworkProbe` stub returning canned capabilities/timeout/auth-challenge responses — no live network. `test_data_source_providers.cpp` is the template. External behavior: a WMS descriptor resolves to `Ready` with the advertised layers/CRS; an unreachable endpoint resolves to `Offline`; an auth challenge with no config resolves to `AuthenticationRequired`; a stateless XYZ template resolves without a capabilities round-trip; capabilities are `Renderable | OfflineCacheable` only.
- **Registration through the existing pipeline** is tested via `registerSource` (mirroring `test_data_manager.cpp`'s existing `InMemorySourceProvider` `RemoteMap` test at lines 80-88, now backed by a real provider): dedup by SourceKey, persistence, query by kind/state, lease acquire. No new `DataManager` assertions are possible because no new API exists — this is the point.
- **Materialization** is tested through `QgisDisplayManager` with a stub `AuthResolver`: a Ready remote map yields a valid `QgsRasterLayer` of the right provider key; an `AuthenticationRequired`/`Offline` remote map is refused at the `addLayer` Ready guard. Renderer internals are not tested.
- **Auth seam** is tested at the `AuthResolver` boundary: `applyAuthConfig` produces a URI carrying the auth-cfg param given a valid id; an empty id with a demanding service yields `AuthenticationRequired`; the resolver never returns credential material (the test asserts the returned string contains only the auth-cfg id, not a password). `test_data_project_roundtrip.cpp` already covers `authConfigId` XML round-trip; this wave adds the display-time application test.
- **Serialization** is tested by registering a remote map, save → clear → reopen, and asserting same AssetId, same `SourceDescriptor` (provider/canonical/layer/authConfigId), and that the structure was *re-derived* by re-probing (stub returns the same canned response) rather than stored. `test_data_project_roundtrip.cpp` is the host.
- **`structuresCompatible` for remote maps** is tested at the `relocate`/re-resolve seam: same service+layers → compatible; different layer set → incompatible relocate.
- **Prior art:** `test_data_source_providers.cpp` (provider contract), `test_qgis_display_manager.cpp` (materialization), `test_data_project_roundtrip.cpp` (serialization + authConfigId), `test_data_manager.cpp:80-88` (the existing RemoteMap registration test this wave makes real).

## Out of Scope

- **Connection Catalog UI** (the parent spec's full deferred item). Named-connection storage, a connection browser, and a "manage connections" dialog are a follow-up wave. This wave's `SourceDescriptor.canonicalSource` carries a connection URI/template directly; a later wave can introduce named connections as a second real caller without rework.
- **Remote-map creation UI / "Add WMS layer" dialog.** A QGIS-style source-select dialog is deferred (mirrors how the virtual-raster wave deferred its creation UI). Registration is driven programmatically (and by tests) this wave.
- **Offline cache materialization.** `OfflineCacheable` is *declared* (so the capability is honest and a later wave can wire it) but this wave does not seed/read a tile cache; an `Offline` state means "service unreachable," not "served from cache."
- **Temporal dimensions.** WMS `TIME` / WMTS temporal tile-matrix selection is deferred (the `Temporal` capability is not declared this wave).
- **Reprojection-on-the-fly policy UI.** QGIS reprojection happens in the renderer; a user-facing CRS negotiation is deferred.
- **Processing-output provider-key audit.** The wrong `RemoteMap → "gdal"` mappings in `output_committer.cpp` and `collection_import_service.cpp` are recorded as a follow-up, not fixed here (processing-output paths).
- **Pixel readback / statistics** for remote maps — permanently out of scope by parent-spec line 109.

## Further Notes

- This wave makes the parent spec's lines 99-109 actually true: a remote map is a catalog citizen with honest capabilities (`Renderable | OfflineCacheable`, nothing it cannot honor) and the two remote-specific states (`Offline`, `AuthenticationRequired`) become reachable through real code.
- The credential discipline is the load-bearing constraint and the largest new concept: the `authConfigId` is already carried everywhere it needs to be (descriptor, XML, provenance) but has **no consumer** today. The `AuthResolver` seam is the first place first-party code touches `QgsAuthManager`, and it is shaped so credential material never crosses it — the resolver produces a *configured URI*, never credentials. This sets the precedent for every later credential-using feature.
- The wave is provider-shaped by design: the "no interface widening" constraint is satisfied because `registerSource` was already kind-agnostic. The only catalog-adjacent change is the `AssetStructure` variant arm + `structuresCompatible` branch, which is a data-layer model extension, not an interface extension.
- Network I/O is the first non-file provider input. The injectable `NetworkProbe` keeps the provider unit-testable and establishes the pattern for any future network-backed provider.
- Wave ordering within this spec: (1) `RemoteMapStructure` value type + `AssetStructure` arm + `structuresCompatible` branch, (2) `NetworkProbe` interface + one provider (XYZ — stateless, no capabilities round-trip) + provider-registry registration + registration tests, (3) the remaining three providers (WMS/WMTS/TMS) sharing the shape, (4) `AuthResolver` seam + display `RemoteMap` materialization arm, (5) serialization round-trip + `isRemoteSource` refinement. Each is a small commit; (2) is the first end-to-end registration, (4) is the first end-to-end display.
