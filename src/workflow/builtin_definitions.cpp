// src/workflow/builtin_definitions.cpp
#include "builtin_definitions.h"

#include "workflow_runtime.h"
#include "workflow_types.h"

namespace sicnu::workflow {
namespace {

/// Single-step TaskPanel tool: one Operator step "run" with a paramNonEmpty gate.
void registerAtomicTool( WorkflowRuntime &runtime,
                         const char *definitionId,
                         const char *title,
                         const char *operatorId,
                         const char *primaryParamKey,
                         const char *gateHint )
{
  WorkflowDefinition d;
  d.id = definitionId;
  d.title = title;
  d.host = HostKind::TaskPanel;

  StepDef s;
  s.id = "run";
  s.title = "运行";
  s.kind = StepKind::Operator;
  s.operatorId = operatorId;
  s.gates.push_back( {
    std::string( "paramNonEmpty:run." ) + primaryParamKey,
    gateHint
  } );

  d.steps = {s};
  runtime.registerDefinition( std::move( d ) );
}

} // namespace

/// Guided "光学产品预处理" workflow: a reusable analysis-ready DAG chaining
/// the existing rs: operators — radiometric calibration -> QA mask (parallel)
/// -> atmospheric correction -> apply mask -> NDVI — with `$stepId.artifact`
/// parameter flow (ADR 0016/0031). The QA mask step is a side branch whose
/// mask artifact is applied to the corrected product (rs:apply_mask) so the
/// final index computation sees analysis-ready, cloud-free pixels.
void registerPreprocessOptical( WorkflowRuntime &runtime )
{
  WorkflowDefinition d;
  d.id = "lab.preprocess.optical";
  d.title = "光学产品预处理";
  d.host = HostKind::TaskPanel;

  StepDef calibration;
  calibration.id = "calibration";
  calibration.title = "辐射定标";
  calibration.kind = StepKind::Operator;
  calibration.operatorId = "rs:radiometric_calibration";
  calibration.artifactOnSuccess = "calibrated";
  calibration.uiMeta = { 0.0, 0.0 };
  calibration.gates.push_back( { "paramNonEmpty:calibration.input", "请选择输入栅格" } );
  calibration.params["unit"] = "toa_reflectance";

  StepDef qaMask;
  qaMask.id = "qa_mask";
  qaMask.title = "云/云影掩膜";
  qaMask.kind = StepKind::Operator;
  qaMask.operatorId = "rs:qa_mask";
  qaMask.artifactOnSuccess = "mask";
  qaMask.uiMeta = { 0.0, 200.0 };
  qaMask.inputs.push_back( { "calibration", "calibrated", "input" } );
  qaMask.params["input"] = "$calibration.calibrated";
  qaMask.params["mask"] = "cloud_and_shadow";

  StepDef atmospheric;
  atmospheric.id = "atmospheric";
  atmospheric.title = "大气校正";
  atmospheric.kind = StepKind::Operator;
  atmospheric.operatorId = "rs:atmospheric_correction";
  atmospheric.artifactOnSuccess = "corrected";
  atmospheric.uiMeta = { 300.0, 0.0 };
  atmospheric.inputs.push_back( { "calibration", "calibrated", "input" } );
  atmospheric.params["input"] = "$calibration.calibrated";
  atmospheric.params["method"] = "dos1";

  StepDef applyMask;
  applyMask.id = "apply_mask";
  applyMask.title = "应用掩膜";
  applyMask.kind = StepKind::Operator;
  applyMask.operatorId = "rs:apply_mask";
  applyMask.artifactOnSuccess = "analysis_ready";
  applyMask.uiMeta = { 600.0, 200.0 };
  applyMask.inputs.push_back( { "atmospheric", "corrected", "input" } );
  applyMask.inputs.push_back( { "qa_mask", "mask", "mask" } );
  applyMask.params["input"] = "$atmospheric.corrected";
  applyMask.params["mask"] = "$qa_mask.mask";

  StepDef ndvi;
  ndvi.id = "ndvi";
  ndvi.title = "NDVI 植被指数";
  ndvi.kind = StepKind::Operator;
  ndvi.operatorId = "rs:spectral_index";
  ndvi.artifactOnSuccess = "ndvi";
  ndvi.uiMeta = { 900.0, 0.0 };
  ndvi.uiMeta.portAddToMap["ndvi"] = true;
  ndvi.inputs.push_back( { "apply_mask", "analysis_ready", "input" } );
  ndvi.params["input"] = "$apply_mask.analysis_ready";
  ndvi.params["index"] = "NDVI";
  // Band params omitted: resolved from the product's semantic band roles
  // (ADR 0065) when the raster carries SICNU_BAND_ROLE metadata.

  d.steps = { calibration, qaMask, atmospheric, applyMask, ndvi };
  runtime.registerDefinition( std::move( d ) );
}

/// Reusable "自动对齐 + 变化检测" DAG: aligns the before-date raster onto the
/// after-date raster's grid (gdal:reproject `reference`, ADR 0091 — grid
/// harmonization) and then runs rs:change_detection difference between them.
/// The user fills `align_before.input` / `align_before.reference` /
/// `change.after` in the TaskPanel; the aligned before flows via the
/// `$align_before.aligned` artifact placeholder.
void registerChangeDetectionAlign( WorkflowRuntime &runtime )
{
  WorkflowDefinition d;
  d.id = "lab.change.align_difference";
  d.title = "变化检测（自动对齐）";
  d.host = HostKind::TaskPanel;

  StepDef alignBefore;
  alignBefore.id = "align_before";
  alignBefore.title = "对齐前时相";
  alignBefore.kind = StepKind::Operator;
  alignBefore.operatorId = "gdal:reproject";
  alignBefore.artifactOnSuccess = "aligned";
  alignBefore.uiMeta = { 0.0, 0.0 };
  alignBefore.gates.push_back( { "paramNonEmpty:align_before.input", "请选择前时相影像" } );
  alignBefore.params["resampling"] = "nearest";

  StepDef change;
  change.id = "change";
  change.title = "差值检测";
  change.kind = StepKind::Operator;
  change.operatorId = "rs:change_detection";
  change.artifactOnSuccess = "change_map";
  change.uiMeta = { 300.0, 0.0 };
  change.uiMeta.portAddToMap["change_map"] = true;
  change.inputs.push_back( { "align_before", "aligned", "before" } );
  change.gates.push_back( { "paramNonEmpty:change.after", "请选择后时相影像" } );
  change.params["before"] = "$align_before.aligned";
  change.params["method"] = "difference";

  d.steps = { alignBefore, change };
  runtime.registerDefinition( std::move( d ) );
}

void registerClassifySupervised( WorkflowRuntime &runtime )
{
  WorkflowDefinition d;
  d.id = "lab.classify.supervised";
  d.title = "监督分类";
  d.host = HostKind::Workspace;
  d.workspaceKind = "classify";

  StepDef classes;
  classes.id = "classes";
  classes.title = "分类体系";
  classes.kind = StepKind::Interactive;

  StepDef samples;
  samples.id = "samples";
  samples.title = "样本";
  samples.kind = StepKind::Interactive;
  samples.gates.push_back( {"hasArtifact:source_raster", "请打开源影像"} );

  StepDef evaluate;
  evaluate.id = "evaluate";
  evaluate.title = "样本评价";
  evaluate.kind = StepKind::Review;

  StepDef train;
  train.id = "train";
  train.title = "训练-分类";
  train.kind = StepKind::Operator;
  train.operatorId = "rs:supervised_classification";
  train.artifactOnSuccess = "classified_output";
  train.gates.push_back( {"hasArtifact:source_raster", "请打开源影像"} );

  StepDef accuracy;
  accuracy.id = "accuracy";
  accuracy.title = "精度评定";
  accuracy.kind = StepKind::Review;
  accuracy.gates.push_back( {"hasArtifact:classified_output", "请先完成全图分类"} );

  StepDef post;
  post.id = "post";
  post.title = "后处理";
  post.kind = StepKind::Interactive;
  post.gates.push_back( {"hasArtifact:classified_output", "请先完成全图分类"} );

  StepDef exportStep;
  exportStep.id = "export";
  exportStep.title = "输出";
  exportStep.kind = StepKind::Review;
  exportStep.gates.push_back( {"hasArtifact:classified_output", "请先完成全图分类或后处理"} );

  d.steps = {classes, samples, evaluate, train, accuracy, post, exportStep};
  runtime.registerDefinition( std::move( d ) );
}

void registerGeorefImageToMap( WorkflowRuntime &runtime )
{
  WorkflowDefinition d;
  d.id = "lab.georef.image_to_map";
  d.title = "几何校正（影像到地图）";
  d.host = HostKind::Workspace;
  d.workspaceKind = "georef";

  StepDef openImage;
  openImage.id = "open_image";
  openImage.title = "打开影像";
  openImage.kind = StepKind::Interactive;

  StepDef gcp;
  gcp.id = "gcp";
  gcp.title = "控制点";
  gcp.kind = StepKind::Interactive;
  gcp.gates.push_back( {"hasArtifact:source_raster", "请先打开源影像"} );

  StepDef transform;
  transform.id = "transform";
  transform.title = "变换模型";
  transform.kind = StepKind::Interactive;
  transform.gates.push_back( {"hasArtifact:gcp_count", "请先采集控制点"} );

  StepDef residual;
  residual.id = "residual";
  residual.title = "残差检查";
  residual.kind = StepKind::Review;
  residual.gates.push_back( {"hasArtifact:gcp_count", "请先采集控制点"} );

  StepDef warp;
  warp.id = "warp";
  warp.title = "重采样写出";
  warp.kind = StepKind::Interactive;
  warp.gates.push_back( {"hasArtifact:gcp_count", "请先采集控制点"} );

  StepDef loadResult;
  loadResult.id = "load_result";
  loadResult.title = "加载结果";
  loadResult.kind = StepKind::Review;
  loadResult.gates.push_back( {"hasArtifact:output", "请先完成重采样写出"} );

  d.steps = {openImage, gcp, transform, residual, warp, loadResult};
  runtime.registerDefinition( std::move( d ) );
}

void registerObia( WorkflowRuntime &runtime )
{
  WorkflowDefinition d;
  d.id = "lab.obia";
  d.title = "面向对象分类 (OBIA)";
  d.host = HostKind::Workspace;
  d.workspaceKind = "obia";

  StepDef openImage;
  openImage.id = "open_image";
  openImage.title = "打开影像";
  openImage.kind = StepKind::Interactive;
  openImage.artifactOnSuccess = "source_raster";

  StepDef segment;
  segment.id = "segment";
  segment.title = "分割";
  segment.kind = StepKind::Operator;
  segment.operatorId = "rs:obia_segment";
  segment.artifactOnSuccess = "segment_map";
  segment.gates.push_back( {"hasArtifact:source_raster", "请先打开源影像"} );

  StepDef label;
  label.id = "label";
  label.title = "对象标注";
  label.kind = StepKind::Interactive;
  label.gates.push_back( {"hasArtifact:segment_map", "请先完成分割"} );

  StepDef classify;
  classify.id = "classify";
  classify.title = "对象分类";
  classify.kind = StepKind::Operator;
  classify.operatorId = "rs:obia_classify";
  classify.artifactOnSuccess = "classified_output";
  classify.gates.push_back( {"hasArtifact:segment_map", "请先完成分割"} );

  StepDef exportStep;
  exportStep.id = "export";
  exportStep.title = "导出 / 精度 / 上主图";
  exportStep.kind = StepKind::Review;
  exportStep.gates.push_back( {"hasArtifact:classified_output", "请先完成对象分类"} );

  d.steps = {openImage, segment, label, classify, exportStep};
  runtime.registerDefinition( std::move( d ) );
}

void registerClassificationPostprocessMerge( WorkflowRuntime &runtime )
{
  WorkflowDefinition wf;
  wf.id = "classification_postprocess_merge";
  wf.title = "Classification Post-Processing & Class Merge";

  StepDef s1;
  s1.id = "classify_step";
  s1.title = "遥感图像分类";
  s1.kind = StepKind::Operator;
  s1.operatorId = "rs:obia_classify";
  s1.artifactOnSuccess = "class_map";
  s1.uiMeta = { 100.0, 150.0 };

  StepDef s2;
  s2.id = "majority_filter";
  s2.title = "3x3 众数滤波降噪";
  s2.kind = StepKind::Operator;
  s2.operatorId = "rs:majority_filter";
  s2.artifactOnSuccess = "filter_map";
  s2.uiMeta = { 400.0, 150.0 };
  s2.inputs.push_back( { "classify_step", "class_map", "input" } );
  s2.params["input"] = "$classify_step.class_map";

  StepDef s3;
  s3.id = "recode_step";
  s3.title = "类别合并重编码";
  s3.kind = StepKind::Operator;
  s3.operatorId = "rs:recode";
  s3.artifactOnSuccess = "final_class_map";
  s3.uiMeta = { 700.0, 150.0 };
  s3.uiMeta.portAddToMap["final_class_map"] = true;
  s3.inputs.push_back( { "majority_filter", "filter_map", "input" } );
  s3.params["input"] = "$majority_filter.filter_map";

  wf.steps = { s1, s2, s3 };
  runtime.registerDefinition( std::move( wf ) );
}

void registerBuiltinWorkflows( WorkflowRuntime &runtime )
{
  registerAtomicTool( runtime, "tool.rs.supervised_classification", "监督分类",
                      "rs:supervised_classification", "image", "请选择输入栅格" );
  registerAtomicTool( runtime, "tool.rs.kmeans_classification", "K-Means K均值聚类",
                      "rs:kmeans_classification", "input", "请选择输入栅格" );
  registerAtomicTool( runtime, "tool.rs.obia_segment", "面向对象分割",
                      "rs:obia_segment", "input", "请选择输入栅格" );
  registerAtomicTool( runtime, "tool.rs.obia_classify", "面向对象分类",
                      "rs:obia_classify", "input", "请选择输入栅格" );
  registerAtomicTool( runtime, "tool.rs.radiometric_calibration", "辐射定标",
                      "rs:radiometric_calibration", "input", "请选择输入栅格" );
  registerAtomicTool( runtime, "tool.rs.spectral_index", "光谱指数",
                      "rs:spectral_index", "input", "请选择输入栅格" );
  registerAtomicTool( runtime, "tool.rs.band_math", "波段运算",
                      "rs:band_math", "input", "请选择输入栅格" );
  registerAtomicTool( runtime, "tool.rs.change_detection", "变化检测",
                      "rs:change_detection", "before", "请选择变化前影像" );
  registerAtomicTool( runtime, "tool.rs.post_classification_change", "后分类比较",
                      "rs:post_classification_change", "before", "请选择变化前分类影像" );
  registerAtomicTool( runtime, "tool.rs.image_fusion", "影像融合",
                      "rs:image_fusion", "pan", "请选择全色影像" );
  registerAtomicTool( runtime, "tool.rs.mosaic", "镶嵌",
                      "rs:mosaic", "inputs", "请添加待镶嵌栅格" );
  registerAtomicTool( runtime, "tool.rs.terrain_analysis", "地形分析",
                      "rs:terrain_analysis", "input", "请选择 DEM 栅格" );
  registerAtomicTool( runtime, "tool.rs.pca", "PCA",
                      "rs:pca", "input", "请选择输入栅格" );
  registerAtomicTool( runtime, "tool.rs.atmospheric_correction", "大气校正",
                      "rs:atmospheric_correction", "input", "请选择输入栅格" );
  registerAtomicTool( runtime, "tool.rs.qa_mask", "云/云影/雪掩膜",
                      "rs:qa_mask", "input", "请选择输入栅格" );
  registerAtomicTool( runtime, "tool.rs.apply_mask", "应用掩膜",
                      "rs:apply_mask", "input", "请选择输入栅格" );

  registerPreprocessOptical( runtime );
  registerChangeDetectionAlign( runtime );
  registerClassifySupervised( runtime );
  registerGeorefImageToMap( runtime );
  registerObia( runtime );
  registerClassificationPostprocessMerge( runtime );
}

} // namespace sicnu::workflow
