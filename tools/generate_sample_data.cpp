// generate_sample_data.cpp — Generate synthetic RS sample data for teaching
// Usage: ./generate_sample_data <output_dir>
//
// Generates:
// 1. landsat_sample.tif — 7-band Landsat-like image (256x256)
// 2. dem_sample.tif — Digital Elevation Model (256x256)
// 3. change_before.tif / change_after.tif — Before/after for change detection
// 4. training_samples.shp — ROI polygons for classification

#include <gdal.h>
#include <gdal_priv.h>
#include <ogr_api.h>
#include <ogr_srs_api.h>
#include <cpl_error.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <random>

// Synthetic land cover types with realistic spectral signatures
// Based on Landsat 8/9 OLI bands:
// Band 1: Coastal/Aerosol (0.43-0.45 μm)
// Band 2: Blue (0.45-0.51 μm)
// Band 3: Green (0.53-0.59 μm)
// Band 4: Red (0.64-0.67 μm)
// Band 5: NIR (0.85-0.88 μm)
// Band 6: SWIR1 (1.57-1.65 μm)
// Band 7: SWIR2 (2.11-2.29 μm)

struct LandCover {
    const char *name;
    float reflectance[7]; // Surface reflectance for each band
};

static const LandCover landCovers[] = {
    {"Water",      {0.05, 0.06, 0.08, 0.04, 0.02, 0.01, 0.005}},
    {"Vegetation", {0.04, 0.05, 0.08, 0.04, 0.45, 0.15, 0.08}},
    {"Urban",      {0.12, 0.13, 0.15, 0.17, 0.20, 0.25, 0.22}},
    {"Bare Soil",  {0.15, 0.18, 0.22, 0.30, 0.35, 0.40, 0.38}},
    {"Forest",     {0.03, 0.04, 0.06, 0.03, 0.50, 0.12, 0.06}},
    {"Shadow",     {0.01, 0.01, 0.02, 0.01, 0.01, 0.01, 0.005}},
};

static const int NUM_COVERS = sizeof(landCovers) / sizeof(landCovers[0]);

// Generate a simple land cover map (256x256)
// Layout: water at bottom, vegetation/forest in middle, urban at top, bare soil patches
static int getLandCoverClass(int x, int y, int width, int height) {
    // Normalize coordinates
    float nx = (float)x / width;
    float ny = (float)y / height;

    // Water at bottom
    if (ny > 0.85f) return 0; // Water

    // Urban area at top-left
    if (nx < 0.4f && ny < 0.3f) return 2; // Urban

    // Bare soil patches
    float dx = nx - 0.7f;
    float dy = ny - 0.5f;
    if (dx * dx + dy * dy < 0.04f) return 3; // Bare soil

    // Forest in the middle-right
    if (nx > 0.5f && ny > 0.3f && ny < 0.7f) return 4; // Forest

    // Shadow in valleys
    float sx = nx - 0.3f;
    float sy = ny - 0.6f;
    if (sx * sx + sy * sy < 0.01f) return 5; // Shadow

    // Default: vegetation
    return 1; // Vegetation
}

// Generate DEM with hills and valleys
static float generateDEM(int x, int y, int width, int height) {
    float nx = (float)x / width;
    float ny = (float)y / height;

    // Base elevation with gentle slope
    float elevation = 100.0f + 200.0f * ny;

    // Add hills
    float hill1 = 150.0f * exp(-((nx - 0.3f) * (nx - 0.3f) + (ny - 0.4f) * (ny - 0.4f)) / 0.02f);
    float hill2 = 100.0f * exp(-((nx - 0.7f) * (nx - 0.7f) + (ny - 0.6f) * (ny - 0.6f)) / 0.03f);

    // Add valley
    float valley = -80.0f * exp(-((nx - 0.5f) * (nx - 0.5f)) / 0.01f) *
                   exp(-((ny - 0.5f) * (ny - 0.5f)) / 0.1f);

    // Add some noise for realism
    float noise = 5.0f * sin(nx * 20.0f) * cos(ny * 15.0f);

    return elevation + hill1 + hill2 + valley + noise;
}

