"""Deterministic GDAL golden master for the P0 parity gate.
Renders LE7 band 4 (NIR) to a fixed 512x512 PNG in the source CRS with a
fixed 2-98 percentile stretch. The C++ path (Task 11) must match within RMSE.
"""
import json, os, subprocess, sys
import numpy as np
from osgeo import gdal

SRC = "data/LE7/LE71300411999327EDC00_B4.TIF"
OUT_PNG = "tests/golden/le7_b4_ref.png"
OUT_JSON = "tests/golden/le7_b4_ref.json"
SIZE = 512

def main():
    gdal.UseExceptions()
    ds = gdal.Open(SRC)
    band = ds.GetRasterBand(1)
    arr = band.ReadAsArray().astype(np.float64)
    lo, hi = np.percentile(arr[arr > 0], [2, 98])
    warped = gdal.Warp("", ds, format="MEM", width=SIZE, height=SIZE,
                       resampleAlg="bilinear")
    w = warped.GetRasterBand(1).ReadAsArray().astype(np.float64)
    wb = np.clip((w - lo) / (hi - lo) * 255.0, 0, 255).astype(np.uint8)
    mem_ds = gdal.GetDriverByName("MEM").Create("", SIZE, SIZE, 1, gdal.GDT_Byte)
    mem_ds.GetRasterBand(1).WriteArray(wb)
    gdal.GetDriverByName("PNG").CreateCopy(OUT_PNG, mem_ds)
    meta = {"src": SRC, "size": SIZE, "stretch_lo": lo, "stretch_hi": hi,
            "crs": ds.GetProjection()[:40], "mean": float(wb.mean())}
    with open(OUT_JSON, "w") as f:
        json.dump(meta, f, indent=2)
    print("wrote", OUT_PNG, meta)

if __name__ == "__main__":
    main()
