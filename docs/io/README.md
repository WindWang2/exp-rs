# Geospatial I/O Foundation 5.0 — Guides

The I/O foundation (ADR 0134, extending the 4.0 base of ADR 0130) is the
single system-level encapsulation of GDAL/PROJ for ExpRS. It lives in the
Qt-free library `Sicnu::Geospatial` (`src/geospatial`) and is reachable from
every surface:

| Surface | Entry points |
|---|---|
| C++ (headless-safe) | `sicnu::geo::*` in `src/geospatial` |
| RSOperators | `io:translate`, `io:warp`, `io:reproject`, `io:clip`, `io:convert_format`, `io:build_overviews`, `io:make_cog`, `io:vector_convert`, `io:inspect`, `io:doctor` |
| CLI | `sicnu_geo_rs_cli data inspect <ds> [--stats]`, `data doctor <ds> [--stats]`, `data probe <ds>`, `data capabilities <ds>`, `data product describe <path>`, `data stac <item.json>` |
| MCP / agent | the `io:` algorithm namespace plus the `io:probe`, `io:capabilities`, `io:product` tools |

Guides:

1. **[certified-formats.md](certified-formats.md)** — the support matrix
   (Certified / Accessible / Unavailable), CRS policy and NoData/scale-offset
   policy.
2. **[canonical-metadata.md](canonical-metadata.md)** — the one metadata
   vocabulary, its JSON schema and its no-full-scan inspection contract.
3. **[cog-guide.md](cog-guide.md)** — producing and validating COGs; safe
   presets and the lossless-for-science rule.
4. **[stac-interop.md](stac-interop.md)** — STAC Item ⇄ canonical metadata.
5. **[multidim-guide.md](multidim-guide.md)** — variable/time/level semantics
   with lazy slices (no flatten-to-bands).
6. **[conversion-guide.md](conversion-guide.md)** — conversion/export through
   the authoritative operators; atomicity and cancel semantics.
7. **[foundation-5-audit.md](foundation-5-audit.md)** — the 5.0 gap analysis:
   how data enters exp-rs, the baseline matrix and the work items this
   foundation answers.

## Foundation 5.0 additions (ADR 0134–0141)

* **Resource URI & identity** (ADR 0135, `util/resource_uri.*`) — one strict
  classifier for every source string with credential-redacted display form
  and `..`-traversal containment.
* **Probe contract & capabilities** (ADR 0136, `probe/*`,
  `formats/format_profiles.*`) — bounded sniff/identify with typed failures
  and per-dataset capability answers.
* **Bounded raster access** (ADR 0134) — block reads, planned tile walks with
  cancellation, overview policies (`Exact` default; a silently sampled
  overview is a wrong-answer factory), and the single explicit resampling
  entry point.
* **Sensor product adapters** (ADR 0137, `products/*`, see
  [docs/products/product-adapters.md](../products/product-adapters.md)) —
  family registry, constituent enumeration with canonical band roles, and
  completeness verdicts (`docs/interoperability/product-matrix.md`).
* **Multidimensional policy** (ADR 0138) — no forced flattening; lazy slices
  with honest CF handling; Zarr/GeoParquet stay capability-gated.
* **Remote I/O** (ADR 0139, `remote/*`) — bounded remote probe plus the local
  HTTP range harness proving window reads never download the file
  (`test_io_remote_range`).
* **Export/conversion policy** (ADR 0140) — staged → validated → published
  writers behind one request/report schema with loss negotiation.
* **I/O error model** (ADR 0141, `common.h`) — the extended `GeoError`
  taxonomy with scoped GDAL error hygiene.

Core rule of thumb: **wrap, don't rewrite**. GDAL/PROJ remain the engines;
the foundation owns policy (CRS, fidelity, atomicity, certification) and
contracts (reader/writer), never the math. Unknown stays unknown — metadata
is never fabricated.
