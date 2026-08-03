# 01 — Optional ProductDiscoverer in CollectionImportService

**What to build:** Make `ProductDiscoverer* discoverer` optional (`nullptr` default) in `CollectionImportService`. Automatically instantiate a default `SatelliteProductsDiscoverer` internally when `nullptr` is passed.

**Blocked by:** None — can start immediately.

**Status:** ready-for-agent

- [ ] `CollectionImportService` can be constructed with `CollectionImportService( dataManager )`
- [ ] Internal `m_defaultDiscoverer` manages `SatelliteProductsDiscoverer` lifetime when discoverer is omitted
- [ ] Probing and committing with default discoverer works end-to-end
- [ ] Existing tests in `test_collection_import_service` pass cleanly
