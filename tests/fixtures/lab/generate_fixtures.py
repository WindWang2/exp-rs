#!/usr/bin/env python3
"""Deterministic fixture generator for the lab auto-grading corpus (D4).

Every fixture is a closed-form scene: expectations (NDVI values, atan(2),
300 K, class proportions, change areas) are ANALYTICALLY DERIVABLE, matching
the known-answer tradition in docs/verification/KNOWN_ANSWER_MATRIX.md.
No random seeds, no timestamps; GTiff written uncompressed so the committed
bytes are reproducible run-to-run.

Scenes (documented in README.md next to this script):
  ndvi_basics      128x128 Float32 NDVI: water rows 0..31 (v=-0.2),
                   vegetation rows 32..127 (v=21/29), 128 declared-invalid
                   input pixels (rows 96..103 x cols 32..47) -> NoData.
  ndvi_bandpair    16x16 2-band Float32 DN stack, declared scale 1e-4:
                   quadrant DN pairs (NIR,Red) = (480,640),(7200,1200),
                   (2400,2800),(4500,1500).
  terrain_slope    32x32 Float32 Horn slope of z=2x, 1 m cells, EPSG:32648:
                   interior atan(2), 1-px border NoData (124 px).
  planck_temp      128x128 Float32 brightness temperature:
                   T = 300 + 2*sin(2*pi*col/128)  (mean 300, pop-sigma sqrt2).
  landcover        32x32 Byte classes (1 water rows0..7, 2 veg rows8..23,
                   3 bare row24..27, 4 urban rows28..31), truth+submission
                   carry NoData=0 on rows 16..17 x cols 0..15 (32 px).
  change_detect    128x128 Byte change mask (1 = change): rows16..31 x
                   cols0..63 and rows64..95 x cols32..95 (3072 px); 64 input
                   invalid pixels (rows100..103 x cols0..15) -> NoData 255.

Wrong-answer variants are documented in wrong_answer_corpus.json.
"""
import math
import os

from osgeo import gdal, osr

HERE = os.path.dirname(os.path.abspath(__file__))

FT = gdal.GDT_Float32
BT = gdal.GDT_Byte
NODATA_F = -9999.0


def write_gtiff(name, arrays, dtype, gt, epsg, nodata, scales=None):
    """arrays: list of 2D row-major lists (band-sequential)."""
    path = os.path.join(HERE, name)
    height = len(arrays[0])
    width = len(arrays[0][0])
    drv = gdal.GetDriverByName("GTiff")
    ds = drv.Create(path, width, height, len(arrays), dtype, [])
    ds.SetGeoTransform([gt[0], gt[1], 0.0, gt[2], 0.0, gt[3]])
    srs = osr.SpatialReference()
    srs.ImportFromEPSG(epsg)
    ds.SetSpatialRef(srs)
    for i, arr in enumerate(arrays, start=1):
        band = ds.GetRasterBand(i)
        band.SetNoDataValue(nodata)
        if scales and scales[i - 1] is not None:
            band.SetScale(scales[i - 1])
        flat = [float(v) for row in arr for v in row]
        band.WriteRaster(0, 0, width, height,
                         bytes(bytearray(_pack(dtype, flat))),
                         buf_type=dtype)
    band.FlushCache()
    ds = None
    print("wrote", path)


def _pack(dtype, values):
    import struct
    if dtype == FT:
        return struct.pack("<%df" % len(values), *values)
    return struct.pack("<%dB" % len(values), *[int(v) for v in values])


def grid(w, h, fill=0.0):
    return [[fill] * w for _ in range(h)]


def clamp_byte(v):
    return max(0, min(255, int(round(v))))


GT4326 = (100.0, 0.001, 40.0, -0.001)  # originX, pxX, originY, pxY


# ---------------------------------------------------------------- ndvi_basics
def ndvi_value(nir, red):
    return (nir - red) / (nir + red)


WATER_NDVI = ndvi_value(0.02, 0.03)          # -0.2 exactly
VEG_NDVI = ndvi_value(0.50, 0.08)            # 21/29

def invalid_basic(r, c):
    return 96 <= r <= 103 and 32 <= c <= 47


