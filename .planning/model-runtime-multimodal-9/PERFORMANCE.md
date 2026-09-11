# PERFORMANCE — model-runtime-multimodal-9

Environment (fixed for all numbers below unless stated):
- Host: linux 6.18, RTX 3080 Laptop 16 GB (SM 8.6), driver 610.57.04/CUDA 13.3
- Build: Release, Ninja, -j4, gcc, ccache warm
- ORT: 1.30.0 GPU SDK (pip) — CPU EP lane AND CUDA EP lane
- Data: synthetic/known-answer fixtures; no giant rasters

(numbers recorded per milestone in TEST_MATRIX.md runs; final consolidated
table lands here at M9.)
