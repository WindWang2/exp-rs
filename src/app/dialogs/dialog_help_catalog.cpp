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
          { tr("Spectral indices: NDVI / EVI / SAVI / NDWI / NDBI / MNDWI"),
            tr("[Index]\n")
            tr("• NDVI = (NIR−Red)/(NIR+Red): vegetation vigour, −1 to 1\n")
            tr("• EVI: enhanced vegetation index; needs NIR/Red/Blue and suppresses atmosphere and soil\n")
            tr("• SAVI: soil-adjusted vegetation index; better for sparse vegetation\n")
            tr("• NDWI = (Green−NIR)/(Green+NIR): water / moisture\n")
            tr("• NDBI = (SWIR−NIR)/(SWIR+NIR): built-up areas\n")
            tr("• MNDWI = (Green−SWIR)/(Green+SWIR): modified water index\n")
            tr("[Bands] Map NIR / Red / Green / Blue / SWIR per sensor (band numbers start at 1).")
            tr("Pre-filled in the common Landsat / Sentinel order; verify against your actual data.\n")
            tr("[Output] Single-band float GeoTIFF. The path is required before running.") } },
        { QStringLiteral( "terrain" ),
          { tr("DEM terrain analysis: slope / aspect / hillshade, etc."),
            tr("[DEM layer] Elevation raster; units should match the CRS (a metric projection is more reliable).\n")
            tr("[Analysis type]\n")
            tr("• Slope: slope (degrees)\n")
            tr("• Aspect: aspect (degrees; north = 0, clockwise)\n")
            tr("• Hillshade: hillshade (needs solar azimuth / elevation)\n")
            tr("• Roughness / TRI / TPI: roughness and topographic position indices\n")
            tr("[Cell Size] Ground resolution (map units); usually estimated automatically once a layer is chosen.\n")
            tr("[Solar azimuth / elevation] Hillshade only: azimuth 0–360° (north = 0), elevation 0–90°.\n")
            tr("[Output] Single-band result GeoTIFF.") } },
        { QStringLiteral( "extract_band" ),
          { tr("Extract a Single Band from a Multiband Raster"),
            tr("[Raster layer] A project raster with more than 1 band.\n")
            tr("[Band] Band number / name to save separately.\n")
            tr("[Output] A single-band GeoTIFF for single-band analysis or combination with other data.") } },
        { QStringLiteral( "temporal_analysis" ),
          { tr("Time series analysis: multitemporal statistics / compositing / index time series / trends / anomalies / series extraction"),
            tr("[Epoch scenes] Add multiple epoch rasters; acquisition times are parsed from product metadata or file names and can be edited.\n")
            tr("[Precheck] Checks before running: time completeness, duplicate epochs, grid consistency (CRS / resolution / origin, no implicit resampling),")
            tr("Band-role resolvability, radiometric state and scale/offset consistency, QA band availability.\n")
            tr("[Analysis] Time series statistics (Welford mean/variance), best-pixel compositing (quality score + observation count), index time series (same kernel as single scenes),")
            tr("Linear trends (real time intervals), anomalies (z-score / difference), point and ROI series (CSV).\n")
            tr("[Memory] Processes in streaming tiles; working memory is independent of the date count, so tens to hundreds of epochs are safe.") } },
        { QStringLiteral( "mosaic" ),
          { tr("Mosaic multiple rasters into a continuous image"),
            tr("[Input list] At least 2 raster files; projections should match, and the engine merges overlaps with its default strategy.\n")
            tr("[Add / Remove] Manage the files taking part in the mosaic.\n")
            tr("[Output] The mosaicked GeoTIFF. Watch disk and memory for large images.") } },
        { QStringLiteral( "atmospheric_correction" ),
          { tr("Atmospheric Correction / DN to Radiance"),
            tr("[Method]\n")
            tr("• DN to Radiance: L = gain×DN + bias, requires sensor gain/offset\n")
            tr("• DOS1: dark object subtraction estimating path radiance\n")
            tr("• DOS2: DOS1 plus transmittance; needs the airmass\n")
            tr("• QUAC: fast all-band atmospheric correction from image statistics; outputs approximate surface reflectance [0,1] with no external parameters\n")
            tr("[Band] Band number to process (QUAC processes all bands and ignores this).\n")
            tr("[Gain / Bias] Radiometric calibration coefficients (from metadata or the product handbook; ignored by QUAC).\n")
            tr("[Airmass] DOS2 only: the airmass, usually ≥ 1.\n")
            tr("[Output] The corrected raster.") } },
        { QStringLiteral( "contrast_stretch" ),
          { tr("Contrast stretching to improve display and downstream analysis"),
            tr("[Method]\n")
            tr("• Linear: min–max linear stretch to the output range\n")
            tr("• Percentage Clip: clips Clip% at both tails before stretching, suppressing extremes\n")
            tr("• Std Dev: stretch to mean±K×std dev\n")
            tr("• Histogram Equalization: enhances global contrast\n")
            tr("[Clip %] Percent clip only; 1–2% is typical.\n")
            tr("[Std Dev K] Std-dev method only; 2 is typical.\n")
            tr("[Output] Stretched multiband GeoTIFF (band by band over the input).") } },
        { QStringLiteral( "fusion" ),
          { tr("Pansharpening / Image Fusion"),
            tr("[Panchromatic] High spatial resolution single band.\n")
            tr("[Multispectral] Lower resolution multiband. Both must cover roughly the same extent and be registered.\n")
            tr("[Method]\n")
            tr("• Linear Weighted: weighted fusion with adjustable Pan Weight and per-band weights\n")
            tr("• Brovey: ratio-based fusion, fast\n")
            tr("• IHS: requires RGB bands\n")
            tr("• PCA: principal component substitution\n")
            tr("• OTB BundleToPerfectSensor / GDAL pansharpen: external toolchains\n")
            tr("[Pan Weight] Panchromatic share in the linear method, 0–1.\n")
            tr("[RGB Bands] IHS only: red / green / blue band numbers within the multispectral image.\n")
            tr("[Output] The sharpened multispectral GeoTIFF.") } },
        { QStringLiteral( "change_detection" ),
          { tr("Two-Date Change Detection"),
            tr("[Earlier / later images] Must be geometrically aligned (same projection, ideally same resolution); registration can be done first.\n")
            tr("[Band] The band used in the computation for each epoch (usually the same-named band or the same index).\n")
            tr("[Method]\n")
            tr("• Difference: later − earlier\n")
            tr("• Normalized Difference: (later − earlier)/(later + earlier)\n")
            tr("• Change Mask: binary change mask where the difference exceeds the threshold\n")
            tr("[Threshold] Change Mask only: the change threshold (same scale as the DN).\n")
            tr("[Output] Difference or mask GeoTIFF.") } },
        { QStringLiteral( "speckle_filter" ),
          { tr("SAR Speckle Filtering"),
            tr("[Filter] Lee / Frost / Kuan / Gamma-MAP; all suppress speckle with different edge preservation.\n")
            tr("[Window] 3×3 / 5×5 / 7×7; larger values smooth more and keep less detail.\n")
            tr("[Noise Variance] Noise variance estimate for Lee / Kuan / Gamma-MAP; adjust per sensor.\n")
            tr("[Damping] Frost only: the damping factor; larger values smooth more.\n")
            tr("[Output] The filtered raster (band count preserved).") } },
        { QStringLiteral( "band_ratio" ),
          { tr("Band ratio or IHS transform"),
            tr("[Mode]\n")
            tr("• Band Ratio: numerator / denominator, highlighting spectral differences of specific features\n")
            tr("• IHS Transform: converts RGB into intensity-hue-saturation space\n")
            tr("[Numerator / Denominator] Numerator and denominator bands of the ratio.\n")
            tr("[R/G/B] The three input bands of the IHS transform.\n")
            tr("[Output] Single-band ratio or multiband IHS result.") } },
        { QStringLiteral( "pca" ),
          { tr("Principal Component Analysis (PCA)"),
            tr("[Components] Number of output components, ≤ the input band count.\n")
            tr("The first components usually contain most of the variance; used for decorrelation, dimensionality reduction and visual enhancement.\n")
            tr("[Output] Multiband PCA GeoTIFF (bands = PC1, PC2, ...).") } },
        { QStringLiteral( "spatial_filter" ),
          { tr("Spatial Convolution Filtering"),
            tr("[Filter]\n")
            tr("• Mean / Gaussian / Median: smooth noise (median preserves edges better)\n")
            tr("• Sobel / Laplacian: edge enhancement\n")
            tr("[Kernel Size] Convolution kernel 3×3 or 5×5.\n")
            tr("[Output] The filtered raster.") } },
        { QStringLiteral( "image_enhancement" ),
          { tr("Combined Image Enhancement Panel"),
            tr("[Method] Switches within the same dialog: contrast stretch / spatial filtering / band ratio · IHS / SAR speckle filtering.\n")
            tr("Each sub-page's parameters match the corresponding standalone menu tool; see the hover descriptions.\n")
            tr("[Output] Enhanced result GeoTIFF.") } },
        { QStringLiteral( "band_math" ),
          { tr("Band Math Expression"),
            tr("[Expression] Arithmetic expression; bands are b1, b2, ... (starting at 1).\n")
            tr("Examples: (b1-b2)/(b1+b2) is an NDVI-style operation; b1*0.0001 rescales.\n")
            tr("Supports + − * / and parentheses.\n")
            tr("[Output] Single-band computation result.") } },
        { QStringLiteral( "apply_mask" ),
          { tr("Apply Mask: set obscured pixels to NoData with a binary / QA mask"),
            tr("[Input Layer] The multiband product raster to process.\n")
            tr("[Mask Layer] A binary or quality mask raster (1 / non-zero = obscured or invalid, 0 = clear and valid).\n")
            tr("[Auto-align grid] If the mask and input raster differ in resolution or extent, nearest-neighbour resampling aligns them automatically.\n")
            tr("[Output NoData] Replacement value for obscured pixels (metadata NoData by default, or custom e.g. -9999).\n")
            tr("[Output] The masked multiband GeoTIFF.") } },
        { QStringLiteral( "radiometric_calibration" ),
          { tr("Radiometric Calibration: DN to radiance / TOA reflectance / brightness temperature"),
            tr("[Physical quantity]\n")
            tr("• Radiance: W/(m²·sr·µm), computed from gain and offset\n")
            tr("• TOA Reflectance: dimensionless reflectance [0, 1], corrected with the solar elevation and sun–earth distance\n")
            tr("• Brightness Temperature: Kelvin temperature (K) of thermal infrared bands\n")
            tr("[Band] Tick 'process all bands' or choose a specific band.\n")
            tr("[Metadata file] Auto-detect or manually choose a Landsat MTL text or Sentinel-2 MTD XML to extract calibration gain/offset and the sun elevation.\n")
            tr("[Output] Calibrated float GeoTIFF.") } },
        { QStringLiteral( "qa_mask" ),
          { tr("Generate QA Mask: extract cloud, cloud shadow, snow or water"),
            tr("[Quality source]\n")
            tr("• Auto: identify the QA band automatically from sensor metadata\n")
            tr("• Landsat QA_PIXEL: parses the Landsat 8/9 quality assessment bitmask\n")
            tr("• Sentinel-2 SCL: parses the Scene Classification Layer\n")
            tr("• Generic Bitmask: bitwise-AND extraction with a generic integer mask\n")
            tr("[Mask category] Cloud and cloud shadow, cloud only, shadow only, snow/ice, water, or all invalid pixels.\n")
            tr("[Bit mask value] Generic bitmask mode only: the integer value used in the test.\n")
            tr("[Output] Single-band binary mask GeoTIFF (1 = obscured / invalid, 0 = clear and valid).") } },
        { QStringLiteral( "post_classification_change" ),
          { tr("Post-classification change detection: two-date classification comparison and transition matrix"),
            tr("[Earlier / later layers] Single-band classification rasters of the same area at two epochs (pixel values are integer class ids).\n")
            tr("[Num Classes] Number of classes used to build the transition matrix (0 = auto-detect the maximum class id).\n")
            tr("[Transition Matrix] Rows are earlier classes, columns later classes; summarises class flows and area transitions.\n")
            tr("[Output] A change-type map GeoTIFF (pixel value encoded as earlier-id × base + later-id) plus a detailed statistics report.") } },
        { QStringLiteral( "orthorectification" ),
          { tr("Orthorectification: geometric orthorectification based on RPC / GCPs and a DEM"),
            tr("[Geometry model] Detects the rational polynomial coefficients (RPC) or ground control points (GCPs) carried by the input raster automatically.\n")
            tr("[Target CRS] The projected CRS of the orthorectification output (a metric projection such as UTM is recommended).\n")
            tr("[DEM terrain correction] Specify an elevation raster to remove terrain-induced geometric distortion; a reference elevation can be given when none is provided.\n")
            tr("[Resampling] Bilinear (smooth, continuous) / Nearest (preserves pixel values) / Cubic / Lanczos.\n")
            tr("[Cell Size] Target resolution (map units); 0 infers it automatically from the sensor resolution.\n")
            tr("[Output] The orthorectified GeoTIFF.") } },

        // ========== Standalone dialogs ==========
        { QStringLiteral( "batch_processing" ),
          { tr("Batch processing: one algorithm over many files"),
            tr("[Algorithm] Choose an algorithm from the processing registry (GDAL / OTB / built-in, etc.).\n")
            tr("[Input Files] Add / Remove manage the list of files to process.\n")
            tr("[Output Directory] All results are written here (file names derived from the inputs).\n")
            tr("[Run Batch] Executes in order; a progress bar and status line provide feedback.\n")
            tr("Suits repeatable pipelines; validate complex parameters on a single file in the toolbox first.") } },
        { QStringLiteral( "preferences" ),
          { tr("Preferences: theme, CRS, logging and external tool paths"),
            tr("[Theme] Light / dark interface theme.\n")
            tr("[Default CRS] Default CRS for new projects.\n")
            tr("[Log to file / Log File] Whether to write a log file, and its path.\n")
            tr("[GDAL Path / OTB Path] External executable directories used by the CLI wrapper algorithms.\n")
            tr("Some options take full effect only after a restart.") } },
        { QStringLiteral( "stac_browser" ),
          { tr("STAC Catalog Search and Asset Loading"),
            tr("[Endpoint] STAC API root URL, e.g. Element84 Earth Search.\n")
            tr("[Collection] Dataset ID, e.g. sentinel-2-l2a.\n")
            tr("[Datetime] Time filter (ISO interval or instant, as supported by the catalog).\n")
            "【BBox】min_lon,min_lat,max_lon,max_lat。\n"
            tr("[Search] Searches items; the table shows ID / collection / time / asset count.\n")
            tr("[Load Selected Asset] Loads the selected assets into the project (network and permissions required).") } },
        { QStringLiteral( "comparison" ),
          { tr("Side-by-side visual layer comparison"),
            tr("[Left / Right Layer] Raster layers from the project.\n")
            tr("[Load] Loads into the comparison view to inspect registration, change or classification differences.\n")
            tr("Complements the main window's swipe: this tool is a side-by-side comparison.") } },
        { QStringLiteral( "crs_preset" ),
          { tr("Common CRS Presets"),
            tr("[Search] Filter by name or EPSG.\n")
            tr("[Tree list] Browse presets in groups.\n")
            tr("[Details] EPSG, name and WKT summary of the selection.\n")
            tr("Double-click or OK applies the CRS to the project / caller.") } },
        { QStringLiteral( "processing_algorithm" ),
          { tr("Processing Algorithm Dialog (Toolbox)"),
            tr("[Parameter table] Hover any parameter label for its description; required fields are validated before running.\n")
            tr("[Advanced] Advanced parameters are collapsed by default.\n")
            tr("[Load result layers] Joins the layer tree automatically when finished.\n")
            tr("[Command] Live preview of the GDAL / OTB / generic CLI call; copy it to a terminal.\n")
            tr("The help page shows the algorithm shortHelp; it complements the RS-specific dialogs behind the menu entries.") } },
        { QStringLiteral( "sift_match" ),
          { tr("SIFT Auto-Matching GCP Generation"),
            tr("[Contrast] Feature contrast threshold; larger values give fewer but steadier points.\n")
            tr("[Max Matches] Upper bound on the number of matched pairs kept.\n")
            tr("[Min Inlier] Minimum RANSAC inlier ratio.\n")
            tr("[RANSAC Threshold] Pixel tolerance.\n")
            tr("[Max Image Side] Maximum edge length to scale to before matching (speed-up).\n")
            tr("Even after importing results into the GCP table, inspect outliers visually.") } },
        { QStringLiteral( "map_coords" ),
          { tr("GCP Target Coordinate Input"),
            tr("Enter or pick control point target coordinates from the map, pairing them with source pixel positions.")
            tr("In I2I two-canvas mode, points are usually picked on both sides directly; this form is rarely used.") } },

        // ========== Georeferencer ==========
        { QStringLiteral( "georef_i2i" ),
          { tr("Image to Image Registration"),
            tr("[Canvases] left = source (Warp), right = reference (Base); Add / Move / Delete GCP need both sides open.\n")
            tr("[Open] A file or a main project layer.\n")
            tr("[Sync zoom] Off by default; do not force sync with different CRSs or when checking row/column on the same scene.\n")
            tr("[SIFT] Auto-matches GCPs (OpenCV).\n")
            tr("[Correction parameters] The panel on the right: transform / resampling / RMS / CRS / output (see 'Parameter Description').\n")
            tr("[GCP table] Row/column and residuals; right-click to locate / enable-disable / delete.\n")
            tr("[Task] Tracks warp progress after running and loads the result.") } },
        { QStringLiteral( "georef_i2m" ),
          { tr("Image to Map Registration"),
            tr("[Source] A file or project layer; [Base] a visible main project map layer.\n")
            tr("[Transform] Includes RPC Physical (needs RPC metadata, optional DEM).\n")
            tr("The rest is similar to I2I: the GCP table, correction parameters and task list.") } },
        { QStringLiteral( "georef_params" ),
          { tr("Geometric correction parameters (panel on the right)"),
            tr("[Transform] Linear / Helmert ≥ 2 points; polynomial 1/2/3 about 3/6/10 points; TPS / projective / RPC.\n")
            tr("Minimum / actual points / DOF: DOF = actual − minimum; residual assessment needs DOF > 0; at DOF = 0 residuals carry no statistical meaning.\n")
            tr("[Resampling] Nearest preserves classes; Bilinear / Cubic for continuous imagery; cell size auto; the background value fills gaps.\n")
            tr("[RMS] In source pixel units; scatter plus X / Y / total / maximum residuals.\n")
            tr("[CRS] The target CRS determines the output and the fit; in I2I it usually follows the reference.\n")
            tr("[Output] A path is required before running. [DEM] RPC only.") } },
        { QStringLiteral( "georef_gcp_table" ),
          { tr("GCP Table"),
            tr("Columns: map coordinates, pixel column/row on both sides, residuals ΔX/ΔY/RMS, enabled state.\n")
            tr("Right-click: locate, enable / disable, edit, delete. Delete removes the selection.\n")
            tr("For same-scene registration, 'col src / row src' should be close to 'col ref / row ref'.") } },
        { QStringLiteral( "georef_tasks" ),
          { tr("Correction Task List"),
            tr("Shows progress after running; can be cancelled; results load into the project when finished.") } },

        // ========== Classification / OBIA ==========
        { QStringLiteral( "classification" ),
          { tr("Pixel-Level Supervised Classification"),
            tr("[Workflow] Load image → define classes → collect ROIs → set algorithm / bands / ignored values → train and classify → accuracy assessment.\n")
            tr("[ROI Tools] Collect training samples with point / rectangle / polygon / freehand / magic wand.\n")
            tr("[Setup bar] Algorithm, bands, training ratio, NoData / ignored values, output path; preview / cross-validation / apply.\n")
            tr("[Accuracy] OA, Kappa, confusion matrix, producer's / user's accuracy.") } },
        { QStringLiteral( "classify_setup" ),
          { tr("Classifier Setup Bar Parameters"),
            tr("[Algorithm] Normal Bayes / SVM / K-means (RF / Mahalanobis / UNet placeholders).\n")
            tr("[Bands] Comma-separated, e.g. 1,2,3.\n")
            tr("[Training ratio] Stratified training share; the rest is a holdout for accuracy.\n")
            tr("[Output] Classification result GeoTIFF.\n")
            tr("[Use source NoData] Metadata NoData values are ignored.\n")
            tr("[Ignored values] Extra DN list (e.g. 0-fill edges).\n")
            tr("[Matching] Ignore the whole pixel if any band is ignored (default), or only when all bands are ignored.\n")
            tr("[Preview] Current viewport only. [Cross-Validation] K-fold evaluation. [Apply] Full-image classification.") } },
        { QStringLiteral( "obia" ),
          { tr("Object-Based Classification (OBIA)"),
            tr("[Load Raster] Loads the image to segment.\n")
            tr("[Segments Kernel] Smoothing kernel size 3–21; larger values give coarser objects.\n")
            tr("[Bins] Quantization levels (built-in segmentation fallback), 2–128.\n")
            tr("[Min region] Minimum object pixel count; suppresses small patches.\n")
            tr("[Segment] Runs segmentation. [Classifier] Normal Bayes / SVM / K-means.\n")
            tr("[Classify] Object-level classification. [Export] Export results.\n")
            tr("[Classes table] Class ID / name / color. Suits high-resolution imagery.") } },
        { QStringLiteral( "obia_class_table" ),
          { tr("OBIA Class Table"),
            tr("[Columns] ID / name / color.\n")
            tr("[ID] Corresponds to the classification raster pixel value, starting at 1; not directly editable.\n")
            tr("[Right-click] Edit name, change color, insert / delete classes.\n")
            tr("[Assign] Assigns the current class to the objects selected on the canvas.") } },
        { QStringLiteral( "obia_segment_table" ),
          { tr("OBIA Object List"),
            tr("[Columns] ID / pixel count / class.\n")
            tr("[Right-click] Locate the object on the canvas, assign the current class, copy the ID.\n")
            tr("Refills from the current level when switching levels; segmentation must be done first.") } },
        { QStringLiteral( "obia_segment_info" ),
          { tr("OBIA Object Info"),
            tr("Shows shape, spectral and hierarchy statistics of the object selected on the canvas (read-only HTML).\n")
            tr("Use the 'Select Objects' map tool to click objects on the canvas and refresh.") } },
        { QStringLiteral( "obia_task_list" ),
          { tr("Task Center (OBIA task list)"),
            tr("[Columns] Title / status / progress / load checkbox.\n")
            tr("[Right-click] View details and log, stop, pause/resume, retry, load outputs into the main view, copy info.\n")
            tr("[Load checkbox] Loads outputs into the main program automatically after the task succeeds.\n")
            tr("[Status colors] blue = running, green = finished, red = failed, grey = queued / cancelled.") } },
        { QStringLiteral( "obia_data_manager" ),
          { tr("Data Management"),
            tr("[Tree] Project data assets and collections; the color bar on the left shows status (green = available, red = unavailable).\n")
            tr("[Right-click] Add to display, promote to project persistent, unload, view properties, copy source path.\n")
            tr("[Double-click] Same as 'Add to Display'. The meta information inspector is below.") } },
        { QStringLiteral( "accuracy" ),
          { tr("Classification Accuracy Assessment"),
            tr("[OA] Overall accuracy. [Kappa] Agreement coefficient.\n")
            tr("[Confusion matrix] Rows = truth, columns = prediction.\n")
            tr("[Producer's accuracy] Share of a class's true samples classified correctly (recall).\n")
            tr("[User's accuracy] Share of a class's predictions that are correct (precision).\n")
            tr("[F1] Harmonic mean of producer's and user's accuracy. [Export CSV] Saves the report.") } },
        { QStringLiteral( "post_process" ),
          { tr("Post-Classification"),
            tr("[Vectorize] Converts the classification raster into vector polygons.\n")
            tr("[Filter] Filters small patches by minimum area.\n")
            tr("[Merge] Merges small patches into adjacent major classes.\n")
            tr("[Output] Exports the processed raster / vector.") } },
        { QStringLiteral( "classifier_load" ),
          { tr("Load Trained Classifier"),
            tr("Loads a previously saved classifier model file from disk.\n")
            tr("Once loaded, the classification can be applied to new images directly, without retraining.") } },
        { QStringLiteral( "merge_classes" ),
          { tr("Class Merging"),
            tr("Merges several fine classes into one coarse class, updating the class table and classified results.\n")
            tr("Executes the merge after source and target classes are chosen.") } },
        { QStringLiteral( "template_match" ),
          { tr("Geometric Correction - Template Matching"),
            tr("[Template] Drag a rectangle around the reference template area on the source image.\n")
            tr("[Search] Auto-matches conjugate points on the image to correct, generating GCPs.\n")
            tr("[Correlation threshold] Controls match confidence; too low invites mismatches, too high misses points.") } },
        { QStringLiteral( "landsat_import" ),
          { tr("Import Landsat Product"),
            tr("[Scene directory] An extracted directory containing *_MTL.txt.\n")
            tr("[Probe] Parses the MTL and lists sub-items (grid groups) and bands.\n")
            tr("[Preview tree] Tick the bands to import; multispectral bands by default.\n")
            tr("[Import] Composites as selected and loads into the project.") } },
        { QStringLiteral( "digitize_tools" ),
          { tr("Vector Digitizing Edit Tools"),
            tr("[Select] Select features with a rectangle. [Add Feature] Draw a new feature.\n")
            tr("[Node Tool] Edit vertices. [Move / Rotate] Transform the whole feature.\n")
            tr("[Reshape] Modify boundaries. [Split] Split features. [Offset] Offset lines.\n")
            tr("[Simplify] Thins vertices. [Reverse] Reverses line direction.\n")
            tr("[Add Ring / Fill Ring] Handles holes inside polygons. [Delete Part] Removes one part of a multipart feature.") } },

        // ========== Main / misc ==========
        { QStringLiteral( "main_window" ),
          { tr("Main Window — SICNU GEO RS"),
            tr("[Menu layout]\n")
            tr("• Project: open/save, import, STAC, layouts and reports\n")
            tr("• Edit: feature editing; digitizing tools live in the 'Edit → Digitizing' submenu\n")
            tr("• View: zoom, pan, identify, measure, compare / swipe\n")
            tr("• Layer: add/remove rasters and vectors, project CRS\n")
            tr("• Raster: preprocessing (incl. image registration), image enhancement, bands and transforms\n")
            tr("• Analysis: spectral indices, change detection, fusion, terrain, classification (thematic)\n")
            tr("• Vector: geometry processing, overlay, spatial selection, attributes and projection\n")
            tr("• Processing: toolbox / history / batch\n")
            tr("• Settings / Window / Help\n")
            tr("Hover menu items and dialog widgets for parameter explanations; Shift+F1 is 'What's This?'.") } },
        { QStringLiteral( "swipe" ),
          { tr("Swipe Comparison Tool"),
            tr("Drag the divider on the map to compare the layers above and below, checking registration or change.") } },
        { QStringLiteral( "sentinel2_import" ),
          { tr("Import Sentinel-2 Product: detect and import L1C / L2A SAFE directories"),
            tr("[Product directory] An extracted Sentinel-2 .SAFE folder or a directory containing MTD_MSIL*.xml.\n")
            tr("[Probe] Parses the metadata XML, identifying the 10 m/20 m/60 m resolution grid groups and the SCL quality band.\n")
            tr("[Band selection] Tick the multispectral bands or derived indices to import in the candidate tree.\n")
            tr("[Import] Loads the selected bands into the data manager automatically and registers them as a dataset collection.") } },
        { QStringLiteral( "modis_import" ),
          { tr("Import MODIS Product: detect and import HDF / GeoTIFF tiles"),
            tr("[Product path] A MODIS HDF4 scientific dataset file (e.g. MOD09GA, MOD13Q1) or an extracted tile directory.\n")
            tr("[Tile] Auto-identify or specify the hXXvYY sinusoidal grid number.\n")
            tr("[Probe] Parses the internal subdataset band list and scale factors.\n")
            tr("[Import] Imports the selected surface reflectance / vegetation index bands into the project.") } },
        { QStringLiteral( "product_import" ),
          { tr("Satellite product import: detect Landsat / Sentinel-2 / MODIS packages"),
            tr("[Product directory] Choose a product folder containing standard sensor metadata.\n")
            tr("[Probe] Reads the metadata and analyses grid resolution, band names and wavelength ranges.\n")
            tr("[Preview tree] Shows bands grouped by resolution and purpose; tick to import into the project.") } },
        { QStringLiteral( "spectral_library" ),
          { tr("Spectral library matching and management: SAM / SID spectral angle matching"),
            tr("[Spectral library file] Choose a USGS / ASTER format library or a JSON spectral library exported by this project.\n")
            tr("[Matching algorithm]\n")
            tr("• SAM (Spectral Angle Mapper): spectral angle in high-dimensional space; smaller is more similar\n")
            tr("• SID (Spectral Information Divergence): spectral information divergence\n")
            tr("[Match] Ranks library reference spectra by similarity against the pixel spectrum collected on the current canvas.\n")
            tr("[Export / Save] Saves the current pixel spectrum to the spectral library with a custom name and class.") } },
        { QStringLiteral( "task_center" ),
          { tr("Task Center: asynchronous task progress monitoring and result management"),
            tr("[Task list] Shows the state of running and queued algorithm tasks with live progress bars and elapsed time.\n")
            tr("[Actions] Right-click to pause, resume, cancel or retry a task; view the live log output.\n")
            tr("[Auto-load] When ticked, artifacts are loaded into map layers automatically after the task succeeds.") } },
        { QStringLiteral( "layout" ),
          { tr("Print Layout / Map Output"),
            tr("Design map frames, legends and scale bars, and export map products.") } },
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
    return QObject::tr(
             "<h3>%1</h3>"
             "<p><b>%2</b></p>"
             "<p>%3</p>"
             "<hr/>"
             tr("<p>Tip: hover over any widget to see its explanation;")
             tr("Menu Help → What's This? (Shift+F1), then click a widget;")
             tr("The GDAL / OTB algorithms in the toolbox have their own help and command previews.</p>") )
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
        return QObject::tr( it->summary );
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
        return wrapBody( title, QObject::tr( it->summary ), QObject::tr( it->body ) );
    }
    return wrapBody(
      title,
      QObject::tr( "Function Description" ),
      QObject::tr( "Fill in inputs, parameters and output paths on the dialog's tabs, then run."
                   tr("Hover widgets for more hints; press 'Help' for the full explanation (if available).") ) );
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
    box.setWindowTitle( title.isEmpty() ? QObject::tr( "Help" ) : title );
    box.setTextFormat( Qt::RichText );
    box.setIcon( QMessageBox::Information );
    box.setText( html );
    box.setStandardButtons( QMessageBox::Ok );
    box.exec();
}

void SicnuDialogHelp::showToolHelp( QWidget *parent, const QString &toolId, const QString &title )
{
    showHelpBox( parent, title.isEmpty() ? QObject::tr( "Help" ) : title,
                 htmlForTool( toolId, title ) );
}
