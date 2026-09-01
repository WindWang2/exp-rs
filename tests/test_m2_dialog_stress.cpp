// test_m2_dialog_stress.cpp — Comprehensive Adversarial Empirical Stress Testing for M2 Dialogs
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "app/dialogs/raster_processing_dialog_base.h"
#include "app/dialogs/dialog_utils.h"

// Batch A Dialogs
#include "app/dialogs/atmospheric_dialog.h"
#include "app/dialogs/radiometric_calibration_dialog.h"
#include "app/dialogs/contrast_stretch_dialog.h"
#include "app/dialogs/spatial_filter_dialog.h"
#include "app/dialogs/speckle_filter_dialog.h"
#include "app/dialogs/spectral_index_dialog.h"
#include "app/dialogs/spectral_library_dialog.h"
#include "app/dialogs/band_ratio_dialog.h"
#include "app/dialogs/band_math_dialog.h"
#include "app/dialogs/extract_band_dialog.h"
#include "app/dialogs/qa_mask_dialog.h"

// Batch B Dialogs
#include "app/dialogs/orthorectification_dialog.h"
#include "app/dialogs/mosaic_dialog.h"
#include "app/dialogs/fusion_dialog.h"
#include "app/dialogs/change_detection_dialog.h"
#include "app/dialogs/pca_dialog.h"
#include "app/dialogs/post_classification_dialog.h"
#include "app/dialogs/terrain_dialog.h"
#include "app/dialogs/apply_mask_dialog.h"
#include "app/dialogs/batch_processing_dialog.h"
#include "app/dialogs/product_import_dialog.h"
#include "app/dialogs/crs_preset_dialog.h"
#include "app/dialogs/comparison_dialog.h"
#include "app/dialogs/help_viewer_dialog.h"
#include "app/dialogs/preferences_dialog.h"
#include "app/classification/rs_merge_classes_dialog.h"
#include "app/classification/rs_post_process_dialog.h"
#include "app/georeferencer/rs_sift_dialog.h"
#include "app/georeferencer/rs_template_match_dialog.h"

namespace {

void verifyProcessingDialogStructure(RasterProcessingDialogBase *dlg, const QString &expectedTitle)
{
    REQUIRE(dlg != nullptr);
    INFO("dlg->windowTitle(): " << dlg->windowTitle().toStdString() << ", expectedTitle: " << expectedTitle.toStdString());
    REQUIRE(dlg->windowTitle().contains(expectedTitle));
    REQUIRE(dlg->minimumSizeHint().width() >= 400);
    REQUIRE(dlg->minimumSizeHint().height() >= 200);

    // Verify standardized QDialogButtonBox exists
    auto *bbox = dlg->buttonBox();
    REQUIRE(bbox != nullptr);
    REQUIRE(bbox->objectName() == QStringLiteral("rsDialogButtonBox"));

    // Verify all 4 canonical buttons
    auto *runBtn = dlg->runButton();
    auto *cancelBtn = dlg->cancelButton();
    auto *resetBtn = dlg->resetButton();
    auto *helpBtn = dlg->helpButton();

    REQUIRE(runBtn != nullptr);
    REQUIRE(cancelBtn != nullptr);
    REQUIRE(resetBtn != nullptr);
    REQUIRE(helpBtn != nullptr);

    // Verify primary / secondary styling properties
    REQUIRE(runBtn->isDefault());
    REQUIRE(runBtn->property("primary").toBool() == true);
    REQUIRE(cancelBtn->property("ghost").toBool() == true);
    REQUIRE(resetBtn->property("ghost").toBool() == true);
    REQUIRE(helpBtn->property("ghost").toBool() == true);

    // Verify presence of QGroupBox containers
    auto groupBoxes = dlg->findChildren<QGroupBox *>();
    REQUIRE_FALSE(groupBoxes.isEmpty());
    for (auto *gb : groupBoxes)
    {
        REQUIRE_FALSE(gb->title().isEmpty());
    }

    // Verify output path edit
    auto *outputEdit = dlg->findChild<QLineEdit *>(QStringLiteral("rsDialogOutputEdit"));
    REQUIRE(outputEdit != nullptr);
    REQUIRE_FALSE(outputEdit->placeholderText().isEmpty());
    REQUIRE_FALSE(outputEdit->toolTip().isEmpty());
}

} // namespace

