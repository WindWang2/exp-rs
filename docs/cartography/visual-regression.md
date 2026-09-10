# Visual Regression Methodology (Cartography Design System 4.0)

Functional JSON assertions cannot see drawing defects. The cartography
harness (`tests/test_cartography_visual.cpp`) renders a deterministic
benchmark set through the real pipeline and asserts in three independent
layers:

```
fixture spec → solver/repair → MapSpecCompiler → QgsPrintLayout
            → QgsLayoutExporter → QImage → assertions
```

## The benchmark set (≥ 8 scenes, drift-checked)

`classification`, `change-before-after`, `time-series-phenology`,
`sar-backscatter`, `scientific-publication`, `multi-panel` (from shipped
templates), plus two purpose-built scenes: `dense-legend` (30 declared
entries in a tight legend) and `cjk-title` (long full-width CJK title).
Every scene must repair to a passing preflight in ≤ 4 bounded iterations.

## Assertion layers

1. **Determinism** — the same spec renders twice in the same process; the
   PNG bytes must hash identically (SHA-256). Any nondeterminism (thread
   ordering, uninitialized state, time-dependent content) fails here.
2. **Geometry contracts** — asserted separately from pixels: every compiled
   item sits inside the page bounds, ≥ 5 furniture items materialize, and
   `extract()` round-trips to a valid MapSpec.
3. **Golden pixel references (opt-in)** — set
   `SICNU_CARTOGRAPHY_GOLDEN_DIR=<dir>`; missing references are written on
   first run. Comparisons downscale to 25% with smooth filtering and
   require mean absolute channel difference < 12 (0–255): layout drift
   fails, font anti-aliasing differences pass. Size changes refresh the
   reference (recorded via test warnings) instead of failing forever.

## Why goldens stay out of the repository

- Font environments differ across platforms/CI images; committing exact
  rasters would flake or force `QT_QPA_PLATFORM`-pinned rendering.
- Determinism + geometry layers already catch the defects JSON tests miss
  (missing furniture, misplaced items, broken z-order, blank renders).
- When a human wants pixel baselines, the golden directory provides them
  without repository bloat (25% scale, ~30–80 KB per reference).

## Extending the harness

Add a fixture to `benchmarkFixtures()` (template id or built spec) and list
it in the required-names check. Keep fixtures data-free: map frames carry
extents, charts carry inline sample data — no raster dependencies, no
network, no QGIS style database.

## Running

```bash
QT_QPA_PLATFORM=offscreen LD_LIBRARY_PATH=/usr/lib \
  ctest --test-dir build -R test_mapspec --output-on-failure

# with golden references (writes/compares out-of-tree):
SICNU_CARTOGRAPHY_GOLDEN_DIR=/tmp/cartography-goldens ctest --test-dir build -R test_mapspec
```

## Platform 7.0 — rendering-free structural hashes

`cartography::structuralDigest(spec)` computes a SHA-256 over the resolved
geometry in canonical form: page envelope plus id-sorted
`collection/id:x,y,w,h[,pN][,zN]` entries with coordinates rounded to
0.01 mm. It is pure and platform-independent — the same resolved layout
produces the same digest everywhere, with no `QgsLayoutExporter` in the
loop.

Use it for:

- **known-answer layouts** — a fixture's digest is pinned in the test suite
  (update only with re-verified geometry); 1 mm of movement flips it;
- **compile determinism** — resolve the same spec twice, compare digests;
- **catalog-wide regression** — digest every fixture before and after a
  solver/compiler change and diff, without any rendering.

The PNG layers above remain the pixel-level contract where the environment
supports them; the structural digest is the honest, always-runnable
fallback that catches layout drift those layers would have caught. Known
environmental limitation: on this headless Windows setup the
`[visual][determinism]`/`[visual][golden]` PNG cases crash in
`QgsLayoutExporter` (DLL-resolution class, 0xC0000135) — they are reported
as not-exercised locally, never as passed.
