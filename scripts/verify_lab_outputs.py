#!/usr/bin/env python3
# scripts/verify_lab_outputs.py — D3 lab track headless verification.
#
# Re-derives the grading-intent assertions (data/labs/grading/*.intent.json)
# against the pipeline outputs under data/labs/_tmp/out/ and prints a
# PASS/FAIL table for EVIDENCE.md. Zone statistics use the aux zoning rasters
# produced by scripts/gen_lab_fixtures.py.
#
# Usage: python3 scripts/verify_lab_outputs.py [--lab all|temporal|sar|hyperspectral|cartography]

import argparse
import json
import math
import os
import sys

import numpy as np
from osgeo import gdal

gdal.UseExceptions()

TMP = "data/labs/_tmp"
results = []


def band(path, idx=1):
    ds = gdal.Open(path)
    arr = ds.GetRasterBand(idx).ReadAsArray().astype(np.float64)
    ds = None
    return arr


def zones_of(path):
    ds = gdal.Open(path)
    arr = ds.GetRasterBand(1).ReadAsArray()
    ds = None
    return arr


def check(lab, assertion_id, ok, detail):
    results.append((lab, assertion_id, "PASS" if ok else "FAIL", detail))
    return ok


def close(v, lo, hi):
    return lo <= v <= hi


def verify_temporal():
    lab = "temporal"
    out = os.path.join(TMP, "out/lab8")
    zones = zones_of(os.path.join(TMP, "temporal/zones.tif"))

    ndvi_path = os.path.join(out, "ndvi_series.tif")
    ds = gdal.Open(ndvi_path)
    n = ds.RasterCount
    ndvi = np.stack([ds.GetRasterBand(i).ReadAsArray().astype(np.float64)
                     for i in range(1, n + 1)])
    ds = None
    valid = [np.isfinite(b).mean() for b in ndvi]
    check(lab, "T1_series_stack", n == 12 and min(valid) >= 0.99,
          f"bands={n} min_valid_frac={min(valid):.4f}")

    forest = np.nanmean(ndvi[:, zones == 1], axis=1)
    water = np.nanmean(ndvi[:, zones == 3], axis=1)
    crop = np.nanmean(ndvi[:, zones == 2], axis=1)
    ok = (close(forest.min(), 0.65, 0.85) and close(forest.max(), 0.65, 0.85)
          and close(water.min(), -0.20, 0.0) and close(water.max(), -0.20, 0.0)
          and crop[5:8].min() >= 0.60 and max(crop[11], crop[0], crop[1]) <= 0.30)
    check(lab, "T2_zone_ndvi_means", ok,
          f"forest[{forest.min():.3f},{forest.max():.3f}] water[{water.min():.3f},{water.max():.3f}] "
          f"crop_summer_min={crop[5:8].min():.3f} crop_winter_max={max(crop[11], crop[0], crop[1]):.3f}")

    tr = band(os.path.join(out, "ndvi_trend.tif"), 1)   # slope per day
    r2 = band(os.path.join(out, "ndvi_trend.tif"), 3)
    dslope, fr, wr = (tr[zones == 4].mean(), abs(np.nanmean(tr[zones == 1])),
                      abs(np.nanmean(tr[zones == 3])))
    contrast = abs(dslope) - fr  # disturbance-magnitude below-forest margin
    ok = dslope <= -3e-4 and fr <= 2e-4 and wr <= 2e-4 and contrast >= 2e-4
    check(lab, "T3_trend_slope", ok,
          f"disturb_slope={dslope:.6f}/day forest={fr:.6f} water={wr:.6f} contrast={contrast:.6f} "
          f"(r2_dist={np.nanmean(r2[zones == 4]):.3f} informational, seasonal variance dominates)")

    ph = os.path.join(out, "phenology.tif")
    sos, pos, eos, los, amp = (band(ph, i) for i in (1, 2, 3, 4, 5))
    crop_sos = np.nanmean(sos[zones == 2]); crop_pos = np.nanmean(pos[zones == 2])
    crop_eos = np.nanmean(eos[zones == 2]); crop_los = np.nanmean(los[zones == 2])
    low_amp = max(np.nanmean(amp[zones == 1]), np.nanmean(amp[zones == 3]))
    ok = (close(crop_sos, 70, 170) and close(crop_pos, 170, 230)
          and close(crop_eos, 230, 330) and close(crop_los, 100, 250)
          and low_amp < 0.15)
    check(lab, "T4_phenology_cropland", ok,
          f"sos={crop_sos:.0f} pos={crop_pos:.0f} eos={crop_eos:.0f} los={crop_los:.0f} "
          f"forest/water_amp={low_amp:.3f} (degenerate, not NoData)")

    an = band(os.path.join(out, "anomaly_2024-10-15.tif"), 1)
    dmean, fmean, wmean = (np.nanmean(an[zones == 4]), abs(np.nanmean(an[zones == 1])),
                           abs(np.nanmean(an[zones == 3])))
    check(lab, "T5_anomaly_disturbed_date", dmean <= -2.0 and fmean <= 1.0 and wmean <= 1.0,
          f"disturb={dmean:.2f} forest={fmean:.2f} water={wmean:.2f}")

    an2 = band(os.path.join(out, "anomaly_2024-08-15_control.tif"), 1)
    # Control at the seasonal peak yields a small POSITIVE z (season effect);
    # the assertion is robust statistics: no coherent negative anomaly —
    # medians and z<=-2 fractions, not means (baseline sigma->0 quantized
    # pixels blow z up; the operator NoDatas only sigma==0).
    patch_med = np.nanmedian(an2[zones == 4])
    patch_frac = (an2[zones == 4] <= -2).mean()
    global_frac = (an2 <= -2).mean()
    fmed = abs(np.nanmedian(an2[zones == 1]))
    wmed = abs(np.nanmedian(an2[zones == 3]))
    ok = (patch_med >= -1.0 and patch_frac <= 0.01 and global_frac <= 0.05
          and fmed <= 1.0 and wmed <= 1.0)
    check(lab, "T6_anomaly_control_date", ok,
          f"patch_median={patch_med:.2f} patch_frac(z<=-2)={patch_frac:.4f} "
          f"global_frac={global_frac:.4f} forest_med={np.nanmedian(an2[zones == 1]):.2f} "
          f"water_med={np.nanmedian(an2[zones == 3]):.2f}")

    cnt = band(os.path.join(out, "nir_summary.tif"), 1)
    check(lab, "T7_summary_health", np.all(cnt == 12), f"count uniq={np.unique(cnt)}")