// ============================================================================
// TEST 1: Batch A Standardized Processing Dialogs
// ============================================================================

TEST_CASE("M2 Batch A Dialogs Structure and Reset Verification", "[m2][batch_a][ui]")
{
    SECTION("AtmosphericDialog")
    {
        AtmosphericDialog dlg;
        verifyProcessingDialogStructure(&dlg, QStringLiteral("大气校正"));
        dlg.setRasterLayer(nullptr);

        auto *outputEdit = dlg.findChild<QLineEdit *>(QStringLiteral("rsDialogOutputEdit"));
        outputEdit->setText(QStringLiteral("/tmp/test_atmo_out.tif"));
        REQUIRE(dlg.outputPath() == QStringLiteral("/tmp/test_atmo_out.tif"));

        // Test Reset
        dlg.resetButton()->click();
        REQUIRE(dlg.outputPath().isEmpty());
    }

    SECTION("RadiometricCalibrationDialog")
    {
        RadiometricCalibrationDialog dlg;
        verifyProcessingDialogStructure(&dlg, QStringLiteral("辐射定标"));
        dlg.setRasterLayer(nullptr);

        auto *outputEdit = dlg.findChild<QLineEdit *>(QStringLiteral("rsDialogOutputEdit"));
        outputEdit->setText(QStringLiteral("/tmp/test_rad_out.tif"));
        REQUIRE(dlg.outputPath() == QStringLiteral("/tmp/test_rad_out.tif"));

        // Test Reset
        dlg.resetButton()->click();
        REQUIRE(dlg.outputPath().isEmpty());
    }

    SECTION("ContrastStretchDialog")
    {
        ContrastStretchDialog dlg;
        verifyProcessingDialogStructure(&dlg, QStringLiteral("对比度拉伸"));
        dlg.setRasterLayer(nullptr);

        auto *outputEdit = dlg.findChild<QLineEdit *>(QStringLiteral("rsDialogOutputEdit"));
        outputEdit->setText(QStringLiteral("/tmp/test_stretch_out.tif"));
        REQUIRE(dlg.outputPath() == QStringLiteral("/tmp/test_stretch_out.tif"));

        // Test Reset
        dlg.resetButton()->click();
        REQUIRE(dlg.outputPath().isEmpty());
    }

    SECTION("SpatialFilterDialog")
    {
        SpatialFilterDialog dlg;
        verifyProcessingDialogStructure(&dlg, QStringLiteral("空间滤波"));
        dlg.setRasterLayer(nullptr);

        auto *outputEdit = dlg.findChild<QLineEdit *>(QStringLiteral("rsDialogOutputEdit"));
        outputEdit->setText(QStringLiteral("/tmp/test_spatial_out.tif"));
        REQUIRE(dlg.outputPath() == QStringLiteral("/tmp/test_spatial_out.tif"));

        // Test Reset
        dlg.resetButton()->click();
        REQUIRE(dlg.outputPath().isEmpty());
    }

    SECTION("SpeckleFilterDialog")
    {
        SpeckleFilterDialog dlg;
        verifyProcessingDialogStructure(&dlg, QStringLiteral("斑点滤波"));
        dlg.setRasterLayer(nullptr);

        auto *outputEdit = dlg.findChild<QLineEdit *>(QStringLiteral("rsDialogOutputEdit"));
        outputEdit->setText(QStringLiteral("/tmp/test_speckle_out.tif"));
        REQUIRE(dlg.outputPath() == QStringLiteral("/tmp/test_speckle_out.tif"));

        // Test Reset
        dlg.resetButton()->click();
        REQUIRE(dlg.outputPath().isEmpty());
    }

    SECTION("SpectralIndexDialog")
    {
        SpectralIndexDialog dlg;
        verifyProcessingDialogStructure(&dlg, QStringLiteral("光谱指数"));
        dlg.setRasterLayer(nullptr);

        auto *outputEdit = dlg.findChild<QLineEdit *>(QStringLiteral("rsDialogOutputEdit"));
        outputEdit->setText(QStringLiteral("/tmp/test_index_out.tif"));
        REQUIRE(dlg.outputPath() == QStringLiteral("/tmp/test_index_out.tif"));

        // Test Reset
        dlg.resetButton()->click();
        REQUIRE(dlg.outputPath().isEmpty());
    }

    SECTION("SpectralLibraryDialog")
    {
        SpectralLibraryDialog dlg;
        REQUIRE(dlg.windowTitle().contains(QStringLiteral("光谱库")));
        REQUIRE(dlg.minimumWidth() >= 500);

        auto *pathEdit = dlg.findChild<QLineEdit *>(QStringLiteral("spectralLibPathEdit"));
        REQUIRE(pathEdit != nullptr);
        auto *matchBtn = dlg.findChild<QPushButton *>(QStringLiteral("spectralMatchBtn"));
        REQUIRE(matchBtn != nullptr);
        auto *saveBtn = dlg.findChild<QPushButton *>(QStringLiteral("spectralSaveBtn"));
        REQUIRE(saveBtn != nullptr);
        auto *table = dlg.findChild<QTableWidget *>(QStringLiteral("spectralMatchTable"));
        REQUIRE(table != nullptr);
    }

    SECTION("BandRatioDialog")
    {
        BandRatioDialog dlg;
        verifyProcessingDialogStructure(&dlg, QStringLiteral("波段比值"));
        dlg.setRasterLayer(nullptr);

        auto *outputEdit = dlg.findChild<QLineEdit *>(QStringLiteral("rsDialogOutputEdit"));
        outputEdit->setText(QStringLiteral("/tmp/test_ratio_out.tif"));
        REQUIRE(dlg.outputPath() == QStringLiteral("/tmp/test_ratio_out.tif"));

        // Test Reset
        dlg.resetButton()->click();
        REQUIRE(dlg.outputPath().isEmpty());
    }

    SECTION("BandMathDialog")
    {
        BandMathDialog dlg;
        verifyProcessingDialogStructure(&dlg, QStringLiteral("波段运算"));
        dlg.setRasterLayer(nullptr);

        auto *outputEdit = dlg.findChild<QLineEdit *>(QStringLiteral("rsDialogOutputEdit"));
        outputEdit->setText(QStringLiteral("/tmp/test_math_out.tif"));
        REQUIRE(dlg.outputPath() == QStringLiteral("/tmp/test_math_out.tif"));

        // Test Reset
        dlg.resetButton()->click();
        REQUIRE(dlg.outputPath().isEmpty());
    }

    SECTION("ExtractBandDialog")
    {
        ExtractBandDialog dlg;
        verifyProcessingDialogStructure(&dlg, QStringLiteral("提取波段"));
        dlg.setRasterLayer(nullptr);

        auto *outputEdit = dlg.findChild<QLineEdit *>(QStringLiteral("rsDialogOutputEdit"));
        outputEdit->setText(QStringLiteral("/tmp/test_extract_out.tif"));
        REQUIRE(dlg.outputPath() == QStringLiteral("/tmp/test_extract_out.tif"));

        // Test Reset
        dlg.resetButton()->click();
        REQUIRE(dlg.outputPath().isEmpty());
    }

    SECTION("QaMaskDialog")
    {
        QaMaskDialog dlg;
        verifyProcessingDialogStructure(&dlg, QStringLiteral("QA 掩膜"));
        dlg.setRasterLayer(nullptr);

        // Verification of QA Mask Dialog invariant: should not auto accept so results stay visible
        REQUIRE_FALSE(static_cast<const RasterProcessingDialogBase &>(dlg).shouldAutoAcceptOnSuccess());

        auto *outputEdit = dlg.findChild<QLineEdit *>(QStringLiteral("rsDialogOutputEdit"));
        outputEdit->setText(QStringLiteral("/tmp/test_qa_out.tif"));
        REQUIRE(dlg.outputPath() == QStringLiteral("/tmp/test_qa_out.tif"));

        // Test Reset
        dlg.resetButton()->click();
        REQUIRE(dlg.outputPath().isEmpty());
    }
}

