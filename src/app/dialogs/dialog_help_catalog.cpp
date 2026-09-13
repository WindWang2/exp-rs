// dialog_help_catalog.cpp — Shared parameter-level help for all app tools.
#include "dialog_help_catalog.h"

#include <QAction>
#include <QDialog>
#include <QHash>
#include <QMessageBox>
#include <QObject>
#include <QWidget>

namespace
{

struct Entry
{
    const char *summary; // short one-line
    const char *body;    // multi-sentence plain (wrapped as <p>)
};

const QHash<QString, Entry> &catalog()
{
    static const QHash<QString, Entry> k = {
        // ========== Raster processing dialogs (toolName) ==========
        { QStringLiteral( "spectral_index" ),
          { "Spectral indices: NDVI / EVI / SAVI / NDWI / NDBI / MNDWI",
            "[Index]\n"
            "• NDVI = (NIR−Red)/(NIR+Red): vegetation vigour, −1 to 1\n"
            "• EVI: enhanced vegetation index; needs NIR/Red/Blue and suppresses atmosphere and soil\n"
            "• SAVI: soil-adjusted vegetation index; better for sparse vegetation\n"
            "• NDWI = (Green−NIR)/(Green+NIR): water / moisture\n"
            "• NDBI = (SWIR−NIR)/(SWIR+NIR): built-up areas\n"
            "• MNDWI = (Green−SWIR)/(Green+SWIR): modified water index\n"
            "[Bands] Map NIR / Red / Green / Blue / SWIR per sensor (band numbers start at 1)."
            "Pre-filled in the common Landsat / Sentinel order; verify against your actual data.\n"
            "[Output] Single-band float GeoTIFF. The path is required before running." } },
        { QStringLiteral( "terrain" ),
          { "DEM terrain analysis: slope / aspect / hillshade, etc.",
            "[DEM layer] Elevation raster; units should match the CRS (a metric projection is more reliable).\n"
            "[Analysis type]\n"
            "• Slope: slope (degrees)\n"
            "• Aspect: aspect (degrees; north = 0, clockwise)\n"
            "• Hillshade: hillshade (needs solar azimuth / elevation)\n"
            "• Roughness / TRI / TPI: roughness and topographic position indices\n"
            "[Cell Size] Ground resolution (map units); usually estimated automatically once a layer is chosen.\n"
            "[Solar azimuth / elevation] Hillshade only: azimuth 0–360° (north = 0), elevation 0–90°.\n"
            "[Output] Single-band result GeoTIFF." } },
        { QStringLiteral( "extract_band" ),
          { "Extract a Single Band from a Multiband Raster",
            "[Raster layer] A project raster with more than 1 band.\n"
            "[Band] Band number / name to save separately.\n"
            "[Output] A single-band GeoTIFF for single-band analysis or combination with other data." } },
        { QStringLiteral( "temporal_analysis" ),
          { "Time series analysis: multitemporal statistics / compositing / index time series / trends / anomalies / series extraction",
            "[Epoch scenes] Add multiple epoch rasters; acquisition times are parsed from product metadata or file names and can be edited.\n"
            "[Precheck] Checks before running: time completeness, duplicate epochs, grid consistency (CRS / resolution / origin, no implicit resampling),"
            "Band-role resolvability, radiometric state and scale/offset consistency, QA band availability.\n"
            "[Analysis] Time series statistics (Welford mean/variance), best-pixel compositing (quality score + observation count), index time series (same kernel as single scenes),"
            "Linear trends (real time intervals), anomalies (z-score / difference), point and ROI series (CSV).\n"
            "[Memory] Processes in streaming tiles; working memory is independent of the date count, so tens to hundreds of epochs are safe." } },
        { QStringLiteral( "mosaic" ),
          { "Mosaic multiple rasters into a continuous image",
            "[Input list] At least 2 raster files; projections should match, and the engine merges overlaps with its default strategy.\n"
            "[Add / Remove] Manage the files taking part in the mosaic.\n"
            "[Output] The mosaicked GeoTIFF. Watch disk and memory for large images." } },
        { QStringLiteral( "atmospheric_correction" ),
          { "Atmospheric Correction / DN to Radiance",
            "[Method]\n"
            "• DN to Radiance: L = gain×DN + bias, requires sensor gain/offset\n"
            "• DOS1: dark object subtraction estimating path radiance\n"
            "• DOS2: DOS1 plus transmittance; needs the airmass\n"
            "• QUAC: fast all-band atmospheric correction from image statistics; outputs approximate surface reflectance [0,1] with no external parameters\n"
            "[Band] Band number to process (QUAC processes all bands and ignores this).\n"
            "[Gain / Bias] Radiometric calibration coefficients (from metadata or the product handbook; ignored by QUAC).\n"
            "[Airmass] DOS2 only: the airmass, usually ≥ 1.\n"
            "[Output] The corrected raster." } },
        { QStringLiteral( "contrast_stretch" ),
          { "Contrast stretching to improve display and downstream analysis",
            "[Method]\n"
            "• Linear: min–max linear stretch to the output range\n"
            "• Percentage Clip: clips Clip% at both tails before stretching, suppressing extremes\n"
            "• Std Dev: stretch to mean±K×std dev\n"
            "• Histogram Equalization: enhances global contrast\n"
            "[Clip %] Percent clip only; 1–2% is typical.\n"
            "[Std Dev K] Std-dev method only; 2 is typical.\n"
            "[Output] Stretched multiband GeoTIFF (band by band over the input)." } },
        { QStringLiteral( "fusion" ),
          { "Pansharpening / Image Fusion",
            "[Panchromatic] High spatial resolution single band.\n"
            "[Multispectral] Lower resolution multiband. Both must cover roughly the same extent and be registered.\n"
            "[Method]\n"
            "• Linear Weighted: weighted fusion with adjustable Pan Weight and per-band weights\n"
            "• Brovey: ratio-based fusion, fast\n"
            "• IHS: requires RGB bands\n"
            "• PCA: principal component substitution\n"
            "• OTB BundleToPerfectSensor / GDAL pansharpen: external toolchains\n"
            "[Pan Weight] Panchromatic share in the linear method, 0–1.\n"
            "[RGB Bands] IHS only: red / green / blue band numbers within the multispectral image.\n"
            "[Output] The sharpened multispectral GeoTIFF." } },
        { QStringLiteral( "change_detection" ),
          { "Two-Date Change Detection",
            "[Earlier / later images] Must be geometrically aligned (same projection, ideally same resolution); registration can be done first.\n"
            "[Band] The band used in the computation for each epoch (usually the same-named band or the same index).\n"
            "[Method]\n"
            "• Difference: later − earlier\n"
            "• Normalized Difference: (later − earlier)/(later + earlier)\n"
            "• Change Mask: binary change mask where the difference exceeds the threshold\n"
            "[Threshold] Change Mask only: the change threshold (same scale as the DN).\n"
            "[Output] Difference or mask GeoTIFF." } },
        { QStringLiteral( "speckle_filter" ),
          { "SAR Speckle Filtering",
            "[Filter] Lee / Frost / Kuan / Gamma-MAP; all suppress speckle with different edge preservation.\n"
            "[Window] 3×3 / 5×5 / 7×7; larger values smooth more and keep less detail.\n"
            "[Noise Variance] Noise variance estimate for Lee / Kuan / Gamma-MAP; adjust per sensor.\n"
            "[Damping] Frost only: the damping factor; larger values smooth more.\n"
            "[Output] The filtered raster (band count preserved)." } },
        { QStringLiteral( "band_ratio" ),
          { "Band ratio or IHS transform",
            "[Mode]\n"
            "• Band Ratio: numerator / denominator, highlighting spectral differences of specific features\n"
            "• IHS Transform: converts RGB into intensity-hue-saturation space\n"
            "[Numerator / Denominator] Numerator and denominator bands of the ratio.\n"
            "[R/G/B] The three input bands of the IHS transform.\n"
            "[Output] Single-band ratio or multiband IHS result." } },
        { QStringLiteral( "pca" ),
          { "Principal Component Analysis (PCA)",
            "[Components] Number of output components, ≤ the input band count.\n"
            "The first components usually contain most of the variance; used for decorrelation, dimensionality reduction and visual enhancement.\n"
            "[Output] Multiband PCA GeoTIFF (bands = PC1, PC2, ...)." } },
        { QStringLiteral( "spatial_filter" ),
          { "Spatial Convolution Filtering",
            "[Filter]\n"
            "• Mean / Gaussian / Median: smooth noise (median preserves edges better)\n"
            "• Sobel / Laplacian: edge enhancement\n"
            "[Kernel Size] Convolution kernel 3×3 or 5×5.\n"
            "[Output] The filtered raster." } },
        { QStringLiteral( "image_enhancement" ),
          { "Combined Image Enhancement Panel",
            "[Method] Switches within the same dialog: contrast stretch / spatial filtering / band ratio · IHS / SAR speckle filtering.\n"
            "Each sub-page's parameters match the corresponding standalone menu tool; see the hover descriptions.\n"
            "[Output] Enhanced result GeoTIFF." } },
        { QStringLiteral( "band_math" ),
          { "Band Math Expression",
            "[Expression] Arithmetic expression; bands are b1, b2, ... (starting at 1).\n"
            "Examples: (b1-b2)/(b1+b2) is an NDVI-style operation; b1*0.0001 rescales.\n"
            "Supports + − * / and parentheses.\n"
            "[Output] Single-band computation result." } },
        { QStringLiteral( "apply_mask" ),
          { "Apply Mask: set obscured pixels to NoData with a binary / QA mask",
            "[Input Layer] The multiband product raster to process.\n"
            "[Mask Layer] A binary or quality mask raster (1 / non-zero = obscured or invalid, 0 = clear and valid).\n"
            "[Auto-align grid] If the mask and input raster differ in resolution or extent, nearest-neighbour resampling aligns them automatically.\n"
            "[Output NoData] Replacement value for obscured pixels (metadata NoData by default, or custom e.g. -9999).\n"
            "[Output] The masked multiband GeoTIFF." } },
        { QStringLiteral( "radiometric_calibration" ),
          { "Radiometric Calibration: DN to radiance / TOA reflectance / brightness temperature",
            "[Physical quantity]\n"
            "• Radiance: W/(m²·sr·µm), computed from gain and offset\n"
            "• TOA Reflectance: dimensionless reflectance [0, 1], corrected with the solar elevation and sun–earth distance\n"
            "• Brightness Temperature: Kelvin temperature (K) of thermal infrared bands\n"
            "[Band] Tick 'process all bands' or choose a specific band.\n"
            "[Metadata file] Auto-detect or manually choose a Landsat MTL text or Sentinel-2 MTD XML to extract calibration gain/offset and the sun elevation.\n"
            "[Output] Calibrated float GeoTIFF." } },
        { QStringLiteral( "qa_mask" ),
          { "Generate QA Mask: extract cloud, cloud shadow, snow or water",
            "[Quality source]\n"
            "• Auto: identify the QA band automatically from sensor metadata\n"
            "• Landsat QA_PIXEL: parses the Landsat 8/9 quality assessment bitmask\n"
            "• Sentinel-2 SCL: parses the Scene Classification Layer\n"
            "• Generic Bitmask: bitwise-AND extraction with a generic integer mask\n"
            "[Mask category] Cloud and cloud shadow, cloud only, shadow only, snow/ice, water, or all invalid pixels.\n"
            "[Bit mask value] Generic bitmask mode only: the integer value used in the test.\n"
            "[Output] Single-band binary mask GeoTIFF (1 = obscured / invalid, 0 = clear and valid)." } },
        { QStringLiteral( "post_classification_change" ),
          { "Post-classification change detection: two-date classification comparison and transition matrix",
            "[Earlier / later layers] Single-band classification rasters of the same area at two epochs (pixel values are integer class ids).\n"
            "[Num Classes] Number of classes used to build the transition matrix (0 = auto-detect the maximum class id).\n"
            "[Transition Matrix] Rows are earlier classes, columns later classes; summarises class flows and area transitions.\n"
            "[Output] A change-type map GeoTIFF (pixel value encoded as earlier-id × base + later-id) plus a detailed statistics report." } },
        { QStringLiteral( "orthorectification" ),
          { "Orthorectification: geometric orthorectification based on RPC / GCPs and a DEM",
            "[Geometry model] Detects the rational polynomial coefficients (RPC) or ground control points (GCPs) carried by the input raster automatically.\n"
            "[Target CRS] The projected CRS of the orthorectification output (a metric projection such as UTM is recommended).\n"
            "[DEM terrain correction] Specify an elevation raster to remove terrain-induced geometric distortion; a reference elevation can be given when none is provided.\n"
            "[Resampling] Bilinear (smooth, continuous) / Nearest (preserves pixel values) / Cubic / Lanczos.\n"
            "[Cell Size] Target resolution (map units); 0 infers it automatically from the sensor resolution.\n"
            "[Output] The orthorectified GeoTIFF." } },

        // ========== Standalone dialogs ==========
        { QStringLiteral( "batch_processing" ),
          { "Batch processing: one algorithm over many files",
            "[Algorithm] Choose an algorithm from the processing registry (GDAL / OTB / built-in, etc.).\n"
            "[Input Files] Add / Remove manage the list of files to process.\n"
            "[Output Directory] All results are written here (file names derived from the inputs).\n"
            "[Run Batch] Executes in order; a progress bar and status line provide feedback.\n"
            "Suits repeatable pipelines; validate complex parameters on a single file in the toolbox first." } },
        { QStringLiteral( "preferences" ),
          { "Preferences: theme, CRS, logging and external tool paths",
            "[Theme] Light / dark interface theme.\n"
            "[Default CRS] Default CRS for new projects.\n"
            "[Log to file / Log File] Whether to write a log file, and its path.\n"
            "[GDAL Path / OTB Path] External executable directories used by the CLI wrapper algorithms.\n"
            "Some options take full effect only after a restart." } },
        { QStringLiteral( "stac_browser" ),
          { "STAC Catalog Search and Asset Loading",
            "[Endpoint] STAC API root URL, e.g. Element84 Earth Search.\n"
            "[Collection] Dataset ID, e.g. sentinel-2-l2a.\n"
            "[Datetime] Time filter (ISO interval or instant, as supported by the catalog).\n"
            "【BBox】min_lon,min_lat,max_lon,max_lat。\n"
            "[Search] Searches items; the table shows ID / collection / time / asset count.\n"
            "[Load Selected Asset] Loads the selected assets into the project (network and permissions required)." } },
        { QStringLiteral( "comparison" ),
          { "Side-by-side visual layer comparison",
            "[Left / Right Layer] Raster layers from the project.\n"
            "[Load] Loads into the comparison view to inspect registration, change or classification differences.\n"
            "Complements the main window's swipe: this tool is a side-by-side comparison." } },
        { QStringLiteral( "crs_preset" ),
          { "Common CRS Presets",
            "[Search] Filter by name or EPSG.\n"
            "[Tree list] Browse presets in groups.\n"
            "[Details] EPSG, name and WKT summary of the selection.\n"
            "Double-click or OK applies the CRS to the project / caller." } },
        { QStringLiteral( "processing_algorithm" ),
          { "Processing Algorithm Dialog (Toolbox)",
            "[Parameter table] Hover any parameter label for its description; required fields are validated before running.\n"
            "[Advanced] Advanced parameters are collapsed by default.\n"
            "[Load result layers] Joins the layer tree automatically when finished.\n"
            "[Command] Live preview of the GDAL / OTB / generic CLI call; copy it to a terminal.\n"
            "The help page shows the algorithm shortHelp; it complements the RS-specific dialogs behind the menu entries." } },
        { QStringLiteral( "sift_match" ),
          { "SIFT Auto-Matching GCP Generation",
            "[Contrast] Feature contrast threshold; larger values give fewer but steadier points.\n"
            "[Max Matches] Upper bound on the number of matched pairs kept.\n"
            "[Min Inlier] Minimum RANSAC inlier ratio.\n"
            "[RANSAC Threshold] Pixel tolerance.\n"
            "[Max Image Side] Maximum edge length to scale to before matching (speed-up).\n"
            "Even after importing results into the GCP table, inspect outliers visually." } },
        { QStringLiteral( "map_coords" ),
          { "GCP Target Coordinate Input",
            "Enter or pick control point target coordinates from the map, pairing them with source pixel positions."
            "In I2I two-canvas mode, points are usually picked on both sides directly; this form is rarely used." } },

        // ========== Georeferencer ==========
        { QStringLiteral( "georef_i2i" ),
          { "Image to Image Registration",
            "[Canvases] left = source (Warp), right = reference (Base); Add / Move / Delete GCP need both sides open.\n"
            "[Open] A file or a main project layer.\n"
            "[Sync zoom] Off by default; do not force sync with different CRSs or when checking row/column on the same scene.\n"
            "[SIFT] Auto-matches GCPs (OpenCV).\n"
            "[Correction parameters] The panel on the right: transform / resampling / RMS / CRS / output (see 'Parameter Description').\n"
            "[GCP table] Row/column and residuals; right-click to locate / enable-disable / delete.\n"
            "[Task] Tracks warp progress after running and loads the result." } },
        { QStringLiteral( "georef_i2m" ),
          { "Image to Map Registration",
            "[Source] A file or project layer; [Base] a visible main project map layer.\n"
            "[Transform] Includes RPC Physical (needs RPC metadata, optional DEM).\n"
            "The rest is similar to I2I: the GCP table, correction parameters and task list." } },
        { QStringLiteral( "georef_params" ),
          { "Geometric correction parameters (panel on the right)",
            "[Transform] Linear / Helmert ≥ 2 points; polynomial 1/2/3 about 3/6/10 points; TPS / projective / RPC.\n"
            "Minimum / actual points / DOF: DOF = actual − minimum; residual assessment needs DOF > 0; at DOF = 0 residuals carry no statistical meaning.\n"
            "[Resampling] Nearest preserves classes; Bilinear / Cubic for continuous imagery; cell size auto; the background value fills gaps.\n"
            "[RMS] In source pixel units; scatter plus X / Y / total / maximum residuals.\n"
            "[CRS] The target CRS determines the output and the fit; in I2I it usually follows the reference.\n"
            "[Output] A path is required before running. [DEM] RPC only." } },
        { QStringLiteral( "georef_gcp_table" ),
          { "GCP Table",
            "Columns: map coordinates, pixel column/row on both sides, residuals ΔX/ΔY/RMS, enabled state.\n"
            "Right-click: locate, enable / disable, edit, delete. Delete removes the selection.\n"
            "For same-scene registration, 'col src / row src' should be close to 'col ref / row ref'." } },
        { QStringLiteral( "georef_tasks" ),
          { "Correction Task List",
            "Shows progress after running; can be cancelled; results load into the project when finished." } },

        // ========== Classification / OBIA ==========
        { QStringLiteral( "classification" ),
          { "Pixel-Level Supervised Classification",
            "[Workflow] Load image → define classes → collect ROIs → set algorithm / bands / ignored values → train and classify → accuracy assessment.\n"
            "[ROI Tools] Collect training samples with point / rectangle / polygon / freehand / magic wand.\n"
            "[Setup bar] Algorithm, bands, training ratio, NoData / ignored values, output path; preview / cross-validation / apply.\n"
            "[Accuracy] OA, Kappa, confusion matrix, producer's / user's accuracy." } },
        { QStringLiteral( "classify_setup" ),
          { "Classifier Setup Bar Parameters",
            "[Algorithm] Normal Bayes / SVM / K-means (RF / Mahalanobis / UNet placeholders).\n"
            "[Bands] Comma-separated, e.g. 1,2,3.\n"
            "[Training ratio] Stratified training share; the rest is a holdout for accuracy.\n"
            "[Output] Classification result GeoTIFF.\n"
            "[Use source NoData] Metadata NoData values are ignored.\n"
            "[Ignored values] Extra DN list (e.g. 0-fill edges).\n"
            "[Matching] Ignore the whole pixel if any band is ignored (default), or only when all bands are ignored.\n"
            "[Preview] Current viewport only. [Cross-Validation] K-fold evaluation. [Apply] Full-image classification." } },
        { QStringLiteral( "obia" ),
          { "Object-Based Classification (OBIA)",
            "[Load Raster] Loads the image to segment.\n"
            "[Segments Kernel] Smoothing kernel size 3–21; larger values give coarser objects.\n"
            "[Bins] Quantization levels (built-in segmentation fallback), 2–128.\n"
            "[Min region] Minimum object pixel count; suppresses small patches.\n"
            "[Segment] Runs segmentation. [Classifier] Normal Bayes / SVM / K-means.\n"
            "[Classify] Object-level classification. [Export] Export results.\n"
            "[Classes table] Class ID / name / color. Suits high-resolution imagery." } },
        { QStringLiteral( "obia_class_table" ),
          { "OBIA Class Table",
            "[Columns] ID / name / color.\n"
            "[ID] Corresponds to the classification raster pixel value, starting at 1; not directly editable.\n"
            "[Right-click] Edit name, change color, insert / delete classes.\n"
            "[Assign] Assigns the current class to the objects selected on the canvas." } },
        { QStringLiteral( "obia_segment_table" ),
          { "OBIA Object List",
            "[Columns] ID / pixel count / class.\n"
            "[Right-click] Locate the object on the canvas, assign the current class, copy the ID.\n"
            "Refills from the current level when switching levels; segmentation must be done first." } },
        { QStringLiteral( "obia_segment_info" ),
          { "OBIA Object Info",
            "Shows shape, spectral and hierarchy statistics of the object selected on the canvas (read-only HTML).\n"
            "Use the 'Select Objects' map tool to click objects on the canvas and refresh." } },
        { QStringLiteral( "obia_task_list" ),
          { "Task Center (OBIA task list)",
            "[Columns] Title / status / progress / load checkbox.\n"
            "[Right-click] View details and log, stop, pause/resume, retry, load outputs into the main view, copy info.\n"
            "[Load checkbox] Loads outputs into the main program automatically after the task succeeds.\n"
            "[Status colors] blue = running, green = finished, red = failed, grey = queued / cancelled." } },
        { QStringLiteral( "obia_data_manager" ),
          { "Data Management",
            "[Tree] Project data assets and collections; the color bar on the left shows status (green = available, red = unavailable).\n"
            "[Right-click] Add to display, promote to project persistent, unload, view properties, copy source path.\n"
            "[Double-click] Same as 'Add to Display'. The meta information inspector is below." } },
        { QStringLiteral( "accuracy" ),
          { "Classification Accuracy Assessment",
            "[OA] Overall accuracy. [Kappa] Agreement coefficient.\n"
            "[Confusion matrix] Rows = truth, columns = prediction.\n"
            "[Producer's accuracy] Share of a class's true samples classified correctly (recall).\n"
            "[User's accuracy] Share of a class's predictions that are correct (precision).\n"
            "[F1] Harmonic mean of producer's and user's accuracy. [Export CSV] Saves the report." } },
        { QStringLiteral( "post_process" ),
          { "Post-Classification",
            "[Vectorize] Converts the classification raster into vector polygons.\n"
            "[Filter] Filters small patches by minimum area.\n"
            "[Merge] Merges small patches into adjacent major classes.\n"
            "[Output] Exports the processed raster / vector." } },
        { QStringLiteral( "classifier_load" ),
          { "Load Trained Classifier",
            "Loads a previously saved classifier model file from disk.\n"
            "Once loaded, the classification can be applied to new images directly, without retraining." } },
        { QStringLiteral( "merge_classes" ),
          { "Class Merging",
            "Merges several fine classes into one coarse class, updating the class table and classified results.\n"
            "Executes the merge after source and target classes are chosen." } },
        { QStringLiteral( "template_match" ),
          { "Geometric Correction - Template Matching",
            "[Template] Drag a rectangle around the reference template area on the source image.\n"
            "[Search] Auto-matches conjugate points on the image to correct, generating GCPs.\n"
            "[Correlation threshold] Controls match confidence; too low invites mismatches, too high misses points." } },
        { QStringLiteral( "landsat_import" ),
          { "Import Landsat Product",
            "[Scene directory] An extracted directory containing *_MTL.txt.\n"
            "[Probe] Parses the MTL and lists sub-items (grid groups) and bands.\n"
            "[Preview tree] Tick the bands to import; multispectral bands by default.\n"
            "[Import] Composites as selected and loads into the project." } },
        { QStringLiteral( "digitize_tools" ),
          { "Vector Digitizing Edit Tools",
            "[Select] Select features with a rectangle. [Add Feature] Draw a new feature.\n"
            "[Node Tool] Edit vertices. [Move / Rotate] Transform the whole feature.\n"
            "[Reshape] Modify boundaries. [Split] Split features. [Offset] Offset lines.\n"
            "[Simplify] Thins vertices. [Reverse] Reverses line direction.\n"
            "[Add Ring / Fill Ring] Handles holes inside polygons. [Delete Part] Removes one part of a multipart feature." } },

        // ========== Main / misc ==========
        { QStringLiteral( "main_window" ),
          { "Main Window — SICNU GEO RS",
            "[Menu layout]\n"
            "• Project: open/save, import, STAC, layouts and reports\n"
            "• Edit: feature editing; digitizing tools live in the 'Edit → Digitizing' submenu\n"
            "• View: zoom, pan, identify, measure, compare / swipe\n"
            "• Layer: add/remove rasters and vectors, project CRS\n"
            "• Raster: preprocessing (incl. image registration), image enhancement, bands and transforms\n"
            "• Analysis: spectral indices, change detection, fusion, terrain, classification (thematic)\n"
            "• Vector: geometry processing, overlay, spatial selection, attributes and projection\n"
            "• Processing: toolbox / history / batch\n"
            "• Settings / Window / Help\n"
            "Hover menu items and dialog widgets for parameter explanations; Shift+F1 is 'What's This?'." } },
        { QStringLiteral( "swipe" ),
          { "Swipe Comparison Tool",
            "Drag the divider on the map to compare the layers above and below, checking registration or change." } },
        { QStringLiteral( "sentinel2_import" ),
          { "Import Sentinel-2 Product: detect and import L1C / L2A SAFE directories",
            "[Product directory] An extracted Sentinel-2 .SAFE folder or a directory containing MTD_MSIL*.xml.\n"
            "[Probe] Parses the metadata XML, identifying the 10 m/20 m/60 m resolution grid groups and the SCL quality band.\n"
            "[Band selection] Tick the multispectral bands or derived indices to import in the candidate tree.\n"
            "[Import] Loads the selected bands into the data manager automatically and registers them as a dataset collection." } },
        { QStringLiteral( "modis_import" ),
          { "Import MODIS Product: detect and import HDF / GeoTIFF tiles",
            "[Product path] A MODIS HDF4 scientific dataset file (e.g. MOD09GA, MOD13Q1) or an extracted tile directory.\n"
            "[Tile] Auto-identify or specify the hXXvYY sinusoidal grid number.\n"
            "[Probe] Parses the internal subdataset band list and scale factors.\n"
            "[Import] Imports the selected surface reflectance / vegetation index bands into the project." } },
        { QStringLiteral( "product_import" ),
          { "Satellite product import: detect Landsat / Sentinel-2 / MODIS packages",
            "[Product directory] Choose a product folder containing standard sensor metadata.\n"
            "[Probe] Reads the metadata and analyses grid resolution, band names and wavelength ranges.\n"
            "[Preview tree] Shows bands grouped by resolution and purpose; tick to import into the project." } },
        { QStringLiteral( "spectral_library" ),
          { "Spectral library matching and management: SAM / SID spectral angle matching",
            "[Spectral library file] Choose a USGS / ASTER format library or a JSON spectral library exported by this project.\n"
            "[Matching algorithm]\n"
            "• SAM (Spectral Angle Mapper): spectral angle in high-dimensional space; smaller is more similar\n"
            "• SID (Spectral Information Divergence): spectral information divergence\n"
            "[Match] Ranks library reference spectra by similarity against the pixel spectrum collected on the current canvas.\n"
            "[Export / Save] Saves the current pixel spectrum to the spectral library with a custom name and class." } },
        { QStringLiteral( "task_center" ),
          { "Task Center: asynchronous task progress monitoring and result management",
            "[Task list] Shows the state of running and queued algorithm tasks with live progress bars and elapsed time.\n"
            "[Actions] Right-click to pause, resume, cancel or retry a task; view the live log output.\n"
            "[Auto-load] When ticked, artifacts are loaded into map layers automatically after the task succeeds." } },
        { QStringLiteral( "layout" ),
          { "Print Layout / Map Output",
            "Design map frames, legends and scale bars, and export map products." } },
    };
    return k;
}

QString wrapBody( const QString &title, const QString &summary, const QString &body )
{
    QString bodyHtml = body.toHtmlEscaped();
    bodyHtml.replace( QLatin1String( "\n" ), QLatin1String( "<br/>" ) );
    // Light markup: 【section】 as bold line starts
    bodyHtml.replace( QStringLiteral( "【" ), QStringLiteral( "<br/><b>【" ) );
    bodyHtml.replace( QStringLiteral( "】" ), QStringLiteral( "】</b> " ) );
    return QString( "<h3>%1</h3>"
             "<p><b>%2</b></p>"
             "<p>%3</p>"
             "<hr/>"
             "<p>Tip: hover over any widget to see its explanation;"
             "Menu Help → What's This? (Shift+F1), then click a widget;"
             "The GDAL / OTB algorithms in the toolbox have their own help and command previews.</p>" )
      .arg( title.toHtmlEscaped(), summary.toHtmlEscaped(), bodyHtml );
}

} // namespace

