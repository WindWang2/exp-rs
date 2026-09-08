# ADR 0134: Remote Sensing I/O & Interoperability Foundation 5.0

- Status: Accepted (Remote Sensing I/O, Sensor Product & Interoperability
  Foundation 5.0 goal)
- Context: master's data plane (DataManager, SourceProvider registry,
  `RasterStructure`/`VectorStructure`, remote handle pool, virtual raster
  recipes) answers "can this file open". Professional remote-sensing work needs
  the next four questions answered as library contracts: *what is this
  resource* (format/product), *what does it mean* (sensor/band/time/CRS),
  *how can it be read safely* (window/block/bounded), and *how can it leave the
  system* (conversion/export without silent loss). The unmerged I/O Foundation
  4.0 prototype (`zcode/geospatial-io-foundation-4`) proved a Qt-free
  `src/geospatial` core against a nine-commits-older master.
- Decision:
  1. **One Qt-free foundation library**: `src/geospatial` (target
     `sicnu_geospatial`) depends only on C++20, GDAL/OGR/OSR and jsoncpp. It is
     a *layer*, not an entry point: everything flows through the existing
     DataManager registration seam; no UI, no cartography, no experiment
     dependencies (downward-only).
  2. **Canonical metadata is the single vocabulary**: every adapter
     (raster/vector/multidim/product/STAC) maps into and out of
     `metadata/canonical_metadata.*`; no consumer re-parses GDAL metadata.
     Absence is encoded (`has*` flags), never fabricated — an unknown value is
     empty/`has*=false`, not a guess.
  3. **Wrap, don't rewrite**: GDAL/OGR/PROJ stay the engines. The layer owns
     policy (CRS resolution, NoData/scale-offset fidelity, atomicity,
     capability certification) and contracts (reader/writer/probe), not
     geometry or projection math.
  4. **Bounded everything**: window reads are the default access; whole-raster
     reads are explicit (`readFull`) with a byte budget; feature streams are
     batched; multidim slices declare a max-cells budget; remote fetches carry
     timeouts and size bounds.
  5. **Atomic publication**: writers stage beside the target, fsync, validate
     by reopen, then publish (single file: transactional rename; dataset
     groups: sidecars first, main file last). `cancel()` removes staging.
- Consequences: algorithms/UI/Pi gain one metadata + reader vocabulary;
  Windows/Linux paths are exercisable without Qt; the 4.0 prototype landed on
  current master as the base and is extended (URI model, probe contract,
  capability model, product registry, remote range harness, export
  negotiation) rather than rewritten; `src/geospatial` adds a new static
  library to every build (small, Qt-free).