// ============================================================================
// TEST 2: Batch B Standardized Processing Dialogs
// ============================================================================

TEST_CASE("M2 Batch B Dialogs Structure and Reset Verification", "[m2][batch_b][ui]")
{
    SECTION("OrthorectificationDialog")
    {
        OrthorectificationDialog dlg;
        verifyProcessingDialogStructure(&dlg, QStringLiteral("正射纠正"));
        dlg.setRasterLayer(nullptr);

        auto *outputEdit = dlg.findChild<QLineEdit *>(QStringLiteral("rsDialogOutputEdit"));
        outputEdit->setText(QStringLiteral("/tmp/test_ortho_out.tif"));
        REQUIRE(dlg.outputPath() == QStringLiteral("/tmp/test_ortho_out.tif"));

        dlg.resetButton()->click();
        REQUIRE(dlg.outputPath().isEmpty());
    }

    SECTION("MosaicDialog")
    {
        MosaicDialog dlg;
        verifyProcessingDialogStructure(&dlg, QStringLiteral("影像镶嵌"));

        auto *outputEdit = dlg.findChild<QLineEdit *>(QStringLiteral("rsDialogOutputEdit"));
        outputEdit->setText(QStringLiteral("/tmp/test_mosaic_out.tif"));
        REQUIRE(dlg.outputPath() == QStringLiteral("/tmp/test_mosaic_out.tif"));

        dlg.resetButton()->click();
        REQUIRE(dlg.outputPath().isEmpty());
    }

    SECTION("FusionDialog")
    {
        FusionDialog dlg;
        verifyProcessingDialogStructure(&dlg, QStringLiteral("影像融合"));

        auto *outputEdit = dlg.findChild<QLineEdit *>(QStringLiteral("rsDialogOutputEdit"));
        outputEdit->setText(QStringLiteral("/tmp/test_fusion_out.tif"));
        REQUIRE(dlg.outputPath() == QStringLiteral("/tmp/test_fusion_out.tif"));

        dlg.resetButton()->click();
        REQUIRE(dlg.outputPath().isEmpty());
    }

    SECTION("ChangeDetectionDialog")
    {
        ChangeDetectionDialog dlg;
        verifyProcessingDialogStructure(&dlg, QStringLiteral("变化检测"));

        auto *outputEdit = dlg.findChild<QLineEdit *>(QStringLiteral("rsDialogOutputEdit"));
        outputEdit->setText(QStringLiteral("/tmp/test_cd_out.tif"));
        REQUIRE(dlg.outputPath() == QStringLiteral("/tmp/test_cd_out.tif"));

        dlg.resetButton()->click();
        REQUIRE(dlg.outputPath().isEmpty());
    }

    SECTION("PcaDialog")
    {
        PcaDialog dlg;
        verifyProcessingDialogStructure(&dlg, QStringLiteral("主成分分析"));
        dlg.setRasterLayer(nullptr);

        auto *outputEdit = dlg.findChild<QLineEdit *>(QStringLiteral("rsDialogOutputEdit"));
        outputEdit->setText(QStringLiteral("/tmp/test_pca_out.tif"));
        REQUIRE(dlg.outputPath() == QStringLiteral("/tmp/test_pca_out.tif"));

        dlg.resetButton()->click();
        REQUIRE(dlg.outputPath().isEmpty());
    }

    SECTION("PostClassificationDialog")
    {
        PostClassificationDialog dlg;
        verifyProcessingDialogStructure(&dlg, QStringLiteral("后分类比较"));

        auto *outputEdit = dlg.findChild<QLineEdit *>(QStringLiteral("rsDialogOutputEdit"));
        outputEdit->setText(QStringLiteral("/tmp/test_post_out.tif"));
        REQUIRE(dlg.outputPath() == QStringLiteral("/tmp/test_post_out.tif"));

        dlg.resetButton()->click();
        REQUIRE(dlg.outputPath().isEmpty());
    }

    SECTION("TerrainDialog")
    {
        TerrainDialog dlg;
        verifyProcessingDialogStructure(&dlg, QStringLiteral("地形分析"));

        auto *outputEdit = dlg.findChild<QLineEdit *>(QStringLiteral("rsDialogOutputEdit"));
        outputEdit->setText(QStringLiteral("/tmp/test_terrain_out.tif"));
        REQUIRE(dlg.outputPath() == QStringLiteral("/tmp/test_terrain_out.tif"));

        dlg.resetButton()->click();
        REQUIRE(dlg.outputPath().isEmpty());
    }

    SECTION("ApplyMaskDialog")
    {
        ApplyMaskDialog dlg;
        verifyProcessingDialogStructure(&dlg, QStringLiteral("应用掩膜"));

        auto *outputEdit = dlg.findChild<QLineEdit *>(QStringLiteral("rsDialogOutputEdit"));
        outputEdit->setText(QStringLiteral("/tmp/test_mask_out.tif"));
        REQUIRE(dlg.outputPath() == QStringLiteral("/tmp/test_mask_out.tif"));

        dlg.resetButton()->click();
        REQUIRE(dlg.outputPath().isEmpty());
    }

    SECTION("BatchProcessingDialog")
    {
        BatchProcessingDialog dlg;
        REQUIRE(dlg.windowTitle().contains(QStringLiteral("批量处理")));
        REQUIRE(dlg.minimumWidth() >= 500);

        auto *algoCombo = dlg.findChild<QComboBox *>(QStringLiteral("batchAlgorithmCombo"));
        REQUIRE(algoCombo != nullptr);
        auto *fileList = dlg.findChild<QListWidget *>(QStringLiteral("batchFileList"));
        REQUIRE(fileList != nullptr);
        auto *outDirEdit = dlg.findChild<QLineEdit *>(QStringLiteral("batchOutputDirEdit"));
        REQUIRE(outDirEdit != nullptr);
    }

    SECTION("ProductImportDialog")
    {
        ProductImportDialog dlg;
        REQUIRE(dlg.windowTitle().contains(QStringLiteral("导入")));
        REQUIRE(dlg.minimumWidth() >= 500);

        auto *tree = dlg.findChild<QTreeWidget *>(QStringLiteral("productPreviewTree"));
        REQUIRE(tree != nullptr);
    }

    SECTION("CrsPresetDialog")
    {
        CrsPresetDialog dlg;
        REQUIRE(dlg.windowTitle().contains(QStringLiteral("坐标系")));
        REQUIRE(dlg.minimumWidth() >= 500);
        REQUIRE(dlg.selectedEpsg() == -1);
    }

    SECTION("ComparisonDialog")
    {
        ComparisonDialog dlg;
        REQUIRE(dlg.windowTitle().contains(QStringLiteral("对比")));
        REQUIRE(dlg.minimumWidth() >= 600);
    }

    SECTION("HelpViewerDialog")
    {
        HelpViewerDialog dlg;
        REQUIRE(dlg.windowTitle().contains(QStringLiteral("帮助文档")));
        REQUIRE(dlg.minimumWidth() >= 500);
        REQUIRE(dlg.tocTree() != nullptr);
        REQUIRE(dlg.textBrowser() != nullptr);
    }

    SECTION("PreferencesDialog")
    {
        PreferencesDialog dlg;
        REQUIRE(dlg.windowTitle().contains(QStringLiteral("首选项")));
        REQUIRE(dlg.minimumWidth() >= 500);
    }

    SECTION("RsMergeClassesDialog")
    {
        RsMergeClassesDialog dlg;
        REQUIRE(dlg.windowTitle().contains(QStringLiteral("合并分类类别")));
        REQUIRE(dlg.minimumWidth() >= 300);

        // Test pure function buildRecodeMap
        QList<int> sourceIds = { 5, 2, 8, 2 };
        auto recodeMap = buildRecodeMap(sourceIds, 2);
        REQUIRE(recodeMap.size() == 3);
        REQUIRE(recodeMap[5] == 2);
        REQUIRE(recodeMap[2] == 2);
        REQUIRE(recodeMap[8] == 2);
    }

    SECTION("RsPostProcessDialog (Sieve, Majority, Clump, Polygonize)")
    {
        const RsPostProcessDialog::Algorithm algos[] = {
            RsPostProcessDialog::Algorithm::Sieve,
            RsPostProcessDialog::Algorithm::Majority,
            RsPostProcessDialog::Algorithm::Clump,
            RsPostProcessDialog::Algorithm::Polygonize
        };

        const QString testInputPath = QStringLiteral("/home/kevin/projects/rs-studio/main/CMakeLists.txt");

        for (auto algo : algos)
        {
            RsPostProcessDialog dlg(algo);
            REQUIRE_FALSE(dlg.windowTitle().isEmpty());
            REQUIRE(dlg.minimumWidth() >= 400);

            dlg.setDefaultInputPath(testInputPath);
            dlg.setDefaultOutputPath(QStringLiteral("/tmp/output.tif"));

            RsPostProcessConfig cfg;
            QString errMsg;
            bool ok = dlg.buildConfig(cfg, &errMsg);
            INFO("algo: " << static_cast<int>(algo) << ", errMsg: " << errMsg.toStdString());
            REQUIRE(ok);
            REQUIRE(cfg.inputPath == testInputPath);
            if (algo == RsPostProcessDialog::Algorithm::Polygonize)
            {
                REQUIRE(cfg.outputVectorPath == QStringLiteral("/tmp/output.tif"));
            }
            else
            {
                REQUIRE(cfg.outputRasterPath == QStringLiteral("/tmp/output.tif"));
            }
        }
    }

    SECTION("RsPostProcessDialog Recode Validation")
    {
        const QString testInputPath = QStringLiteral("/home/kevin/projects/rs-studio/main/CMakeLists.txt");
        RsPostProcessDialog dlg(RsPostProcessDialog::Algorithm::Recode);
        dlg.setDefaultInputPath(testInputPath);

        // Empty recode map should fail validation
        RsPostProcessConfig cfg;
        QString errMsg;
        bool ok = dlg.buildConfig(cfg, &errMsg);
        REQUIRE_FALSE(ok);
        REQUIRE(errMsg.contains(QStringLiteral("旧类")));

        // Adding row to recode table
        auto *table = dlg.findChild<QTableWidget *>();
        REQUIRE(table != nullptr);
        table->insertRow(0);
        table->setItem(0, 0, new QTableWidgetItem(QStringLiteral("1")));
        table->setItem(0, 1, new QTableWidgetItem(QStringLiteral("2")));

        ok = dlg.buildConfig(cfg, &errMsg);
        REQUIRE(ok);
        REQUIRE(cfg.runRecode);
        REQUIRE(cfg.recodeMap.value(1) == 2);
    }

    SECTION("RsSiftDialog")
    {
        RsSiftDialog dlg;
        REQUIRE(dlg.windowTitle().contains(QStringLiteral("SIFT")));
        REQUIRE(dlg.minimumWidth() >= 400);

        auto p = dlg.params();
        REQUIRE(p.contrastThreshold > 0.0);
        REQUIRE(p.maxMatches > 0);
        REQUIRE(p.minInlierRatio > 0.0);
        REQUIRE(p.ransacThreshold > 0.0);
        REQUIRE(p.maxImageSide > 0);
    }

    SECTION("RsTemplateMatchDialog")
    {
        RsTemplateMatchDialog dlg;
        REQUIRE(dlg.windowTitle().contains(QStringLiteral("模板匹配")));
        REQUIRE(dlg.minimumWidth() >= 400);

        auto p = dlg.params();
        REQUIRE(p.templateSize > 0);
        REQUIRE(p.searchRadiusPx > 0);
        REQUIRE(p.minScore > 0.0);
    }
}

