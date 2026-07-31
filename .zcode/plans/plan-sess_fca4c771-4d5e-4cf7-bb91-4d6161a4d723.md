## #66 — Real HTTP NetworkProbe + host injection

Closes the last Remote Map Assets gap: today the 4 remote-map providers are constructed in `DataManager::defaultProviders()` (`src/data/data_manager.cpp:191-197`) with `nullptr` probe → `NoNetworkProbe` (always Offline). A real probe must live in `src/app` (`src/data/CMakeLists.txt:57-69` forbids linking `Qt6::Network`), and the host must inject it.

### Design decisions (resolved against constraints)

- **Timing: synchronous probe with a short transfer timeout.** The probe interface is synchronous (`probe() const -> ProbeOutcome`) and runs on the GUI thread inside `registerSource` (thread-guarded at `data_manager.cpp:223`). A deferred/async re-resolve path would need a NEW public `DataManager` API (`refreshAsset`/`setNetworkProbe`) — that **violates the standing constraint** "Do not start later waves by widening the Data Manager interface … expose new interface concepts only when a second real caller requires them." No second caller needs async today. So: short timeout (3s), sync. If a real async caller emerges later, it's a follow-up ticket.
- **WMS/WMTS XML: reachability + minimal fields.** `QgsWmsCapabilities`/`QgsWmtsCapabilities` are **NOT vendored** (confirmed absent from `src/`). The probe hand-parses GetCapabilities XML for the registration-essential fields: `layerNames`, `crsList`, `imageFormat`, `extent`. WMTS `TileMatrixSet` parsing (pixelSizeX/Y from ScaleDenominator, service-discovered z-range) is **deferred** to a follow-up — WMTS still registers Ready with reachability confirmed and z-range from caller options.
- **HTTP primitive: `QgsNetworkAccessManager::blockingGet(request, authCfg)`** (`src/core/network/qgsnetworkaccessmanager.h:481`). Genuinely synchronous, singleton, auto-applies auth + proxy. No nested `QEventLoop`.
- **Testability via pure-logic split** (the `buildRemoteMapUri` / `AuthResolver` precedent): the concrete probe delegates HTTP to an injectable `CapabilitiesFetcher` interface; the XML→`RemoteMapStructure` parsing is a **pure function** unit-tested against fixture XML strings. Production uses a `QgsBlockingCapabilitiesFetcher` (`blockingGet`); tests inject a stub returning canned XML.

### Changes

**1. `src/data/data_manager.{h,cpp}` — probe-accepting registry (NO new public API)**

- Add a private `static std::unique_ptr<internal::SourceProviderRegistry> defaultProviders(const internal::NetworkProbe *probe)` overload. The existing `defaultProviders()` calls `defaultProviders(nullptr)` (backward-compatible).
- Add a private `DataManager(const internal::NetworkProbe *probe, QObject *parent = nullptr)` constructor (friend-accessible pattern mirroring the existing private `DataManager(unique_ptr<SourceProviderRegistry>)` at `data_manager.h:214`). It builds `defaultProviders(probe)` and forwards; the virtual-raster provider bind (lines 204-209) runs in its body unchanged.
- The 4 remote-map providers are constructed with `probe` instead of `nullptr`. The local GDAL/OGR providers ignore it.
- **No public API change.** Both additions are private/friend-gated. The standing constraint is honored.

**2. `src/app/display/network_probe.{h,cpp}` (NEW) — the concrete probe + pure parser**

`network_probe.h`:
- `class CapabilitiesFetcher` — abstract interface: `virtual std::optional<CapabilitiesResponse> fetch(const QString &url, const QString &authConfigId) const = 0;` where `CapabilitiesResponse{ int httpStatus; QByteArray body; QString contentType; }`.
- `class QgisNetworkProbe final : public data::internal::NetworkProbe` — the concrete probe. Constructor takes `const CapabilitiesFetcher *fetcher` (non-owning; production injects a `QgsBlockingCapabilitiesFetcher`, tests inject a stub). Implements `probe(service, url, options)`:
  - WMS/WMTS: build the GetCapabilities URL (KVP form: `?SERVICE=WMS&REQUEST=GetCapabilities&VERSION=1.3.0`), `fetcher->fetch(url, authConfigId)`, then `parseWmsCapabilities(body)` / `parseWmtsCapabilities(body)` (pure functions, below).
  - XYZ/TMS: substitute `{z}/{x}/{y}` → a center triplet at z=zMin (or z=0 if none), `fetcher->fetch(tileUrl, authConfigId)`, decide state from HTTP status (200 → Ready; 401/403 → AuthenticationRequired; network error / timeout → Offline).
  - Short transfer timeout enforced by the fetcher (production: `QNetworkRequest::setTransferTimeout(3000ms)`).
