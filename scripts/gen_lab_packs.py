#!/usr/bin/env python3
# scripts/gen_lab_packs.py — author/regenerate the sicnu.lab-pack/1 manifests.
#
# One pack per lab under data/labs/packs/<lab_id>.pack.json plus the grading
# corpus pack. Deterministic: identical repo state produces byte-identical
# packs (sorted keys off, stable input order, checksums computed from the
# committed fixture files).
#
# Input provenance tiers (see src/agent/lab_data_pack.h):
#   committed-fixture  sha256+bytes pinned from the working tree (hard checks)
#   generated-samples  data/samples/* from sicnu_generate_samples — sizes are
#                      toolchain-dependent, so no bytes field is ever emitted
#                      (soft checks)
#   generated-tmp      data/labs/_tmp/* from gen_lab_fixtures.py (soft checks,
#                      no bytes field either)
#
# Regeneration is machine-independent: whether or not the generated inputs
# happen to exist locally never changes the output bytes.
#
# Usage: python3 scripts/gen_lab_packs.py [--samples-dir data/samples]
#   Writes/refreshes data/labs/packs/*.pack.json. Run from the repo root.

import argparse
import hashlib
import json
import os
import sys

SCHEMA = "sicnu.lab-pack/1"
PACKS_DIR = os.path.join("data", "labs", "packs")
FIXTURES_DIR = os.path.join("tests", "fixtures", "lab")

LANDSAT_TRUTH = (
    "synthetic 7-band Landsat-like (OLI band order); GDAL bands 1..7 = "
    "coastal/blue/green/red/nir/swir1/swir2; 256x256; fixed-seed surface model"
)
DEM_TRUTH = "synthetic DEM 256x256, float32, metres, EPSG:4326-like grid"
CHANGE_TRUTH = (
    "synthetic before/after pair 256x256; one added dark region in 'after' "
    "is the change signal"
)
LANDCOVER_TRUTH = (
    "32x32 Byte class map, classes 1..4 by row bands (rows 0-7/8-23/24-27/"
    "28-31), truth-NoData block rows 16-17 x cols 0-15; EPSG:4326, 0.001 deg "
    "pixels, origin (100, 40), nodata 0; classified stand-in merges class 3 "
    "into class 2 (closed-form OA 864/992)"
)
LANDCOVER_GENERATOR = (
    "python3 scripts/gen_lab_fixtures.py landcover --out data/labs/_tmp --seed 42"
)


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def committed(rel):
    """committed-fixture entry with live checksum. Committed fixtures are
    git-tracked: a missing one is a broken checkout, and silently dropping
    the entry would make the pack a function of the local tree again —
    fail loudly instead."""
    if not os.path.isfile(rel):
        print(f"gen_lab_packs: committed fixture missing: {rel}")
        sys.exit(1)
    return {
        "path": rel.replace(os.sep, "/"),
        "role": "fixture",
        "provenance": "committed-fixture",
        "sha256": sha256_file(rel),
        "bytes": os.path.getsize(rel),
    }


def generated(rel, role, sensor_truth, generator, notes=""):
    # No `bytes` pin: generated sizes drift with the local GDAL/toolchain, and
    # emitting one only when the file happens to exist would make the pack a
    # function of the machine instead of the repo (the header promises the
    # opposite). The verifier treats absent bytes as "no size check".
    entry = {
        "path": rel.replace(os.sep, "/"),
        "role": role,
        "provenance": "generated-samples",
        "generator": generator,
        "sensor_truth": sensor_truth,
    }
    if notes:
        entry["notes"] = notes
    return entry


def generated_tmp(rel, role, sensor_truth, generator, notes=""):
    entry = {
        "path": rel.replace(os.sep, "/"),
        "role": role,
        "provenance": "generated-tmp",
        "generator": generator,
        "sensor_truth": sensor_truth,
    }
    if notes:
        entry["notes"] = notes
    return entry


SAMPLES_GENERATOR = "sicnu_generate_samples <data-dir>"
TEMPORAL_GENERATOR = (
    "python3 scripts/gen_lab_fixtures.py temporal --out data/labs/_tmp --seed 42"
)
SAR_GENERATOR = "python3 scripts/gen_lab_fixtures.py sar --out data/labs/_tmp --seed 42"
HYPER_GENERATOR = (
    "python3 scripts/gen_lab_fixtures.py hyperspectral --out data/labs/_tmp --seed 42"
)