void SicnuDialogHelp::tip( QWidget *w, const QString &text )
{
    if ( !w || text.isEmpty() )
        return;
    w->setToolTip( text );
    w->setStatusTip( text );
    w->setWhatsThis( text );
}

void SicnuDialogHelp::tip( QAction *a, const QString &text )
{
    if ( !a || text.isEmpty() )
        return;
    a->setToolTip( text );
    a->setStatusTip( text );
    a->setWhatsThis( text );
}

QString SicnuDialogHelp::shortForTool( const QString &toolId, const QString &titleFallback )
{
    const auto it = catalog().constFind( toolId );
    if ( it != catalog().constEnd() )
        return  it->summary ;
    if ( !titleFallback.isEmpty() )
        return titleFallback;
    return toolId;
}

QString SicnuDialogHelp::htmlForTool( const QString &toolId, const QString &titleFallback )
{
    const auto it = catalog().constFind( toolId );
    const QString title = titleFallback.isEmpty() ? toolId : titleFallback;
    if ( it != catalog().constEnd() )
    {
        return wrapBody( title,  it->summary ,  it->body  );
    }
    return wrapBody(
      title,
      "Function Description" ,
      "Fill in inputs, parameters and output paths on the dialog's tabs, then run."
                   "Hover widgets for more hints; press 'Help' for the full explanation (if available)."  );
}

void SicnuDialogHelp::applyDialogChrome( QDialog *dlg, const QString &toolId )
{
    if ( !dlg )
        return;
    const QString title = dlg->windowTitle().isEmpty() ? toolId : dlg->windowTitle();
    const QString summary = shortForTool( toolId, title );
    dlg->setToolTip( summary );
    dlg->setWhatsThis( htmlForTool( toolId, title ) );
    dlg->setStatusTip( summary );
}

void SicnuDialogHelp::showHelpBox( QWidget *parent, const QString &title, const QString &html )
{
    QMessageBox box( parent );
    box.setWindowTitle( title.isEmpty() ? "Help"  : title );
    box.setTextFormat( Qt::RichText );
    box.setIcon( QMessageBox::Information );
    box.setText( html );
    box.setStandardButtons( QMessageBox::Ok );
    box.exec();
}

void SicnuDialogHelp::showToolHelp( QWidget *parent, const QString &toolId, const QString &title )
{
    showHelpBox( parent, title.isEmpty() ? "Help"  : title,
                 htmlForTool( toolId, title ) );
}