// ============================================================================
// TEST 3: Rapid Instantiation & Destruction Lifecycle Stress
// ============================================================================

TEST_CASE("M2 Dialogs Rapid Lifecycle Instantiation & Destruction Stress", "[m2][stress][lifecycle]")
{
    constexpr int ITERATIONS = 20;

    SECTION("Rapid Lifecycle: Batch A")
    {
        for (int i = 0; i < ITERATIONS; ++i)
        {
            { AtmosphericDialog d; }
            { RadiometricCalibrationDialog d; }
            { ContrastStretchDialog d; }
            { SpatialFilterDialog d; }
            { SpeckleFilterDialog d; }
            { SpectralIndexDialog d; }
            { SpectralLibraryDialog d; }
            { BandRatioDialog d; }
            { BandMathDialog d; }
            { ExtractBandDialog d; }
            { QaMaskDialog d; }
        }
        REQUIRE(true);
    }

    SECTION("Rapid Lifecycle: Batch B (Part 1)")
    {
        for (int i = 0; i < ITERATIONS; ++i)
        {
            { OrthorectificationDialog d; }
            { MosaicDialog d; }
            { FusionDialog d; }
            { ChangeDetectionDialog d; }
            { PcaDialog d; }
            { PostClassificationDialog d; }
            { TerrainDialog d; }
            { ApplyMaskDialog d; }
        }
        REQUIRE(true);
    }

    SECTION("Rapid Lifecycle: Batch B (Part 2)")
    {
        for (int i = 0; i < ITERATIONS; ++i)
        {
            { BatchProcessingDialog d; }
            { ProductImportDialog d; }
            { CrsPresetDialog d; }
            { ComparisonDialog d; }
            { HelpViewerDialog d; }
            { PreferencesDialog d; }
            { RsMergeClassesDialog d; }
            { RsPostProcessDialog d(RsPostProcessDialog::Algorithm::Sieve); }
            { RsSiftDialog d; }
            { RsTemplateMatchDialog d; }
        }
        REQUIRE(true);
    }
}