TEMPORAL_TRUTH = (
    "12 monthly 256x256 float32 scenes, red=band3 nir=band4, reflectance "
    "[0,1] quantised to 1/255, SICNU_ACQUISITION_DATE per scene; zones.tif "
    "codes 1=evergreen 2=cropland 3=water 4=disturbance"
)
SAR_TRUTH = (
    "C-bandlike sigma0 DN scenes 256x256; before/after pair plus 6-scene "
    "stack; DEM in metres; change_groundtruth codes 0=no 1=change"
)
HYPER_TRUTH = (
    "hyperspectral scene 256x256 with N bands in the VNIR range, reflectance "
    "[0,1]; hsi_zones.tif carries the class-truth codes used by grading"
)


def classic_lab(lab_id, prerequisites, note=""):
    entries = []
    for rel, truth in prerequisites:
        entries.append(generated(rel, "sample", truth, SAMPLES_GENERATOR, note))
    return base_pack(lab_id, entries)


def base_pack(lab_id, entries, generator_note=""):
    committed_bytes = sum(e["bytes"] for e in entries if e["provenance"] == "committed-fixture")
    pack = {
        "schema_version": SCHEMA,
        "lab_id": lab_id,
        "pack_version": "1.0.0",
        "license": "generated-in-repo (synthetic teaching data, no third-party rights)",
    }
    if committed_bytes:
        pack["declared_offline_bytes"] = committed_bytes
    if generator_note:
        pack["notes"] = generator_note
    pack["inputs"] = entries
    return pack


