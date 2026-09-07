# ADR 0135: Resource URI & Identity Model (5.0)

- Status: Accepted (Remote Sensing I/O Foundation 5.0 goal)
- Context: today every call site treats a source as either a
  `std::filesystem`-ish path or a GDAL VSI string. Remote hrefs, directory
  products, subdatasets, STAC assets, VSI wrappers and in-memory artifacts are
  distinguished by scattered `startsWith` checks (and Windows already needed
  one bug class fixed: `/vsicurl/…` must never touch `QFileInfo`). Unicode
  paths, UNC, drive letters, percent-encoding and query strings are regular
  user input, and credentials can appear in remote URLs.
- Decision:
  1. **`resource_uri.h`** introduces `ResourceUri` — a parsed, classified view
     of a source string with `ResourceKind`:
     `LocalFile | LocalDirectory | DirectoryProduct | RemoteHttp | VsiRemote |
     VirtualDataset | Subdataset | StacAsset | InMemory`.
  2. One parser, strict results: scheme detection (drive letters and UNC are
     Windows-local, not schemes), query/fragment split, percent-decoding for
     display only (identity keeps the raw form), VSI prefix stripping. Parsing
     never throws for weird input — it classifies as `Invalid` with a reason.
  3. **Identity vs display**: `canonical()` is the dedup/identity form
     (normalized separators, resolved VSI spelling); `display()` is the
     human form (decoded, credential-redacted). Log/UI surfaces use `display()`
     only; the redaction removes userinfo, and query parameters whose names
     match credential patterns (`X-Amz-Signature`, `sig`, `token`, `key`,
     `SAS`, `GoogleAccessId`) are masked.
  4. **Local safety helpers**: `resolveAgainst(base)` for project-relative
     paths with `..` traversal containment, and Windows long-path (`\\?\`)
     opt-in helper. The model does not replace `SourceDescriptor`; it backs it
     (providers keep their canonicalization, the URI model adds classification
     and safety).
- Consequences: a single tested component answers "is this remote? local?
  a subdataset? safe to log?"; new remote families (S3/GS/Azure VSI) classify
  without new call-site logic; tests pin Unicode/UNC/traversal/redaction
  behavior (`test_io_uri`).