// Generate change detection data
static void generateChangeData(int x, int y, int width, int height,
                                float &before_val, float &after_val) {
    float nx = (float)x / width;
    float ny = (float)y / height;

    // Before: vegetation area
    before_val = 0.4f + 0.1f * sin(nx * 10.0f) * cos(ny * 8.0f);

    // After: some deforestation (vegetation loss in a patch)
    float deforest_x = nx - 0.4f;
    float deforest_y = ny - 0.5f;
    float deforest_dist = deforest_x * deforest_x + deforest_y * deforest_y;

    if (deforest_dist < 0.03f) {
        // Deforested area - lower reflectance
        after_val = 0.15f + 0.05f * sin(nx * 20.0f);
    } else {
        // Unchanged vegetation
        after_val = before_val + 0.02f * sin(nx * 5.0f);
    }
}

static bool writeGeoTIFF(const char *filename, int width, int height, int bands,
                          float *data, double *geotransform, const char *proj) {
    GDALDriverH driver = GDALGetDriverByName("GTiff");
    if (!driver) {
        fprintf(stderr, "Failed to get GTiff driver\n");
        return false;
    }

    char **options = nullptr;
    options = CSLSetNameValue(options, "COMPRESS", "LZW");

    GDALDatasetH dataset = GDALCreate(driver, filename, width, height, bands,
                                       GDT_Float32, options);
    CSLDestroy(options);

    if (!dataset) {
        fprintf(stderr, "Failed to create %s\n", filename);
        return false;
    }

    GDALSetGeoTransform(dataset, geotransform);
    GDALSetProjection(dataset, proj);

    for (int b = 0; b < bands; b++) {
        GDALRasterBandH band = GDALGetRasterBand(dataset, b + 1);
        CPLErr err = GDALRasterIO(band, GF_Write, 0, 0, width, height,
                                   data + b * width * height, width, height,
                                   GDT_Float32, 0, 0);
        if (err != CE_None) {
            fprintf(stderr, "Failed to write band %d of %s\n", b + 1, filename);
            GDALClose(dataset);
            return false;
        }
    }

    GDALClose(dataset);
    return true;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <output_dir>\n", argv[0]);
        return 1;
    }

    const char *outputDir = argv[1];
    GDALAllRegister();

    const int width = 256;
    const int height = 256;

    // Georeferencing: simple geographic coordinates (WGS84)
    double geotransform[6] = {
        116.0,   // Top-left X (longitude)
        0.001,   // Pixel width
        0.0,     // Rotation
        40.0,    // Top-left Y (latitude)
        0.0,     // Rotation
        -0.001   // Pixel height (negative)
    };

    const char *proj = "GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,298.257223563]],PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]]";

    std::mt19937 rng(42); // Fixed seed for reproducibility
    std::normal_distribution<float> noise(0.0f, 0.02f);

    // 1. Generate Landsat-like multi-band image
    printf("Generating landsat_sample.tif...\n");
    {
        const int numBands = 7;
        std::vector<float> data(width * height * numBands);

        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int cls = getLandCoverClass(x, y, width, height);
                const LandCover &lc = landCovers[cls];

                for (int b = 0; b < numBands; b++) {
                    float val = lc.reflectance[b] + noise(rng);
                    val = std::max(0.0f, std::min(1.0f, val));
                    data[b * width * height + y * width + x] = val;
                }
            }
        }

        std::string path = std::string(outputDir) + "/landsat_sample.tif";
        if (!writeGeoTIFF(path.c_str(), width, height, numBands, data.data(),
                           geotransform, proj)) {
            return 1;
        }
    }

    // 2. Generate DEM
    printf("Generating dem_sample.tif...\n");
    {
        std::vector<float> data(width * height);

        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                data[y * width + x] = generateDEM(x, y, width, height);
            }
        }

        std::string path = std::string(outputDir) + "/dem_sample.tif";
        if (!writeGeoTIFF(path.c_str(), width, height, 1, data.data(),
                           geotransform, proj)) {
            return 1;
        }
    }

    // 3. Generate change detection data (before/after)
    printf("Generating change_before.tif and change_after.tif...\n");
    {
        std::vector<float> before(width * height);
        std::vector<float> after(width * height);

        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                float b, a;
                generateChangeData(x, y, width, height, b, a);
                before[y * width + x] = b + noise(rng);
                after[y * width + x] = a + noise(rng);
            }
        }

        std::string path_before = std::string(outputDir) + "/change_before.tif";
        std::string path_after = std::string(outputDir) + "/change_after.tif";

        if (!writeGeoTIFF(path_before.c_str(), width, height, 1, before.data(),
                           geotransform, proj)) {
            return 1;
        }
        if (!writeGeoTIFF(path_after.c_str(), width, height, 1, after.data(),
                           geotransform, proj)) {
            return 1;
        }
    }

    // 4. Generate training ROI shapefile
    printf("Generating training_samples.shp...\n");
    {
        OGRSFDriverH shpDriver = OGRGetDriverByName("ESRI Shapefile");
        if (!shpDriver) {
            fprintf(stderr, "Failed to get Shapefile driver\n");
            return 1;
        }

        std::string shpPath = std::string(outputDir) + "/training_samples.shp";
        OGRDataSourceH dataSource = OGR_Dr_CreateDataSource(shpDriver, shpPath.c_str(), nullptr);
        if (!dataSource) {
            fprintf(stderr, "Failed to create shapefile\n");
            return 1;
        }

        OGRSpatialReferenceH srs = OSRNewSpatialReference(proj);
        OGRLayerH layer = OGR_DS_CreateLayer(dataSource, "training", srs, wkbPolygon, nullptr);
        OSRDestroySpatialReference(srs);

        // Add class field
        OGRFieldDefnH fieldDef = OGR_Fld_Create("class_name", OFTString);
        OGR_Fld_SetWidth(fieldDef, 20);
        OGR_L_CreateField(layer, fieldDef, 1);
        OGR_Fld_Destroy(fieldDef);

        // Add class ID field
        OGRFieldDefnH idFieldDef = OGR_Fld_Create("class_id", OFTInteger);
        OGR_L_CreateField(layer, idFieldDef, 1);
        OGR_Fld_Destroy(idFieldDef);

        // Create ROI polygons for each land cover class
        struct ROIDef {
            const char *name;
            int classId;
            double x1, y1, x2, y2; // Bounding box in georeferenced coords
        };

        ROIDef rois[] = {
            {"Water",      1, 116.05, 39.87, 116.15, 39.90},
            {"Vegetation", 2, 116.10, 39.92, 116.20, 39.96},
            {"Urban",      3, 116.02, 39.97, 116.12, 40.00},
            {"Bare Soil",  4, 116.15, 39.93, 116.22, 39.97},
            {"Forest",     5, 116.18, 39.94, 116.25, 39.98},
        };

        for (const auto &roi : rois) {
            OGRFeatureH feature = OGR_F_Create(OGR_L_GetLayerDefn(layer));
            OGR_F_SetFieldString(feature, OGR_F_GetFieldIndex(feature, "class_name"), roi.name);
            OGR_F_SetFieldInteger(feature, OGR_F_GetFieldIndex(feature, "class_id"), roi.classId);

            OGRGeometryH ring = OGR_G_CreateGeometry(wkbLinearRing);
            OGR_G_AddPoint_2D(ring, roi.x1, roi.y1);
            OGR_G_AddPoint_2D(ring, roi.x2, roi.y1);
            OGR_G_AddPoint_2D(ring, roi.x2, roi.y2);
            OGR_G_AddPoint_2D(ring, roi.x1, roi.y2);
            OGR_G_AddPoint_2D(ring, roi.x1, roi.y1);

            OGRGeometryH polygon = OGR_G_CreateGeometry(wkbPolygon);
            OGR_G_AddGeometry(polygon, ring);
            OGR_G_DestroyGeometry(ring);

            OGR_F_SetGeometry(feature, polygon);
            OGR_G_DestroyGeometry(polygon);

            OGR_L_CreateFeature(layer, feature);
            OGR_F_Destroy(feature);
        }

        OGR_DS_Destroy(dataSource);
    }

    printf("Sample data generation complete!\n");
    printf("Files created in: %s\n", outputDir);
    printf("  - landsat_sample.tif (7-band Landsat-like image)\n");
    printf("  - dem_sample.tif (Digital Elevation Model)\n");
    printf("  - change_before.tif (Pre-change image)\n");
    printf("  - change_after.tif (Post-change image)\n");
    printf("  - training_samples.shp (Training ROI polygons)\n");

    return 0;
}
