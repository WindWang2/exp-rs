# ADR 0050: Relocate StacClient to src/app/ and Absorb COG Asset Selection

## Status
Accepted

## Context
`StacClient` (src/agent/stac_client.{h,cpp}) was compiled into the app
library (src/app/CMakeLists.txt) despite living in src/agent/, whose
CMakeLists does not list it — an orphaned location. Its sole consumer,
`StacBrowserDialog`, also owned STAC-domain knowledge that belongs on the
client side of the seam: COG asset sniffing (picking which asset href of
a STAC item is the cloud-optimized GeoTIFF) and `/vsicurl/` prefixing.

## Decision
1. **Move `StacClient` to src/app/**: `src/app/stac_client.{h,cpp}`,
   compiled via a relative path in the app target; the test target's
   source path/include directory and the dialog's include updated
   accordingly. Class name and API unchanged.

2. **Add `StacClient::selectCogHref(const QJsonObject&)`**: static
   helper returning the item's COG asset href — first asset in key
   order whose href ends with `.tif` or whose type is `image/tiff` —
   validated with `validateAssetHref` and prefixed with `/vsicurl/`;
   empty when unusable. The dialog calls the one-liner.

## Consequences
- **STAC asset-selection logic has a single owner**; the dialog is a UI call.
- **SSRF policy stays enforced** for asset hrefs, now inside the helper.
- **Invalid-href items show the generic "no COG asset" message** instead of "Rejected STAC asset href".
- **`validateAssetHref` remains public and unit-tested**.