def temporal_entries():
    entries = []
    dates = [
        "2024-01-15", "2024-02-15", "2024-03-15", "2024-04-15", "2024-05-15",
        "2024-06-15", "2024-07-15", "2024-08-15", "2024-09-15", "2024-10-15",
        "2024-11-15", "2024-12-15",
    ]
    for date in dates:
        entries.append(
            generated_tmp(
                f"data/labs/_tmp/temporal/scene_{date}.tif",
                "sample",
                TEMPORAL_TRUTH,
                TEMPORAL_GENERATOR,
            )
        )
    entries.append(
        generated_tmp(
            "data/labs/_tmp/temporal/zones.tif",
            "truth",
            TEMPORAL_TRUTH,
            TEMPORAL_GENERATOR,
            "grading aux; not in the student chain",
        )
    )
    return entries


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--samples-dir", default=os.path.join("data", "samples"))
    parser.add_argument(
        "--check",
        action="store_true",
        help="verify committed packs match regeneration (zero diff) instead of writing",
    )
    args = parser.parse_args()

    samples = args.samples_dir.replace(os.sep, "/")
    packs = {}

    landsat = [(os.path.join(samples, "landsat_sample.tif"), LANDSAT_TRUTH)]
    change_pair = [
        (os.path.join(samples, "change_before.tif"), CHANGE_TRUTH),
        (os.path.join(samples, "change_after.tif"), CHANGE_TRUTH),
    ]

    packs.update(
        {
            "lab01_image_enhancement": classic_lab("lab01_image_enhancement", landsat),
            "lab02_spectral_analysis": classic_lab("lab02_spectral_analysis", landsat),
            "lab03_classification": classic_lab(
                "lab03_classification",
                landsat
                + [(os.path.join(samples, "training_samples.shp"), "training ROI polygons (ESRI shapefile) with a class field")],
            ),
            "lab04_change_detection": classic_lab("lab04_change_detection", change_pair),
            "lab05_terrain_analysis": classic_lab(
                "lab05_terrain_analysis", [(os.path.join(samples, "dem_sample.tif"), DEM_TRUTH)]
            ),
            "lab06_georeferencing": classic_lab("lab06_georeferencing", landsat),
            "lab07_image_fusion": classic_lab(
                "lab07_image_fusion", landsat,
                "same sample acts as pan + MS input (teaching demo)",
            ),
            "lab08_atmospheric_correction": classic_lab("lab08_atmospheric_correction", landsat),
            "lab09_pca_analysis": classic_lab("lab09_pca_analysis", landsat),
            "lab10_mosaic": classic_lab("lab10_mosaic", change_pair),
            "lab11_obia_classification": classic_lab("lab11_obia_classification", landsat),
            # RS14 curriculum additions.
            "lab15_data_inspection": classic_lab("lab15_data_inspection", landsat),
            "lab16_accuracy_assessment": base_pack(
                "lab16_accuracy_assessment",
                [
                    generated_tmp(
                        "data/labs/_tmp/landcover/landcover_classified.tif",
                        "sample", LANDCOVER_TRUTH, LANDCOVER_GENERATOR),
                    generated_tmp(
                        "data/labs/_tmp/landcover/landcover_truth.tif",
                        "truth", LANDCOVER_TRUTH, LANDCOVER_GENERATOR,
                        "grading-adjacent truth twin; the pinned grading oracle "
                        "is the committed tests/fixtures/lab/landcover_truth.tif"),
                ],
            ),
        }
    )

    # Labspec labs (sicnu.labspec.v1): tmp fixtures from gen_lab_fixtures.py.
    temporal = temporal_entries()
    packs["temporal_analysis"] = base_pack(
        "temporal_analysis", temporal,
        "inputs regenerate via scripts/gen_lab_fixtures.py; sizes drift with toolchain (soft)",
    )
    # D16 timeline studio consumes the same temporal stack.
    packs["temporal_phenology_timeline"] = base_pack(
        "temporal_phenology_timeline", temporal,
        "shared with labspec temporal_analysis inputs (regenerated identically)",
    )

    sar = [
        generated_tmp("data/labs/_tmp/sar/sar_before_dn.tif", "sample", SAR_TRUTH, SAR_GENERATOR),
        generated_tmp("data/labs/_tmp/sar/sar_after_dn.tif", "sample", SAR_TRUTH, SAR_GENERATOR),
        generated_tmp("data/labs/_tmp/sar/sar_stack_dn.tif", "sample", SAR_TRUTH, SAR_GENERATOR),
        generated_tmp("data/labs/_tmp/sar/change_groundtruth.tif", "truth", SAR_TRUTH, SAR_GENERATOR),
        generated_tmp("data/labs/_tmp/sar/dem.tif", "aux", DEM_TRUTH, SAR_GENERATOR),
    ]
    packs["sar_processing"] = base_pack("sar_processing", sar)

    hyper = [
        generated_tmp("data/labs/_tmp/hyperspectral/hsi_scene.tif", "sample", HYPER_TRUTH, HYPER_GENERATOR),
        generated_tmp("data/labs/_tmp/hyperspectral/hsi_zones.tif", "truth", HYPER_TRUTH, HYPER_GENERATOR),
    ]
    lib = committed(os.path.join("data", "labs", "spectral-library", "lab10_sicnu_library.json"))
    if lib:
        lib["role"] = "aux"
        lib["sensor_truth"] = "SICNU spectral library (ADR 0081) — class endmembers"
        hyper.append(lib)
    mspec = committed(os.path.join("data", "labs", "mapspecs", "lab11_thematic_map.mapspec.json"))
    packs["hyperspectral_analysis"] = base_pack("hyperspectral_analysis", hyper)

    carto = list(temporal)
    if mspec:
        mspec = dict(mspec)
        mspec["role"] = "aux"
        mspec["sensor_truth"] = "map layout spec (thematic map composition)"
        carto.append(mspec)
    packs["cartographic_mapping"] = base_pack("cartographic_mapping", carto)

    # The grading corpus is its own deployment unit: every committed fixture
    # and corpus manifest, checksum-pinned.
    corpus = []
    if os.path.isdir(FIXTURES_DIR):
        for name in sorted(os.listdir(FIXTURES_DIR)):
            rel = os.path.join(FIXTURES_DIR, name)
            if not os.path.isfile(rel):
                continue
            entry = committed(rel)
            if not entry:
                continue
            if name.endswith(".tif") or name.endswith(".png"):
                entry["role"] = "fixture"
                entry["sensor_truth"] = (
                    "closed-form deterministic scene (no RNG, no timestamps)"
                )
                corpus.append(entry)
            elif name.endswith(".mapspec.json"):
                entry["role"] = "fixture"
                entry["sensor_truth"] = "MapSpec v5 document fixture (corpus input)"
                corpus.append(entry)
            elif name.endswith(".json"):
                entry["role"] = "aux"
                entry["sensor_truth"] = "corpus manifest (declared expected scores)"
                corpus.append(entry)
    if corpus:
        packs["grading_corpus"] = base_pack(
            "grading_corpus", corpus,
            "fixtures regenerate via tests/fixtures/lab/generate_fixtures.py (osgeo host)",
        )

    os.makedirs(PACKS_DIR, exist_ok=True)
    drifted = []
    for lab_id in sorted(packs):
        out = os.path.join(PACKS_DIR, f"{lab_id}.pack.json")
        text = json.dumps(packs[lab_id], indent=2, ensure_ascii=False) + "\n"
        if args.check:
            with open(out, "r", encoding="utf-8") as handle:
                if handle.read() != text:
                    drifted.append(out)
            continue
        with open(out, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(text)
        print(f"wrote {out}")
    if args.check:
        if drifted:
            for path in drifted:
                print(f"DRIFT {path}")
            import sys

            sys.exit(1)
        print("packs in sync")


if __name__ == "__main__":
    main()