// ============================================================================
// TEST 4: High-DPI and Extreme Geometry Layout Deformation Stress
// ============================================================================

TEST_CASE("M2 Dialogs Responsive Layout & Geometry Stress", "[m2][stress][geometry]")
{
    const QSize sizesToTest[] = {
        QSize(10, 10),        // Extreme micro size
        QSize(100, 100),      // Undersized
        QSize(520, 400),      // Default / Standard
        QSize(1920, 1080),    // Full HD
        QSize(3840, 2160),    // 4K UHD
        QSize(3000, 200),     // Ultra-wide panoramic
        QSize(200, 3000)      // Ultra-tall vertical banner
    };

    auto stressDialog = [&](QDialog &dlg) {
        for (const auto &sz : sizesToTest)
        {
            dlg.resize(sz);
            dlg.adjustSize();
            qApp->processEvents();

            QSize minHint = dlg.minimumSizeHint();
            REQUIRE(minHint.width() > 0);
            REQUIRE(minHint.height() > 0);

            // Ensure no invalid bounding geometries
            REQUIRE(dlg.width() > 0);
            REQUIRE(dlg.height() > 0);
        }
    };

    SECTION("Geometry Reflow: AtmosphericDialog")
    {
        AtmosphericDialog dlg;
        stressDialog(dlg);
    }

    SECTION("Geometry Reflow: ContrastStretchDialog")
    {
        ContrastStretchDialog dlg;
        stressDialog(dlg);
    }

    SECTION("Geometry Reflow: ChangeDetectionDialog")
    {
        ChangeDetectionDialog dlg;
        stressDialog(dlg);
    }

    SECTION("Geometry Reflow: BatchProcessingDialog")
    {
        BatchProcessingDialog dlg;
        stressDialog(dlg);
    }

    SECTION("Geometry Reflow: ProductImportDialog")
    {
        ProductImportDialog dlg;
        stressDialog(dlg);
    }

    SECTION("Geometry Reflow: HelpViewerDialog")
    {
        HelpViewerDialog dlg;
        stressDialog(dlg);
    }

    SECTION("Geometry Reflow: PreferencesDialog")
    {
        PreferencesDialog dlg;
        stressDialog(dlg);
    }
}