def verify_sar():
    lab = "sar"
    out = os.path.join(TMP, "out/lab9")
    zones = zones_of(os.path.join(TMP, "temporal/zones.tif"))  # same grid/layout

    s0 = band(os.path.join(out, "sigma0_before.tif"))
    wm, fm = s0[:, :25].mean(), s0[zones == 1].mean()
    check(lab, "S1_calibration", close(wm, 0.005, 0.008) and close(fm, 0.020, 0.032) and s0.min() >= 0,
          f"water={wm:.5f} forest={fm:.5f} min={s0.min():.6f}")

    def enl(a, zone_mask):
        v = a[zone_mask]
        return v.mean() ** 2 / v.var()
    raw = s0[:, :25]
    lee = band(os.path.join(out, "sigma0_before_lee5.tif"))
    leew = lee[:, :25]
    gain = enl(leew, np.ones(leew.shape, bool)) / enl(raw, np.ones(raw.shape, bool))
    drift = abs(leew.mean() / raw.mean() - 1.0)
    check(lab, "S2_despeckle_enl", gain >= 1.5 and drift <= 0.10,
          f"enl_gain={gain:.2f} mean_drift={drift:.3%}")

    gt = zones_of(os.path.join(TMP, "sar/change_groundtruth.tif"))
    ch = band(os.path.join(out, "change_otsu.tif"))
    patch = gt == 1
    det = (ch[patch] == 1).mean()
    outside = (ch[gt == 0] == 1).mean()
    check(lab, "S3_change_detection", det >= 0.65 and outside <= 0.02,
          f"detection={det:.2%} false_alarm={outside:.3%}")

    ds = gdal.Open(os.path.join(out, "stack_lee_allbands.tif"))
    nb = ds.RasterCount
    b1 = ds.GetRasterBand(1).ReadAsArray()
    b2 = ds.GetRasterBand(2).ReadAsArray()
    ds = None
    hole_nan = np.isnan(b2[:20, :20]).all() if nb >= 2 else False
    v1 = np.isfinite(b1).sum()
    v2 = np.isfinite(b2).sum() if nb >= 2 else -1
    check(lab, "S4_speckle_allbands_nodata",
          nb == 2 and hole_nan and v1 == 65536 and v2 == 65136,
          f"bands={nb} band1_valid={v1} band2_valid={v2} hole_all_nan={hole_nan}")

    ds = gdal.Open(os.path.join(out, "gamma0_before.tif"))
    nb = ds.RasterCount
    g0 = ds.GetRasterBand(1).ReadAsArray()
    ds = None
    ratio = np.nanmean(g0[:, :25]) / raw.mean()
    check(lab, "S5_terrain_correction_geometry",
          nb >= 2 and np.isfinite(g0).mean() >= 0.98 and close(ratio, 0.5, 3.5),
          f"bands={nb} valid={np.isfinite(g0).mean():.4f} water_gamma0_over_sigma0={ratio:.3f} "
          "(>1/cos35: fixture DEM carries real slopes)")


