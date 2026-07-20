# Teaching / Agent Pipeline Examples

JSON pipelines for `sicnu_geo_rs_cli --pipeline <file.json>`.

Schema: see `data/schemas/pipeline_schema.json`.

## Usage

```bash
# List operators
./cmake-build/sicnu_geo_rs_cli --list

# Print operator schema
./cmake-build/sicnu_geo_rs_cli --schema rs:spectral_index

# Run a pipeline (headless)
./cmake-build/sicnu_geo_rs_cli --pipeline data/pipelines/ndvi_smooth.json
```

## Files

| File | Purpose |
|------|---------|
| `ndvi_smooth.json` | Spectral index (NDVI) then OpenCV Gaussian blur |
| `reproject_clip.json` | Reproject to Web Mercator then clip by extent |
| `supervised_classify.json` | SVM land-cover from `training_samples.shp` |
| `train_then_predict.json` | Train + save model, then predict-only with `modelIn` |
| `obia_segment.json` | Simple OBIA object segmentation |
| `obia_classify.json` | Full OBIA classify (objects + ROI train + SVM) |
| `obia_export.json` | Segment → stats CSV → polygon shapefile |

Edit `input` / `output` paths to match your local data. Sample GeoTIFFs live under `data/samples/`.

### Classification / OBIA notes

- `rs:kmeans_classification` — unsupervised (no samples)
- `rs:supervised_classification` — train from polygons (`classField`) **or** predict-only with `modelIn`
- `rs:obia_segment` — teaching object segmentation (smooth → quantize → CC)
- `rs:obia_classify` — segment + mean features + ROI majority label + classify objects
- `rs:segment_stats` — per-object mean/area table (CSV)
- `gdal:polygonize` — label/class raster → polygons
