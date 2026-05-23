import numpy as np
import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '../build'))

import raster_ops

# Create dummy multi-band data (e.g. 4 pixels, 3 bands)
# Random floats
np.random.seed(42)
data = np.random.rand(100, 3).astype(np.float32)

projected, evecs, mean = raster_ops.compute_pca(data)

print("Projected shape:", projected.shape)
print("Eigenvectors shape:", evecs.shape)
print("Mean shape:", mean.shape)

# Verify with numpy
mean_np = data.mean(axis=0)
centered = data - mean_np
cov = np.cov(centered, rowvar=False)
evals_np, evecs_np = np.linalg.eigh(cov)
# sort descending
idx = np.argsort(evals_np)[::-1]
evecs_np = evecs_np[:, idx]
projected_np = centered.dot(evecs_np)

print("C++ Mean:", mean)
print("NP Mean:", mean_np)

# Eigenvectors could be flipped in sign, which is fine in PCA
assert np.allclose(np.abs(projected), np.abs(projected_np), atol=1e-5), "PCA projection mismatch"
print("Success!")