- Pure parser declarations (in the header for unit-test visibility, or a sibling `wms_capabilities_parser.h`): `RemoteMapStructure parseWmsCapabilities(const QByteArray &xml)`, `parseWmtsCapabilities(const QByteArray &xml)`. These walk the GetCapabilities XML (Qt6::Xml `QDomDocument`) for `<Layer><Name>`, `<CRS>`/`<SRS>`, `<Format>`, `<EX_GeographicBoundingBox>`/`<LatLonBoundingBox>`.

`network_probe.cpp`:
- `class QgsBlockingCapabilitiesFetcher final : public CapabilitiesFetcher` — production impl. Builds a `QNetworkRequest` with `setTransferTimeout(3000)`, calls `QgsNetworkAccessManager::instance()->blockingGet(request, authConfigId)`, maps the `QgsNetworkReplyContent` to `CapabilitiesResponse`. Lives here (links `Qt6::Network` + `qgis_core`).
- The `QgisNetworkProbe::probe` body + parser bodies.

**3. `src/app/project_context.{h,cpp}` — host wires the real probe**

- `ProjectContext` gains a private ctor `ProjectContext(const display::QgisNetworkProbe *probe)` initializing `m_dataManager(probe)` and `m_displayManager(&m_dataManager)`. (The probe is owned by the host that calls `create()`; ProjectContext holds a non-owning pointer only for the registry-build step.)
  - Actually cleaner: `ProjectContext` OWNS the probe (a `std::unique_ptr<QgisNetworkProbe>` member + the `QgsBlockingCapabilitiesFetcher`), constructed in `create()`. The private ctor takes no probe arg; it builds its own. This keeps `create()`'s signature unchanged and lifecycle simple.
- `ProjectContext::create()` builds the probe, passes it to the private `DataManager(probe)` ctor via the init list.
- A new test-only seam: `ProjectContext::create(mainViewSpec, NetworkProbe*)` overload (or a `createForTesting` factory) so tests can inject a stub probe and assert a remote-map asset registers Ready. This mirrors the existing `QgisDisplayManager(dataManager, authResolver)` test-injection ctor precedent — a constructor overload, not a public setter.

**4. `src/app/CMakeLists.txt` (blob-staged — file is dirty with histogram work)**

- Add `display/network_probe.cpp` to `sicnu_qgis_display` sources.
- Add `Qt6::Network` to `sicnu_qgis_display`'s `target_link_libraries` (PRIVATE — the probe is an impl detail; `qgis_core` already provides `QgsNetworkAccessManager`). Verify the forbidden-link guard in `src/data/CMakeLists.txt` still passes (it only checks `sicnu_data`, not `sicnu_qgis_display`).

**5. `tests/test_wms_capabilities_parser.cpp` (NEW) — pure-logic unit tests (no HTTP)**

