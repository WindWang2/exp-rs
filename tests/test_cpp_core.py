import os
import sys
import time
import pytest
import numpy as np
from osgeo import gdal

BUILD = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "cmake-build"))
BUILD_PY = os.path.join(BUILD, "src", "python")
DATA_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))

@pytest.fixture(scope="session")
def core():
    sys.path.insert(0, BUILD_PY)
    sys.path.insert(0, BUILD)
    os.environ.setdefault("ANTIGRAVITY_DATA", BUILD)
    import _antigravity_core as c
    c.init(BUILD)
    return c

def test_open_le7_b4(core):
    tif = os.path.join(DATA_ROOT, "data/LE7/LE71300411999327EDC00_B4.TIF")
    info = core.open_raster(tif)
    assert info["valid"] is True
    assert info["width"] > 7000 and info["height"] > 7000
    assert info["crs"].startswith("EPSG:")

def _rmse(a, b):
    a = a.astype(np.float64)
    b = b.astype(np.float64)
    return float(np.sqrt(np.mean((a - b) ** 2)))

def test_render_parity(core, tmp_path):
    tif = os.path.join(DATA_ROOT, "data/LE7/LE71300411999327EDC00_B4.TIF")
    out = str(tmp_path / "cpp.png")
    assert core.render_to_png(tif, out, 512)
    # Hold dataset refs explicitly — GDAL Band proxy goes null if Dataset is GC'd
    ds_cpp = gdal.Open(out)
    ds_ref = gdal.Open(os.path.join(DATA_ROOT, "tests/golden/le7_b4_ref.png"))
    assert ds_cpp is not None, f"GDAL could not open rendered PNG: {out}"
    assert ds_ref is not None, "GDAL could not open golden master"
    cpp = ds_cpp.GetRasterBand(1).ReadAsArray()
    ref = ds_ref.GetRasterBand(1).ReadAsArray()
    del ds_cpp, ds_ref
    assert cpp.shape == ref.shape == (512, 512)
    rmse = _rmse(cpp, ref)
    assert rmse < 25.0, f"render RMSE {rmse:.2f} exceeds tolerance vs golden master"

def test_render_perf(core, tmp_path):
    tif = os.path.join(DATA_ROOT, "data/LE7/LE71300411999327EDC00_B4.TIF")
    out = str(tmp_path / "perf.png")
    core.render_to_png(tif, out, 1024)
    t0 = time.perf_counter()
    assert core.render_to_png(tif, out, 1024)
    dt = time.perf_counter() - t0
    assert dt < 0.6, f"render took {dt:.3f}s, not faster than Python baseline"
