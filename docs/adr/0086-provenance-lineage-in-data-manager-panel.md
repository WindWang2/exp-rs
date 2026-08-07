# ADR 0086: Provenance and Lineage in the Data Manager Panel

## Context

The provenance model was complete (DerivationRecord + attach + serialize,
ADR-0065 groundwork; lineage queries `derivedFrom` / `derivedOutputsOf`) but
had no UI: the Data Manager panel's asset inspector showed identity, source,
and structure only — nothing about how an asset was produced or what it
produced. The mission's C4 asks for a processing-provenance UI.

## Decision

`DataManagerPanel::showAssetDetails` gains a **"溯源与谱系"** section:
- for assets with a derivation record: algorithm id + version, the JSON
  parameter snapshot, task reference, completion timestamp, and the asset
  names it was **源自** (`derivedFrom`);
- for directly-registered assets: an explicit "无派生记录（直接注册）" row;
- always: the asset names **派生产物** (`derivedOutputsOf`).

## Consequences

- Provenance is now inspectable in the desktop UI: every derived raster can
  answer "what produced me, with what parameters, from which inputs, and what
  was derived from me" (mission P1 provenance surface).
- The lineage queries from A5 are exercised through the UI; a panel test pins
  both the derivation-record rendering and the lineage names.
