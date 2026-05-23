import numpy as np
import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '../build'))

import raster_ops

# Test warp_image
input_data = np.array([[10, 20], [30, 40]], dtype=np.uint8)
coeffs_x = np.array([0.0, 1.0, 0.0], dtype=np.float64) # Identity mapping x = x
coeffs_y = np.array([0.0, 0.0, 1.0], dtype=np.float64) # Identity mapping y = y
out_w = 2
out_h = 2

out = raster_ops.warp_image(input_data, out_w, out_h, coeffs_x, coeffs_y)
print("Warped output:")
print(out)
assert np.array_equal(out, input_data), "Identity warp failed"
print("Success!")