// ============================================================================
// TEST 5: Adversarial Parameter Mutation & Extreme Inputs
// ============================================================================

TEST_CASE("M2 Dialogs Extreme Inputs and Boundary Parameter Stress", "[m2][stress][boundary]")
{
    SECTION("Extreme strings and Unicode paths in Output Edits")
    {
        AtmosphericDialog dlg;
        auto *edit = dlg.findChild<QLineEdit *>(QStringLiteral("rsDialogOutputEdit"));
        REQUIRE(edit != nullptr);

        const QString extremeStrings[] = {
            QString(),
            QStringLiteral("   "),
            QStringLiteral("\t\n\r"),
            QStringLiteral("/tmp/测 试/影像_🚀_123.tif"),
            QStringLiteral("/path/with/special/chars/!@#$%^&*()_+=~`[]{}|;:',.<>?/.tif"),
            QString(4096, 'a') + QStringLiteral(".tif")
        };

        for (const auto &str : extremeStrings)
        {
            edit->setText(str);
            REQUIRE(dlg.outputPath() == str.trimmed());
        }
    }

    SECTION("BandMathDialog expression boundaries")
    {
        BandMathDialog dlg;
        auto *edit = dlg.findChild<QLineEdit *>(QStringLiteral("rsDialogOutputEdit"));
        REQUIRE(edit != nullptr);

        // Mutate output path and reset
        edit->setText(QStringLiteral("/tmp/math.tif"));
        REQUIRE_FALSE(dlg.outputPath().isEmpty());
        dlg.resetButton()->click();
        REQUIRE(dlg.outputPath().isEmpty());
    }

    SECTION("PcaDialog component spinbox boundaries")
    {
        PcaDialog dlg;
        auto *spin = dlg.findChild<QSpinBox *>();
        if (spin)
        {
            spin->setValue(spin->minimum());
            REQUIRE(spin->value() == spin->minimum());
            spin->setValue(spin->maximum());
            REQUIRE(spin->value() == spin->maximum());
            spin->setValue(-999999);
            REQUIRE(spin->value() >= spin->minimum());
            spin->setValue(999999);
            REQUIRE(spin->value() <= spin->maximum());
        }
    }

    SECTION("RsSiftDialog spinbox bounds")
    {
        RsSiftDialog dlg;
        auto params = dlg.params();
        REQUIRE(params.contrastThreshold >= 0.01);
        REQUIRE(params.contrastThreshold <= 0.10);
        REQUIRE(params.maxMatches >= 10);
        REQUIRE(params.maxMatches <= 500);
        REQUIRE(params.minInlierRatio >= 0.1);
        REQUIRE(params.minInlierRatio <= 0.9);
        REQUIRE(params.ransacThreshold >= 1.0);
        REQUIRE(params.ransacThreshold <= 10.0);
        REQUIRE(params.maxImageSide >= 512);
        REQUIRE(params.maxImageSide <= 4096);
    }
}
