# 02 — Simplify LandsatImportDialog Call sites

**What to build:** Update `LandsatImportDialog` to construct `CollectionImportService` using the zero-boilerplate `CollectionImportService service( m_dataManager )` constructor.

**Blocked by:** 01 — Optional ProductDiscoverer in CollectionImportService

**Status:** ready-for-agent

- [ ] `LandsatImportDialog` uses zero-boilerplate `CollectionImportService( m_dataManager )`
- [ ] Code builds without warnings
- [ ] Landsat product probing and committing in GUI dialog functions correctly
