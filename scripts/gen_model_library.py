#!/usr/bin/env python3
"""Generates the Platform 3.0 model catalog manifests (templates, no weights).

Manifests use the exact v2/v3 keys the ModelCatalog parser reads (input object
with band_roles, inputs[] for multi-input models, output object with
classes/uncertainty, preprocess/tiling/postprocess/runtime/artifact sections,
domain block). Readiness stays missing_artifact until weights are placed and
the checksum filled in — weights are never committed.
"""
import json
import os

MODELS_DIR = os.path.join(os.path.dirname(__file__), "..", "models")


def input_contract(band_roles, temporal_length=0, name=""):
    c = {"dtype": "float32", "layout": "NCHW", "band_roles": band_roles}
    if name:
        c["name"] = name
    if temporal_length:
        c["temporal_length"] = temporal_length
        c["temporal_collapse"] = "channels"
    return c


def manifest(
    name, task, output, description, tags, sensors, resolution,
    band_roles, modalities, preprocess, tiling=None, classes=None,
    gpu=False, vram=0, temporal_length=0, polarizations=None,
    uncertainty=None, accuracy=-1.0, radiometric_state="", source="",
    mask_threshold=-1.0, batch=2, inputs=None, tensor_names=None,
):
    tiling = tiling or {"supported": True, "tile_size": 256, "overlap": 32, "batch_size": batch}
    m = {
        "name": name,
        "task": task,
        "framework": "onnx",
        "path": "",
        "gpu": gpu,
        "accuracy": accuracy,
        "domain": {
            "sensors": sensors,
            "resolution_range": resolution,
            "modalities": modalities,
        },
        "artifact": {
            "path": "",
            "checksum": "",
            "size_bytes": 0,
            "note": "weights are not distributed with the repository; download "
                    "from the reference, place beside the manifest, fill path + "
                    "sha256 checksum",
        },
        "input": input_contract(band_roles, temporal_length),
        "preprocess": preprocess,
        "tiling": tiling,
        "output": {
            "type": output,
            "classes": classes or [],
        },
        "postprocess": {
            "mask_threshold": mask_threshold if output == "raster" else -1.0,
        },
        "runtime": {
            "gpu": gpu,
            "cpu_fallback": True,
            "estimated_vram_mb": vram,
            "estimated_ram_mb": max(512, vram or 1024),
            "supports_tiling": tiling.get("supported", True),
        },
        "reference": source,
        "description": description,
        "tags": tags,
    }
    if inputs:
        # v3 multi-input: every input needs a unique name.
        m["inputs"] = inputs
        del m["input"]
    if polarizations:
        m["domain"]["polarizations"] = polarizations
    if radiometric_state:
        m["domain"]["radiometric_state"] = radiometric_state
    if not classes:
        del m["output"]["classes"]
    if tensor_names:
        m["output"]["tensor_names"] = tensor_names
    if uncertainty:
        m["output"]["uncertainty"] = uncertainty
    if not m["output"].get("classes") and not tensor_names and not uncertainty:
        pass
    return m


MEAN_STD_S2_4B = {
    "normalize": "mean_std",
    "mean": [0.1768, 0.1684, 0.1462, 0.2306],
    "std": [0.2103, 0.2144, 0.2374, 0.2183],
    "scale": 1.0,
}
MEAN_STD_S2_2B = {
    "normalize": "mean_std",
    "mean": [0.1684, 0.2306],
    "std": [0.2144, 0.2183],
    "scale": 1.0,
}
LINEAR_255 = {"normalize": "linear", "scale": 1.0 / 255.0}
MEAN_STD_RGB = {
    "normalize": "mean_std",
    "mean": [0.485, 0.456, 0.406],
    "std": [0.229, 0.224, 0.225],
    "scale": 1.0,
}
NONE_PREP = {"normalize": "none"}