def gen_ndvi_basics():
    w = h = 128

    def base(fill_invalid):
        out = grid(w, h)
        for r in range(h):
            for c in range(w):
                if invalid_basic(r, c):
                    out[r][c] = NODATA_F if fill_invalid else 0.0
                else:
                    out[r][c] = WATER_NDVI if r < 32 else VEG_NDVI
        return out

    write_gtiff("ndvi_basics_reference.tif", [base(True)], FT, GT4326, 4326, NODATA_F)

    # wrong: linear stretch x100 +50 (un-calibrated display stretch)
    ref = base(True)
    stretched = [[NODATA_F if v == NODATA_F else v * 100.0 + 50.0 for v in row]
                 for row in ref]
    write_gtiff("ndvi_basics_wrong_stretch.tif", [stretched], FT, GT4326, 4326, NODATA_F)

    # wrong: everything NoData ("masked everything")
    all_nodata = [[NODATA_F] * w for _ in range(h)]
    write_gtiff("ndvi_basics_wrong_all_nodata.tif", [all_nodata], FT, GT4326, 4326, NODATA_F)

    # wrong: forgot to mask (invalid inputs computed as 0.0 instead of NoData)
    write_gtiff("ndvi_basics_wrong_unmasked.tif", [base(False)], FT, GT4326, 4326, NODATA_F)

    # wrong: red/NIR swapped (sign flip of every value)
    swapped = grid(w, h)
    for r in range(h):
        for c in range(w):
            if invalid_basic(r, c):
                swapped[r][c] = NODATA_F
            else:
                n, rd = (0.03, 0.02) if r < 32 else (0.08, 0.50)
                swapped[r][c] = ndvi_value(n, rd)
    write_gtiff("ndvi_basics_wrong_swapped_bands.tif", [swapped], FT, GT4326, 4326, NODATA_F)


# -------------------------------------------------------------- ndvi_bandpair
BANDPAIR_QUADS = [  # (rows, cols, NIR_DN, RED_DN)
    (range(0, 8), range(0, 8), 480, 640),     # water
    (range(0, 8), range(8, 16), 7200, 1200),  # veg
    (range(8, 16), range(0, 8), 2400, 2800),  # bare soil
    (range(8, 16), range(8, 16), 4500, 1500),  # sparse veg
]


def gen_ndvi_bandpair():
    w = h = 16

    def stack(red_scale=None):
        # stored DN with declared per-band scale 1e-4 (offset 0) — the gain
        # invariance lesson: applying or omitting a pure-gain calibration
        # cannot change the ratio index; a per-band gain mismatch can.
        nir = grid(w, h)
        red = grid(w, h)
        for r in range(h):
            for c in range(w):
                for rows, cols, ndn, rdn in BANDPAIR_QUADS:
                    if r in rows and c in cols:
                        nir[r][c] = ndn
                        red[r][c] = rdn
        return [nir, red]

    write_gtiff("ndvi_bandpair_reference.tif", stack(),
                FT, GT4326, 4326, NODATA_F, scales=[1e-4, 1e-4])

    # wrong: bands swapped (Red stored first)
    ref = stack()
    write_gtiff("ndvi_bandpair_wrong_swapped.tif", [ref[1], ref[0]],
                FT, GT4326, 4326, NODATA_F, scales=[1e-4, 1e-4])

    # wrong: per-band gain mis-declared (band 2 scale doubled) — a linear
    # calibration whose gain no longer cancels in the ratio index
    write_gtiff("ndvi_bandpair_wrong_gain_mismatch.tif", stack(),
                FT, GT4326, 4326, NODATA_F, scales=[1e-4, 2e-4])


# --------------------------------------------------------------- terrain_slope
ATAN2 = math.atan(2.0)
GT32648 = (500000.0, 1.0, 4000000.0, -1.0)


def gen_terrain_slope():
    w = h = 32

    def slope(radians=True, fill_border=True, axis="x"):
        out = grid(w, h, NODATA_F)
        for r in range(h):
            for c in range(w):
                interior = 1 <= r <= 30 and 1 <= c <= 30
                if not interior:
                    if fill_border:
                        out[r][c] = NODATA_F
                    else:
                        out[r][c] = ATAN2 if radians else math.degrees(ATAN2)
                    continue
                if radians:
                    out[r][c] = ATAN2
                else:
                    out[r][c] = math.degrees(ATAN2)
                if axis == "y":
                    out[r][c] = 0.0 if radians else 0.0
        return out

    write_gtiff("terrain_slope_reference.tif", [slope()], FT, GT32648, 32648, NODATA_F)
    write_gtiff("terrain_slope_wrong_degrees.tif", [slope(radians=False)], FT, GT32648, 32648, NODATA_F)
    write_gtiff("terrain_slope_wrong_axis.tif", [slope(axis="y")], FT, GT32648, 32648, NODATA_F)
    write_gtiff("terrain_slope_wrong_filled_border.tif", [slope(fill_border=False)], FT, GT32648, 32648, NODATA_F)


# -------------------------------------------------------------- planck_temp
K1 = 774.8853   # Landsat 8 TIRS b10
K2 = 1321.0789


def planck_t(col):
    return 300.0 + 2.0 * math.sin(2.0 * math.pi * col / 128.0)