def verify_hyperspectral():
    lab = "hyperspectral"
    out = os.path.join(TMP, "out/lab10")
    zones = zones_of(os.path.join(TMP, "hyperspectral/hsi_zones.tif"))

    lib = json.load(open("data/labs/spectral-library/lab10_sicnu_library.json"))
    truth = [np.array(e["spectrum"]) for e in lib["entries"]]

    mnf = os.path.join(out, "mnf_components.tif")
    ds = gdal.Open(mnf); nb = ds.RasterCount
    c1 = ds.GetRasterBand(1).ReadAsArray().astype(np.float64)
    c4 = ds.GetRasterBand(nb).ReadAsArray().astype(np.float64)
    ds = None
    def fisher(a):
        means = [a[zones == z].mean() for z in (1, 2, 3)]
        within = np.mean([a[zones == z].var() for z in (1, 2, 3)])
        between = np.var(means)
        return between / max(within, 1e-9)
    ok = nb == 4 and fisher(c1) > fisher(c4)
    check(lab, "H1_mnf_ordering", ok, f"bands={nb} fisher_c1={fisher(c1):.2f} fisher_c4={fisher(c4):.2f}")

    # PPI result lives in the runner's step JSON; the run script exports it to a sidecar.
    ppi_path = os.path.join(out, "ppi_result.json")
    if os.path.exists(ppi_path):
        ppi = json.load(open(ppi_path))
        ends = [np.array(e) for e in ppi.get("endmembers", [])]
        def sam_deg(a, b):
            cos = float(np.dot(a, b) / (np.linalg.norm(a) * np.linalg.norm(b)))
            return math.degrees(math.acos(max(-1.0, min(1.0, cos))))
        angles = []
        for e in ends:
            angles.append(min(sam_deg(e, t) for t in truth))
        idx = ppi.get("indices", [])
        ok = (len(ends) == 3 and len(set(idx)) == 3
              and (sum(angles) / len(angles)) <= 15.0 and max(angles) <= 25.0)
        check(lab, "H2_ppi_endmembers", ok,
              f"angles={['%.2f' % a for a in angles]} (water endmember dominates angular noise) "
              f"indices_distinct={len(set(idx)) == 3}")
    else:
        check(lab, "H2_ppi_endmembers", False, f"{ppi_path} missing (run pipeline via run_lab_pipelines.sh)")

    labels = band(os.path.join(out, "sam_labels.tif"))
    acc = np.mean([(labels[zones == z] == z - 1).mean() for z in (1, 2, 3)])
    check(lab, "H3_sam_pure_zone_accuracy", acc >= 0.95, f"pure_zone_accuracy={acc:.3f}")

    sid = band(os.path.join(out, "sid_labels.tif"))
    acc_s = np.mean([(sid[zones == z] == z - 1).mean() for z in (1, 2, 3)])
    check(lab, "H4_sid_pure_zone_accuracy", acc_s >= 0.90, f"pure_zone_accuracy={acc_s:.3f}")

    ab = os.path.join(out, "abundances.tif")
    ds = gdal.Open(ab); nab = ds.RasterCount
    a = np.stack([ds.GetRasterBand(i).ReadAsArray().astype(np.float64) for i in range(1, nab + 1)])
    ds = None
    err = band(os.path.join(out, "unmix_error.tif"))
    own = [a[z - 1][zones == z].mean() for z in (1, 2, 3)]  # zone z ↔ endmember z-1
    s = a.sum(axis=0)
    mixed_water = a[0][zones == 4].mean()
    check(lab, "H5_unmixing_abundances",
          min(own) >= 0.9 and np.abs(s - 1).mean() <= 0.05
          and close(mixed_water, 0.1, 0.9) and np.nanmean(err) <= 0.02,
          f"own={['%.3f' % o for o in own]} sum_dev={np.abs(s - 1).mean():.4f} "
          f"mixed_water={mixed_water:.3f} mean_err={np.nanmean(err):.4f}")

    pipe = json.load(open("data/labs/pipelines/lab10_hyperspectral_analysis.pipeline.json"))
    refs_ok = True
    for step in pipe["steps"]:
        for key in ("refs", "endmembers"):
            if key in step.get("params", {}):
                refs_ok &= step["params"][key] == [list(t) for t in truth]
    check(lab, "H6_zero_drift_refs", refs_ok, "pipeline refs == library spectra")


