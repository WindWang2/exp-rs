# BUNDLE.md — offline lab sample bundle layout (spec for D7)

This is the directory layout the offline deployment package (D7) zips. The
foundry (goal D1) produces everything below **except** the zip itself; D7 owns
zipping, versioning and the installer seam. No zip logic lives in
`sicnu_generate_samples` or `scripts/gen_samples.*`.

## Layout

```
<bundle>/
├── data/samples/
│   ├── landsat_sample.tif        # 7-band optical, SICNU_BAND_ROLE metadata
│   ├── dem_sample.tif            # DEM, Float32 meters
│   ├── change_before.tif         # change pair
│   ├── change_after.tif
│   ├── training_samples.shp      # + .dbf/.shx/.prj (ROI polygons)
│   ├── landsat_truth.tif         # ground-truth companions (grading)
│   ├── change_truth.tif
│   ├── dem_slope_truth.tif
│   ├── dem_aspect_truth.tif
│   └── manifest.json             # SHA-256 fingerprints + seed + GDAL version
├── tools/sicnu_generate_samples  # the generator binary (regeneration path)
└── BUNDLE.md                     # this spec, shipped inside the bundle
```

## Rules

1. **manifest.json is the integrity anchor.** Installers and lab machines run
   `sicnu_generate_samples --out=<dir> --verify`; exit 0 means the bundle is
   intact. Exit 4 = drift → re-extract or regenerate.
2. **Regeneration instead of distribution is allowed**: `tools/` ships the
   generator, so a bundle can omit `data/samples/` entirely and generate on
   first use (`scripts/gen_samples.sh|.cmd`). Because generation is
   deterministic (same host GDAL build ⇒ byte-identical outputs), both paths
   yield equivalent data; cross-host GDAL builds may differ byte-wise while
   being scientifically identical (documented in `docs/datasets/lab-samples.md`).
3. **Rasters are never tracked in git**; the bundle is built from a generated
   working tree, not from the repository.
4. `data/samples/manifest.json` MAY be committed by the release manager as the
   blessed reference for a release (it is un-ignored in `.gitignore` for
   exactly that purpose); the template
   `data/samples/manifest.template.json` documents the schema.
5. Profiles: ship `--profile=lab` bundles for teaching; `--profile=stress`
   (2048²) bundles are for the benchmark/D4 grading lanes, ~10× the pixels.

## D7 checklist

- [ ] run `scripts/gen_samples.sh` in a clean tree, `--verify` green
- [ ] copy the layout above, include the generator binary for the target OS
- [ ] zip; record zip SHA-256 in the release notes (outside this track)
- [ ] installer restores `data/samples/` under the app data root (D7 seam;
      no first-run auto-generation inside `src/` — epic default #2)