def gen_planck_temp():
    w = h = 128

    def t_field(transform):
        out = grid(w, h)
        for r in range(h):
            for c in range(w):
                out[r][c] = transform(planck_t(c))
        return out

    write_gtiff("planck_temperature_reference.tif", [t_field(lambda t: t)], FT, GT4326, 4326, NODATA_F)

    # wrong: Celsius submitted where Kelvin is graded
    write_gtiff("planck_temperature_wrong_celsius.tif",
                [t_field(lambda t: t - 273.15)], FT, GT4326, 4326, NODATA_F)

    # wrong: inverted from 100x-scaled radiance (uncalibrated DN path)
    def t_from_scaled_lampda(t):
        lum = K2 / math.log(K1 / t + 1.0)
        return K2 / math.log(K1 / (100.0 * lum) + 1.0)

    write_gtiff("planck_temperature_wrong_dn_inverted.tif",
                [t_field(t_from_scaled_lampda)], FT, GT4326, 4326, NODATA_F)

    # wrong: scale slip x1.05
    write_gtiff("planck_temperature_wrong_gain.tif",
                [t_field(lambda t: t * 1.05)], FT, GT4326, 4326, NODATA_F)


# ----------------------------------------------------------------- landcover
LC_INVALID = lambda r, c: 16 <= r <= 17 and 0 <= c <= 15  # 32 px


def lc_class(r, _c):
    if r <= 7:
        return 1
    if r <= 23:
        return 2
    if r <= 27:
        return 3
    return 4


def gen_landcover():
    w = h = 32

    def cls(fill_invalid_with=None):
        out = grid(w, h, 0)
        for r in range(h):
            for c in range(w):
                if LC_INVALID(r, c):
                    out[r][c] = 0 if fill_invalid_with is None else fill_invalid_with
                else:
                    out[r][c] = lc_class(r, c)
        return out

    write_gtiff("landcover_truth.tif", [cls()], BT, GT4326, 4326, 0)
    write_gtiff("landcover_reference.tif", [cls()], BT, GT4326, 4326, 0)

    # wrong: everything mapped to the majority class
    all_veg = [[2] * w for _ in range(h)]
    write_gtiff("landcover_wrong_all_veg.tif", [all_veg], BT, GT4326, 4326, 0)

    # wrong: labels shifted by one (systematic legend error)
    shifted = grid(w, h, 0)
    for r in range(h):
        for c in range(w):
            if LC_INVALID(r, c):
                shifted[r][c] = 0
            else:
                shifted[r][c] = (lc_class(r, c) % 4) + 1
    write_gtiff("landcover_wrong_shifted_labels.tif", [shifted], BT, GT4326, 4326, 0)

    # wrong: forgot to mask — invalid pixels filled with the class estimate
    write_gtiff("landcover_wrong_unmasked.tif", [cls(fill_invalid_with=2)], BT, GT4326, 4326, 0)


# -------------------------------------------------------------- change_detect
def is_change(r, c):
    return (16 <= r <= 31 and 0 <= c <= 63) or (64 <= r <= 95 and 32 <= c <= 95)


def invalid_change(r, c):
    return 100 <= r <= 103 and 0 <= c <= 15  # 64 px


def gen_change_detect():
    w = h = 128

    def mask(fn_change, fill_invalid=255):
        out = grid(w, h, 255)
        for r in range(h):
            for c in range(w):
                if invalid_change(r, c):
                    out[r][c] = fill_invalid
                else:
                    out[r][c] = 1 if fn_change(r, c) else 0
        return out

    write_gtiff("change_detect_reference.tif", [mask(is_change)], BT, GT4326, 4326, 255)

    # wrong: threshold too low — one extra 32x32 block flagged
    over = lambda r, c: is_change(r, c) or (0 <= r <= 31 and 96 <= c <= 127)
    write_gtiff("change_detect_wrong_over_detect.tif", [mask(over)], BT, GT4326, 4326, 255)

    # wrong: threshold too high — first block missed
    under = lambda r, c: 64 <= r <= 95 and 32 <= c <= 95
    write_gtiff("change_detect_wrong_under_detect.tif", [mask(under)], BT, GT4326, 4326, 255)

    # wrong: everything NoData
    write_gtiff("change_detect_wrong_all_nodata.tif",
                [[[255] * w for _ in range(h)]], BT, GT4326, 4326, 255)

    # wrong: inverted mask (sign error)
    inv = lambda r, c: (not invalid_change(r, c)) and not is_change(r, c)
    write_gtiff("change_detect_wrong_inverted.tif", [mask(inv)], BT, GT4326, 4326, 255)


if __name__ == "__main__":
    gdal.UseExceptions()
    gen_ndvi_basics()
    gen_ndvi_bandpair()
    gen_terrain_slope()
    gen_planck_temp()
    gen_landcover()
    gen_change_detect()
