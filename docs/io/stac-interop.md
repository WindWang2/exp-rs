# STAC Interoperability

The foundation maps STAC Items onto the canonical metadata model in both
directions (`src/geospatial/stac/stac_mapper.h`, ADR 0130). This replaces the
earlier split — a GUI-only shallow parser and a separate headless adapter —
with one tested vocabulary (`tests/test_io_stac.cpp`, plus the round-trip
matrix).

## Reading: STAC Item → canonical

```cpp
sicnu::geo::StacItem item = sicnu::geo::StacItem::parseText(jsonText);
sicnu::geo::RasterMetadata meta = sicnu::geo::stacItemToCanonical(item);
```

Mapped fields (declared vocabulary; absence stays absence):

| STAC | Canonical |
|---|---|
| `id` | `productId` |
| `properties.datetime` (or `start_datetime`) | `acquisitionTime` |
| `platform`, `constellation`, `instruments` | `platform`, `sensor` |
| `s2:processing_level` / `processing:level` | `processingLevel` |
| `eo:cloud_cover` | `cloudCover` (+ `hasCloudCover`) |
| `gsd` | `gsd` (+ `hasGsd`) |
| `proj:epsg` | `crs.authid` (declared, not resolved) |
| `sar:polarizations`, `sar:instrument_mode` | `metadata["sar:*"]` |
| `assets` (role `data` first) | primary asset `path`, media type → driver hint |

Structural violations (no `id`, no time, no assets, non-Feature) throw
`GeoError(InvalidArgument)` with a field-level detail — a broken catalog never
yields half-built metadata.

## Writing: canonical → STAC

```cpp
Json::Value item = sicnu::geo::canonicalToStacItem(meta, assetHref, itemId);
```

Produces a STAC 1.0-compatible Item (projection/EO/SAR extensions declared as
used). Requires the fields STAC mandates: acquisition time (structured error
otherwise) and extent for bbox/geometry. `eo:bands` is derived from canonical
band roles/descriptions/wavelengths. The generated document re-parses through
`StacItem::parse` losslessly for the mapped fields (tested).

## Lineage & persistence

STAC does not introduce a second persistence system: provenance continues to
flow through the governance APIs (DerivationRecord / lineage edges, ADR 0129);
STAC documents are metadata in, metadata out.

## Remote assets

Asset hrefs keep their declared form; remote opens ride GDAL's VSI layer
(`/vsicurl/`-style) with the existing SSRF policy in the STAC browser path.
`io:inspect` / `data doctor` accept VSI handles directly.

## UTC instant normalization (8.0)

STAC datetimes arrive with mixed offsets (`Z`, `+02:00`, naive). The mapper
derives normalized UTC instants at parse time (`datetimeUtc` on `StacItem`);
`buildTemporalSeries` orders series by parsed instants (parseable instants
form one ordering class, unparseable datetimes another — a class-split keeps
the sort transitive while letting mixed-offset acquisitions interleave
correctly), ties break deterministically
(item id, then input order), and duplicate acquisition instants are reported
through `buildTemporalSeriesDetailed` (kept, never dropped). Naive datetimes
are treated as UTC per the STAC spec and flagged (`datetimeAssumedUtc`);
unparseable datetimes leave the UTC form empty — never a guessed time.
`toJson()` keeps the origin's verbatim RFC 3339 wire form.