MODELS = [
    # --- buildings ---
    manifest("unet-buildings-s2", "segmentation", "raster",
             "U-Net building footprint segmentation on 10 m Sentinel-2 imagery "
             "(template: download weights, set artifact path + checksum).",
             ["buildings", "unet", "sentinel-2"], ["Sentinel-2"], [3.0, 10.0],
             ["red", "green", "blue", "nir"], ["optical"], MEAN_STD_S2_4B,
             {"supported": True, "tile_size": 256, "overlap": 32, "batch_size": 2},
             classes=["background", "building"], accuracy=0.91,
             source="https://github.com/template/unet-buildings-s2"),
    manifest("sam-buildings-hr", "segmentation", "polygon",
             "SAM-family prompt-free building extraction for high-resolution "
             "imagery (0.3-1 m); outputs polygons after mask thresholding.",
             ["buildings", "sam", "high-resolution"],
             ["WorldView-3", "GF-2", "PlanetScope"], [0.3, 1.5],
             ["red", "green", "blue"], ["optical"], MEAN_STD_RGB,
             {"supported": True, "tile_size": 1024, "overlap": 128, "batch_size": 1},
             gpu=True, vram=4096, accuracy=0.90,
             source="https://github.com/template/sam-buildings"),
    manifest("yolo-building-detection", "detection", "vector",
             "YOLO-family building detection on aerial imagery (template).",
             ["buildings", "yolo", "detection"], ["GF-2", "WorldView-2"], [0.5, 2.0],
             ["red", "green", "blue"], ["optical"], LINEAR_255,
             {"supported": True, "tile_size": 640, "overlap": 64, "batch_size": 4},
             classes=["building"], accuracy=0.88,
             source="https://github.com/template/yolo-buildings"),
    # --- roads ---
    manifest("unet-roads-s2", "segmentation", "raster",
             "U-Net road extraction on Sentinel-2 (10 m); pairs well with a "
             "morphological thinning follow-up.",
             ["roads", "unet", "sentinel-2"], ["Sentinel-2"], [10.0, 10.0],
             ["red", "green", "blue", "nir"], ["optical"], MEAN_STD_S2_4B,
             {"supported": True, "tile_size": 256, "overlap": 32, "batch_size": 2},
             classes=["background", "road"], accuracy=0.84,
             source="https://github.com/template/road-extraction"),
    manifest("dlinknet-roads-hr", "segmentation", "raster",
             "D-LinkNet road segmentation for high-resolution aerial imagery "
             "(DeepGlobe-style).",
             ["roads", "dlinknet", "high-resolution"], ["WorldView-3", "aerial"],
             [0.3, 1.0], ["red", "green", "blue"], ["optical"], MEAN_STD_RGB,
             {"supported": True, "tile_size": 512, "overlap": 64, "batch_size": 1},
             classes=["background", "road"], accuracy=0.89,
             source="https://github.com/template/dlinknet"),
    manifest("segformer-roads", "segmentation", "raster",
             "SegFormer road segmentation (transformer encoder) on "
             "high-resolution optical imagery.",
             ["roads", "segformer", "transformer"], ["PlanetScope", "GF-2"],
             [0.5, 3.0], ["red", "green", "blue"], ["optical"], MEAN_STD_RGB,
             {"supported": True, "tile_size": 512, "overlap": 64, "batch_size": 1},
             gpu=True, vram=3072, classes=["background", "road"], accuracy=0.87,
             source="https://github.com/template/segformer-roads"),
    # --- water ---
    manifest("unet-water-s2", "segmentation", "raster",
             "Surface water segmentation on Sentinel-2 (NDWI-assisted U-Net).",
             ["water", "unet", "sentinel-2"], ["Sentinel-2"], [10.0, 20.0],
             ["green", "nir"], ["optical"], MEAN_STD_S2_2B,
             {"supported": True, "tile_size": 256, "overlap": 32, "batch_size": 4},
             classes=["dry", "water"], accuracy=0.93,
             source="https://github.com/template/water-segmentation"),
    manifest("unet-water-sar", "segmentation", "raster",
             "SAR water segmentation (Sentinel-1 VV+VH); robust to cloud cover, "
             "expects sigma0 linear power (calibrate with rs:sar_calibrate).",
             ["water", "sar", "sentinel-1"], ["Sentinel-1"], [10.0, 40.0],
             ["vv", "vh"], ["sar"], NONE_PREP,
             {"supported": True, "tile_size": 256, "overlap": 32, "batch_size": 4},
             polarizations=["VV", "VH"], radiometric_state="sigma0",
             classes=["land", "water", "shadow"], accuracy=0.90,
             source="https://github.com/template/sar-water"),
    # --- land cover ---
    manifest("segformer-landcover-s2", "segmentation", "raster",
             "SegFormer land-cover classification (10 classes) on Sentinel-2 "
             "bands.",
             ["landcover", "segformer"], ["Sentinel-2"], [10.0, 30.0],
             ["blue", "green", "red", "nir", "swir1", "swir2"], ["optical"],
             {"normalize": "mean_std",
              "mean": [0.1768, 0.1684, 0.1462, 0.2306, 0.2537, 0.1826],
              "std": [0.2103, 0.2144, 0.2374, 0.2183, 0.2295, 0.2189],
              "scale": 1.0},
             {"supported": True, "tile_size": 512, "overlap": 64, "batch_size": 1},
             gpu=True, vram=4096,
             classes=["background", "cropland", "forest", "grassland", "shrubland",
                      "wetland", "water", "built-up", "bare", "snow"],
             accuracy=0.78,
             source="https://github.com/template/segformer-lc"),
    manifest("unet-deeplabv3-landcover", "segmentation", "raster",
             "DeepLabV3+ land-cover segmentation on Landsat-8/9 surface "
             "reflectance.",
             ["landcover", "deeplabv3", "landsat"], ["Landsat-8", "Landsat-9"],
             [15.0, 30.0], ["blue", "green", "red", "nir", "swir1", "swir2"],
             ["optical"],
             {"normalize": "mean_std",
              "mean": [0.1768, 0.1684, 0.1462, 0.2306, 0.2537, 0.1826],
              "std": [0.2103, 0.2144, 0.2374, 0.2183, 0.2295, 0.2189],
              "scale": 1.0},
             {"supported": True, "tile_size": 256, "overlap": 32, "batch_size": 2},
             classes=["background", "cropland", "forest", "urban", "water", "barren"],
             accuracy=0.75,
             source="https://github.com/template/deeplab-lc"),
    # --- crops ---
    manifest("temporal-transformer-crop", "classification", "raster",
             "Temporal transformer crop-type classifier over a season of "
             "Sentinel-2 composites: feeds T=12 frames collapsed to 48 channels "
             "(4 bands x 12 frames).",
             ["crop", "temporal", "transformer"], ["Sentinel-2"], [10.0, 20.0],
             ["red", "green", "blue", "nir"], ["optical"], MEAN_STD_S2_4B,
             {"supported": False, "tile_size": 128, "overlap": 0, "batch_size": 1},
             temporal_length=12, gpu=True, vram=2048,
             classes=["background", "rice", "maize", "wheat", "soy", "other"],
             accuracy=0.82,
             source="https://github.com/template/temporal-crop"),
    manifest("unet-crop-parcel", "segmentation", "raster",
             "Parcel-level crop segmentation on Sentinel-2 (smallholder "
             "landscapes).",
             ["crop", "unet"], ["Sentinel-2"], [10.0, 10.0],
             ["red", "green", "blue", "nir"], ["optical"], MEAN_STD_S2_4B,
             {"supported": True, "tile_size": 256, "overlap": 32, "batch_size": 2},
             classes=["background", "crop"], accuracy=0.85,
             source="https://github.com/template/crop-parcel"),
    # --- forest (optical-SAR fusion, multi-input) ---
    manifest("unet-forest-s1s2", "segmentation", "raster",
             "Optical-SAR fusion forest mapping: two named inputs (Sentinel-1 "
             "sigma0 linear power + Sentinel-2 reflectance).",
             ["forest", "fusion", "sentinel-1", "sentinel-2"],
             ["Sentinel-1", "Sentinel-2"], [10.0, 20.0],
             ["vv", "red", "nir"], ["sar", "optical"], NONE_PREP,
             {"supported": True, "tile_size": 256, "overlap": 32, "batch_size": 1},
             polarizations=["VV"], radiometric_state="sigma0",
             classes=["non-forest", "forest"], accuracy=0.88,
             inputs=[
                 {"name": "sar", "dtype": "float32", "layout": "NCHW",
                  "band_roles": ["vv"]},
                 {"name": "optical", "dtype": "float32", "layout": "NCHW",
                  "band_roles": ["red", "nir"]},
             ],
             source="https://github.com/template/s1s2-forest"),
    # --- change detection (Siamese pairs) ---
    manifest("siamese-change-buildings", "change_detection", "raster",
             "Siamese U-Net building change detection: two named inputs "
             "(before/after, same band roles).",
             ["change", "siamese", "buildings"], ["GF-2", "WorldView-3"],
             [0.5, 2.0], ["red", "green", "blue"], ["optical"], MEAN_STD_RGB,
             {"supported": True, "tile_size": 256, "overlap": 32, "batch_size": 1},
             classes=["no-change", "change"], accuracy=0.86,
             inputs=[
                 {"name": "before", "dtype": "float32", "layout": "NCHW",
                  "band_roles": ["red", "green", "blue"]},
                 {"name": "after", "dtype": "float32", "layout": "NCHW",
                  "band_roles": ["red", "green", "blue"]},
             ],
             source="https://github.com/template/siamese-change"),
    manifest("siamese-change-deforestation", "change_detection", "raster",
             "Deforestation change detection from bi-temporal Sentinel-2 "
             "pairs (named before/after inputs).",
             ["change", "forest", "siamese"], ["Sentinel-2"], [10.0, 20.0],
             ["red", "green", "blue", "nir"], ["optical"], MEAN_STD_S2_4B,
             {"supported": True, "tile_size": 256, "overlap": 32, "batch_size": 1},
             classes=["stable", "loss", "gain"], accuracy=0.83,
             inputs=[
                 {"name": "before", "dtype": "float32", "layout": "NCHW",
                  "band_roles": ["red", "green", "blue", "nir"]},
                 {"name": "after", "dtype": "float32", "layout": "NCHW",
                  "band_roles": ["red", "green", "blue", "nir"]},
             ],
             source="https://github.com/template/deforestation-change"),
    # --- cloud ---
    manifest("unet-cloud-s2", "segmentation", "raster",
             "Cloud / cloud-shadow segmentation on Sentinel-2 TOA "
             "(s2cloudless-style) for QA mask generation; emits an uncertainty "
             "band (softmax entropy).",
             ["cloud", "qa", "sentinel-2"], ["Sentinel-2"], [10.0, 60.0],
             ["blue", "green", "red", "nir"], ["optical"],
             {"normalize": "mean_std", "mean": [0.13, 0.14, 0.16, 0.20],
              "std": [0.20, 0.21, 0.23, 0.25], "scale": 1.0},
             {"supported": True, "tile_size": 256, "overlap": 32, "batch_size": 4},
             classes=["clear", "cloud", "shadow"], uncertainty="entropy",
             accuracy=0.94,
             source="https://github.com/template/s2cloudless"),
    # --- ships / airplanes ---
    manifest("yolo-ship-detection", "detection", "vector",
             "YOLO-family ship detection on Sentinel-2 / high-resolution "
             "coastal scenes.",
             ["ship", "yolo", "detection"], ["Sentinel-2", "GF-2"], [0.8, 10.0],
             ["red", "green", "blue", "nir"], ["optical"], LINEAR_255,
             {"supported": True, "tile_size": 640, "overlap": 96, "batch_size": 4},
             classes=["ship"], accuracy=0.87,
             source="https://github.com/template/yolo-ship"),
    manifest("yolo-airplane-detection", "detection", "vector",
             "Airplane detection on high-resolution airport imagery.",
             ["airplane", "yolo", "detection"], ["WorldView-3", "GF-2"],
             [0.3, 1.0], ["red", "green", "blue"], ["optical"], LINEAR_255,
             {"supported": True, "tile_size": 640, "overlap": 96, "batch_size": 4},
             classes=["airplane"], accuracy=0.90,
             source="https://github.com/template/yolo-airplane"),
    # --- SAR-specific ---
    manifest("unet-sar-ship-s1", "segmentation", "raster",
             "Sentinel-1 ship segmentation on VV sigma0 (linear power), robust "
             "to cloud/night acquisition.",
             ["ship", "sar", "sentinel-1"], ["Sentinel-1"], [10.0, 25.0],
             ["vv"], ["sar"], NONE_PREP,
             {"supported": True, "tile_size": 256, "overlap": 32, "batch_size": 4},
             polarizations=["VV"], radiometric_state="sigma0",
             classes=["sea", "ship"], accuracy=0.88,
             source="https://github.com/template/sar-ship"),
    manifest("unet-sar-flood-s1", "segmentation", "raster",
             "Flood extent mapping from Sentinel-1 VV/VH (sigma0 linear "
             "power); chain with rs:sar_calibrate and rs:sar_speckle first.",
             ["flood", "water", "sar"], ["Sentinel-1"], [10.0, 30.0],
             ["vv", "vh"], ["sar"], NONE_PREP,
             {"supported": True, "tile_size": 256, "overlap": 32, "batch_size": 4},
             polarizations=["VV", "VH"], radiometric_state="sigma0",
             classes=["dry", "flooded", "permanent-water"], accuracy=0.89,
             source="https://github.com/template/sar-flood"),
    # --- generic families ---
    manifest("unet-generic-binary", "segmentation", "raster",
             "Generic 3-band binary segmentation U-Net (fine-tune template for "
             "custom single-class targets).",
             ["generic", "unet", "template"], ["Sentinel-2", "PlanetScope"],
             [0.5, 20.0], ["red", "green", "blue"], ["optical"], MEAN_STD_RGB,
             {"supported": True, "tile_size": 256, "overlap": 32, "batch_size": 2},
             classes=["background", "target"], accuracy=-1.0,
             source="https://github.com/template/unet-generic"),
    manifest("swin-landcover-hr", "segmentation", "raster",
             "Swin-transformer land-cover segmentation for high-resolution "
             "imagery (windowed attention encoder).",
             ["landcover", "swin", "transformer"], ["WorldView-3", "GF-2"],
             [0.3, 2.0], ["red", "green", "blue", "nir"], ["optical"],
             MEAN_STD_RGB,
             {"supported": True, "tile_size": 512, "overlap": 64, "batch_size": 1},
             gpu=True, vram=6144,
             classes=["background", "built-up", "vegetation", "water", "bare"],
             accuracy=0.80,
             source="https://github.com/template/swin-lc"),
    manifest("ssl-embedding-encoder", "embedding", "raster",
             "Self-supervised remote-sensing embedding encoder (SSL backbone): "
             "produces a 128-channel feature stack for downstream classifiers.",
             ["embedding", "ssl", "features"], ["Sentinel-2", "Landsat-8"],
             [10.0, 30.0], ["red", "green", "blue", "nir"], ["optical"],
             MEAN_STD_S2_4B,
             {"supported": True, "tile_size": 224, "overlap": 28, "batch_size": 4},
             source="https://github.com/template/ssl-encoder"),
    manifest("temporal-siamese-crop-change", "change_detection", "raster",
             "Temporal crop-change detection: named before/after inputs, each "
             "T=6 frames collapsed to 12 channels (Siamese temporal encoder).",
             ["change", "crop", "temporal", "siamese"], ["Sentinel-2"],
             [10.0, 20.0], ["red", "nir"], ["optical"], MEAN_STD_S2_2B,
             {"supported": False, "tile_size": 128, "overlap": 0, "batch_size": 1},
             temporal_length=6, classes=["no-change", "crop-rotation"],
             accuracy=0.78,
             inputs=[
                 {"name": "before", "dtype": "float32", "layout": "NCHW",
                  "band_roles": ["red", "nir"], "temporal_length": 6,
                  "temporal_collapse": "channels"},
                 {"name": "after", "dtype": "float32", "layout": "NCHW",
                  "band_roles": ["red", "nir"], "temporal_length": 6,
                  "temporal_collapse": "channels"},
             ],
             source="https://github.com/template/temporal-siamese"),
]


def main():
    written = []
    for m in MODELS:
        d = os.path.join(MODELS_DIR, m["name"])
        os.makedirs(d, exist_ok=True)
        path = os.path.join(d, "model.json")
        with open(path, "w", encoding="utf-8") as f:
            json.dump(m, f, indent=2, ensure_ascii=False)
            f.write("\n")
        written.append(m["name"])
    print(f"wrote {len(written)} manifests")
    for n in written:
        print(" -", n)


if __name__ == "__main__":
    main()
