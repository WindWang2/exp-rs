# Geospatial I/O Foundation 4.0 — Guides

The I/O foundation (ADR 0130) is the single system-level encapsulation of
GDAL/PROJ for ExpRS. It lives in the Qt-free library `Sicnu::Geospatial`
(`src/geospatial`) and is reachable from every surface:

| Surface | Entry points |
|---|---|
| C++ (headless-safe) | `sicnu::geo::*` in `src/geospatial` |
| RSOperators | `io:translate`, `io:warp`, `io:reproject`, `io:clip`, `io:convert_format`, `io:build_overviews`, `io:make_cog`, `io:vector_convert`, `io:inspect`, `io:doctor` |
| CLI | `sicnu_geo_rs_cli data inspect <ds> [--stats]`, `data doctor <ds> [--stats]` |
| MCP / agent | the `io:` algorithm namespace |

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

Core rule of thumb: **wrap, don't rewrite**. GDAL/PROJ remain the engines;
the foundation owns policy (CRS, fidelity, atomicity, certification) and
contracts (reader/writer), never the math.