def verify_cartography():
    lab = "cartography"
    out = os.path.join(TMP, "out/lab11")
    zones = zones_of(os.path.join(TMP, "temporal/zones.tif"))

    comp = band(os.path.join(out, "ndvi_composite_mean.tif"), 1)
    check(lab, "K1_composite_statistics",
          close(comp[zones == 1].mean(), 0.65, 0.85) and close(comp[zones == 3].mean(), -0.20, 0.0),
          f"forest={comp[zones == 1].mean():.3f} water={comp[zones == 3].mean():.3f}")

    ds = gdal.Open(os.path.join(out, "thematic_mask.tif"))
    m = ds.GetRasterBand(1).ReadAsArray()
    dt = ds.GetRasterBand(1).DataType
    ds = None
    uniq = np.unique(m)
    frac = (m == 1).mean()
    check(lab, "K2_thematic_mask",
          set(uniq.tolist()) <= {0, 1} and dt == gdal.GDT_Byte and close(frac, 0.80, 0.95),
          f"values={uniq.tolist()} dtype=GDT_Byte vegetated_fraction={frac:.3f} "
          "(Otsu separates water/low from vegetated)")

    stats_path = os.path.join(out, "threshold_stats.json")
    if os.path.exists(stats_path):
        st = json.load(open(stats_path))
        th = st.get("thresholdUsed", st.get("threshold"))
        check(lab, "K3_otsu_threshold_sanity", close(th, -0.3, 0.3),
              f"threshold={th} (valley between water negatives and vegetated means) "
              f"stats={ {k: st[k] for k in list(st)[:4]} }")
    else:
        check(lab, "K3_otsu_threshold_sanity", False,
              f"{stats_path} missing (captured by run_lab_pipelines.sh)")

    doc = json.load(open("data/labs/mapspecs/lab11_thematic_map.mapspec.json"))
    five = all(doc.get(c) for c in ("titles", "legends", "scale_bars", "north_arrows", "source_notes"))
    check(lab, "K4_mapspec_validation", doc["spec_version"] == 5 and five and bool(doc["map_frames"]),
          f"spec_version={doc['spec_version']} five_elements={five} (validate/preflight 由 lab 链测试或 MCP 执行)")

    png = os.path.join(out, "map/lab11_thematic_map.png")
    if os.path.exists(png):
        size = os.path.getsize(png)
        check(lab, "K5_map_export", size >= 10000, f"png_bytes={size}")
    else:
        check(lab, "K5_map_export", False, f"{png} missing (导出走 MCP 脚本/实验室链测试)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lab", default="all",
                    choices=["all", "temporal", "sar", "hyperspectral", "cartography"])
    args = ap.parse_args()
    table = {"temporal": verify_temporal, "sar": verify_sar,
             "hyperspectral": verify_hyperspectral, "cartography": verify_cartography}
    order = table.keys() if args.lab == "all" else [args.lab]
    for name in order:
        table[name]()
    print(f"{'lab':<14} {'assertion':<32} {'result':<6} detail")
    fails = 0
    for lab, aid, res, detail in results:
        fails += res == "FAIL"
        print(f"{lab:<14} {aid:<32} {res:<6} {detail}")
    print(f"\n{len(results) - fails}/{len(results)} assertions passed, {fails} failed")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
