#!/usr/bin/env python3
# scripts/gen_lab_fixtures.py — D3 lab track local verification fixtures.
#
# Generates deterministic synthetic rasters under data/labs/_tmp/ (gitignored,
# deleted before the PR — never commit generated rasters). The committed
# products of this track remain the *data requirement specs* in
# data/labs/data-specs/, consumed by D1; this script only exists so the four
# lab pipelines can be executed headlessly for local evidence.
#
# Resource caps honored (track Autonomy default 5): temporal stacks are
# ≤12 epochs, ≤512×512, float32 with 8-bit (1/255) value quantization.
#
# Usage: python3 scripts/gen_lab_fixtures.py <lab> --out <dir> [--seed 42]
#   lab: temporal | sar | hyperspectral | all-temporal-themes

import argparse
import math
import os
import sys

try:
    from osgeo import gdal, osr
except ImportError:  # pragma: no cover
    sys.exit("GDAL Python bindings required: python3 -c 'from osgeo import gdal'")

gdal.UseExceptions()

WIDTH, HEIGHT = 256, 256
ORIGIN_X, ORIGIN_Y = 102.5, 30.5
PIXEL = 0.001

TEMPORAL_DATES = [
    "2024-01-15", "2024-02-15", "2024-03-15", "2024-04-15",
    "2024-05-15", "2024-06-15", "2024-07-15", "2024-08-15",
    "2024-09-15", "2024-10-15", "2024-11-15", "2024-12-15",
]


def day_of_year(date_str):
    from datetime import date
    y, m, d = (int(p) for p in date_str.split("-"))
    return date(y, m, d).timetuple().tm_yday


class Rng:
    """Deterministic uniform/normal RNG (xorshift, no external deps)."""

    def __init__(self, seed):
        self.state = seed & 0xFFFFFFFF or 0x9E3779B9

    def next_u32(self):
        x = self.state
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        self.state = x
        return x

    def uniform(self):
        return self.next_u32() / 4294967296.0

    def normal(self):
        # Box-Muller
        u1 = max(self.uniform(), 1e-12)
        u2 = self.uniform()
        return math.sqrt(-2.0 * math.log(u1)) * math.cos(2.0 * math.pi * u2)


def quantize8(v):
    """8-bit-scaled float32: reflectance quantized to 1/255 steps."""
    return round(max(0.0, min(1.0, v)) * 255.0) / 255.0


def write_gtiff(path, bands_data, dates=None, band_roles=None, dtype=gdal.GDT_Float32):
    drv = gdal.GetDriverByName("GTiff")
    nb = len(bands_data)
    ds = drv.Create(path, WIDTH, HEIGHT, nb, dtype,
                    options=["COMPRESS=DEFLATE", "TILED=NO"])
    ds.SetGeoTransform((ORIGIN_X, PIXEL, 0.0, ORIGIN_Y, 0.0, -PIXEL))
    srs = osr.SpatialReference()
    srs.ImportFromEPSG(4326)
    ds.SetProjection(srs.ExportToWkt())
    if dates:
        ds.SetMetadataItem("SICNU_ACQUISITION_DATE", dates)
    for i, plane in enumerate(bands_data, start=1):
        band = ds.GetRasterBand(i)
        band.WriteArray(plane)
        if band_roles and i in band_roles:
            band.SetMetadataItem("SICNU_BAND_ROLE", band_roles[i])
    ds.FlushCache()
    ds = None


def make_zone_masks():
    """Class-code plane: 1 forest, 2 cropland, 3 water, 4 disturbance, 5 soil."""
    codes = [[5] * WIDTH for _ in range(HEIGHT)]
    for y in range(HEIGHT):
        for x in range(WIDTH):
            if x < 25:
                codes[y][x] = 3            # water strip (west)
            elif y < 100:
                codes[y][x] = 1            # evergreen forest (north)
            else:
                codes[y][x] = 2            # cropland (south block)
    for y in range(130, 194):
        for x in range(60, 108):
            codes[y][x] = 4                # disturbance patch inside cropland
    return codes


def cropland_ndvi(doy):
    """Unimodal seasonal curve: base 0.15, peak 0.80 at DOY 200 (sigma 75 d)."""
    return 0.15 + 0.65 * math.exp(-0.5 * ((doy - 200) / 75.0) ** 2)


def gen_temporal(out_dir, seed):
    rng = Rng(seed)
    os.makedirs(out_dir, exist_ok=True)
    scenes_dir = os.path.join(out_dir, "temporal")
    os.makedirs(scenes_dir, exist_ok=True)
    codes = make_zone_masks()

    for date_str in TEMPORAL_DATES:
        doy = day_of_year(date_str)
        blue = [[0.0] * WIDTH for _ in range(HEIGHT)]
        green = [[0.0] * WIDTH for _ in range(HEIGHT)]
        red = [[0.0] * WIDTH for _ in range(HEIGHT)]
        nir = [[0.0] * WIDTH for _ in range(HEIGHT)]
        for y in range(HEIGHT):
            for x in range(WIDTH):
                c = codes[y][x]
                if c == 1:      # evergreen forest
                    ndvi = 0.75 + rng.normal() * 0.02
                    nir_p = 0.45
                elif c == 3:    # water
                    ndvi = -0.10 + rng.normal() * 0.02
                    nir_p = 0.03
                elif c == 4:    # disturbance patch: cropland curve, cut after Aug
                    if date_str <= "2024-08-15":
                        ndvi = cropland_ndvi(doy) + rng.normal() * 0.025
                    else:
                        ndvi = 0.12 + rng.normal() * 0.02
                    nir_p = 0.42
                elif c == 2:    # cropland
                    ndvi = cropland_ndvi(doy) + rng.normal() * 0.025
                    nir_p = 0.42
                else:           # background soil
                    ndvi = 0.12 + rng.normal() * 0.02
                    nir_p = 0.25
                n = quantize8(nir_p + rng.normal() * 0.01)
                r = quantize8(n * (1.0 - ndvi) / (1.0 + ndvi))
                g = quantize8((n + r) / 2.0)
                b = quantize8(r * 0.9)
                blue[y][x], green[y][x], red[y][x], nir[y][x] = b, g, r, n
        path = os.path.join(scenes_dir, f"scene_{date_str}.tif")
        write_gtiff(path, [blue, green, red, nir], dates=date_str,
                    band_roles={3: "red", 4: "nir"})
        print(f"wrote {path}")

    # Grading-aux product: class-code raster (byte). Declared in the lab's
    # data spec so D1 regenerates it for the real classroom environment.
    zones_path = os.path.join(scenes_dir, "zones.tif")
    write_gtiff(zones_path, [[float(codes[y][x]) for x in range(WIDTH)]
                             for y in range(HEIGHT)],
                dtype=gdal.GDT_Byte)
    print(f"wrote {zones_path}")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("lab", choices=["temporal", "sar", "hyperspectral"])
    ap.add_argument("--out", required=True, help="output dir (must end in data/labs/_tmp)")
    ap.add_argument("--seed", type=int, default=42)
    args = ap.parse_args()

    norm = os.path.normpath(args.out)
    if os.path.basename(norm) != "_tmp":
        sys.exit("refusing: --out must point at data/labs/_tmp (fixtures are never committed)")

    if args.lab == "temporal":
        gen_temporal(norm, args.seed)
    else:
        sys.exit(f"lab '{args.lab}' generator lands with its authoring phase")


if __name__ == "__main__":
    main()
