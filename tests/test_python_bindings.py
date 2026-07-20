#!/usr/bin/env python3
"""Tests for sicnu_operators pybind11 module (cv::Mat/ndarray zero-copy)."""
import os
import sys
import unittest

import numpy as np

# The C++ extension module is staged next to the test runner under ../lib.
script_dir = os.path.dirname(os.path.abspath(__file__))
module_dir = os.path.abspath(os.path.join(script_dir, "..", "lib"))
if module_dir not in sys.path:
    sys.path.insert(0, module_dir)

import sicnu_operators as so


class TestOperatorRegistry(unittest.TestCase):
    def test_list_operators(self):
        names = so.list_operators()
        self.assertIn("opencv:gaussian_blur", names)
        self.assertIn("opencv:sobel", names)

    def test_operator_schema(self):
        schema = so.operator_schema("opencv:gaussian_blur")
        self.assertIn("properties", schema)
        self.assertIn("input", schema["properties"])
        self.assertEqual(schema["properties"]["kernelSize"]["default"], 5)

    def test_operator_metadata(self):
        meta = so.operator_metadata("opencv:gaussian_blur")
        self.assertEqual(meta["group"], "opencv-filter")


class TestMatNdarrayRoundtrip(unittest.TestCase):
    def test_ndarray_to_cv_mat_and_back_is_zero_copy(self):
        arr = np.arange(16, dtype=np.uint8).reshape(4, 4)

        # ndarray -> cv::Mat (zero-copy)
        mat = so.to_cv_mat(arr)
        self.assertEqual(mat.shape, (4, 4))
        self.assertEqual(mat.dtype, np.uint8)
        self.assertEqual(mat[1, 2], arr[1, 2])

        # Modify numpy array and verify cv::Mat sees the change.
        arr[1, 2] = 99
        self.assertEqual(mat[1, 2], 99)

        # cv::Mat -> ndarray (zero-copy)
        out = so.to_numpy(mat)
        self.assertEqual(out.shape, (4, 4))
        self.assertEqual(out.dtype, np.uint8)
        self.assertEqual(out[1, 2], 99)

        # Modify cv::Mat view and verify ndarray sees the change.
        mat[0, 0] = 42
        self.assertEqual(out[0, 0], 42)

    def test_gaussian_blur_on_ndarray(self):
        arr = np.zeros((16, 16), dtype=np.uint8)
        arr[:, :8] = 50
        arr[:, 8:] = 200

        out = so.gaussian_blur(arr, 3, 1.0)
        self.assertEqual(out.shape, (16, 16))
        self.assertEqual(out.dtype, np.uint8)

    def test_sobel_on_ndarray(self):
        arr = np.zeros((16, 16), dtype=np.uint8)
        arr[:, :8] = 50
        arr[:, 8:] = 200

        out = so.sobel(arr, 1, 0, 3)
        self.assertEqual(out.shape, (16, 16))


class TestRunOperator(unittest.TestCase):
    def test_run_opencv_gaussian_blur(self):
        import tempfile

        with tempfile.TemporaryDirectory() as tmpdir:
            input_path = os.path.join(tmpdir, "in.tif")
            output_path = os.path.join(tmpdir, "out.tif")

            # Create a simple GeoTIFF via GDAL Python bindings if available.
            try:
                from osgeo import gdal
            except ImportError:
                self.skipTest("GDAL Python bindings not available")

            drv = gdal.GetDriverByName("GTiff")
            ds = drv.Create(input_path, 16, 16, 1, gdal.GDT_Float32)
            band = ds.GetRasterBand(1)
            data = np.full((16, 16), 100.0, dtype=np.float32)
            data[:, 8:] = 200.0
            band.WriteArray(data)
            ds.SetGeoTransform([0.0, 1.0, 0.0, 0.0, 0.0, -1.0])
            ds.SetProjection(
                'GEOGCS["WGS 84",DATUM["WGS_1984",SPHEROID["WGS 84",6378137,298.257223563]],'
                'PRIMEM["Greenwich",0],UNIT["degree",0.0174532925199433]]'
            )
            ds = None

            result = so.run_operator(
                "opencv:gaussian_blur",
                {"input": input_path, "output": output_path, "kernelSize": 5, "sigma": 1.0},
            )
            self.assertEqual(result["output"], output_path)
            self.assertTrue(os.path.exists(output_path))


if __name__ == "__main__":
    unittest.main()