- Stage fixture GetCapabilities XML strings (a minimal valid WMS 1.3.0 doc, a WMTS doc) as test constants.
- Assert `parseWmsCapabilities` extracts: expected `layerNames`, `crsList`, `imageFormat`, `extent`.
- Assert malformed/empty XML → `valid == false` (probe will report Offline).
- WMTS: assert reachability fields parse; TileMatrixSet is a documented follow-up (assert it's absent/deferred).

**6. `tests/test_network_probe.cpp` (NEW) — the concrete probe with a stub fetcher**

- Inject a `StubCapabilitiesFetcher` returning canned XML + status codes.
- WMS reachable → `state == Ready`, structure has the parsed fields.
- WMS 401 → `state == AuthenticationRequired`.
- WMS network error / timeout → `state == Offline` (NOT Error — the conservative contract from `NoNetworkProbe`).
- XYZ reachable (200 on tile GET) → `state == Ready`; XYZ 404 → `Offline`.
- No fetcher injected (nullptr) → graceful Offline (mirrors NoNetworkProbe).
- Assert authConfigId is passed through to the fetcher (credential discipline — the probe never handles credential material, only the id).

**7. `tests/test_project_context_remote_probe.cpp` (NEW, or extend an existing ProjectContext test) — end-to-end host wiring**

- Construct `ProjectContext` with an injected stub probe, register a WMS source, assert the asset is `Ready` with the parsed structure (proving the host wires the probe into the DataManager). This is the integration proof that closes #66.

**8. `tests/CMakeLists.txt` (blob-staged)**

- Add the 2-3 new test targets. `test_wms_capabilities_parser` links `Qt6::Xml` + `Sicnu::data` + `sicnu_qgis_display` (for the parser symbols). `test_network_probe` + the ProjectContext test link `sicnu_qgis_display` + `qgis_core` + `qgis_gui`.

### TDD order

1. `test_wms_capabilities_parser.cpp` (pure XML parse) → red → implement parsers → green.
2. `test_network_probe.cpp` (concrete probe + stub fetcher) → red → implement `QgisNetworkProbe` + `CapabilitiesFetcher` + `QgsBlockingCapabilitiesFetcher` → green.
3. `defaultProviders(probe)` + private `DataManager(probe)` ctor → existing tests stay green (nullptr default), new ProjectContext test → red → green.
4. `ProjectContext` wiring (owns probe, injects into DataManager) → end-to-end test → green.
5. CMake blob-staging: verify each staged file CONFIGURES in a fresh build dir before commit (the #61 lesson).
6. Full regression (22+ targets).
7. Two-axis review (parallel Explore): (a) probe correctness/auth-discipline/timeout-handling, (b) spec conformance + the "no new public DataManager API" constraint check.
8. Commit + gh issue comment on #66.

### Why this shape

- **No public DataManager API added** — the probe injection is private/friend-gated, honoring the standing constraint. The existing `StubNetworkProbe`-based tests stay valid (the seam is unchanged).
- **Pure-logic split makes the hard part testable** — GetCapabilities XML parsing (the bulk of the code, and the part most likely to have bugs) is unit-tested against fixture strings with zero HTTP. Only the thin `QgsBlockingCapabilitiesFetcher` wrapper touches the network, and it's a trivial delegation to `blockingGet`.
- **Synchronous + short timeout matches the existing contract** — no architectural change to `registerSource`, no JobEngine worker, no new re-resolve API. The standing async-deferral is preserved as a documented follow-up gated on a real caller.
- **Auth discipline airtight** — the probe carries only `authConfigId` (never credential material); `blockingGet(request, authCfg)` applies it via `QgsAuthManager`. Mirrors the `AuthResolver` pattern exactly.
- **src/data stays network-free** — the concrete probe + fetcher live in `sicnu_qgis_display` (src/app), which already links `qgis_core`/`qgis_gui` and can add `Qt6::Network`. The `src/data` forbidden-link guard is untouched.

### Risk / scope notes

- **UI freeze on slow servers** (1-3s worst case): documented and accepted for this slice. The `blockingGet` timeout caps it. A deferred/async path is a follow-up if a real caller needs it.
- **XML parsing edge cases** (WMS 1.1.1 vs 1.3.0, CRS vs SRS, LatLonBoundingBox vs EX_GeographicBoundingBox, namespaces): the first slice handles the common 1.3.0 case; exotic variants are follow-ups. Tests use representative fixtures, not exhaustive schema coverage.
- **Blob-staging discipline** (the #61 lesson): both `src/app/CMakeLists.txt` and `tests/CMakeLists.txt` are dirty with unrelated histogram/stretch work. I'll stage ONLY my line additions via `git hash-object`/`update-index`, and verify each configures in a fresh build dir before committing.
- **WMTS TileMatrixSet parsing deferred** — WMTS registers Ready with reachability + caller z-range; pixelSizeX/Y and service-discovered z-range need the TileMatrix parse, which is a follow-up ticket. Documented in the probe's doc comment.