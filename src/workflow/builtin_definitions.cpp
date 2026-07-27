// src/workflow/builtin_definitions.cpp
#include "builtin_definitions.h"

#include "workflow_registry.h"
#include "workflow_types.h"

namespace sicnu::workflow {
namespace {

/// Single-step TaskPanel tool: one Operator step "run" with a paramNonEmpty gate.
void registerAtomicTool( WorkflowRegistry &reg,
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
  reg.registerDefinition( std::move( d ) );
}

} // namespace

/// Supervised classification workspace (7 steps ↔ RsClassifyStep order).
/// Soft GateDef hints use pure session artifacts only (source_raster /
/// classified_output). Classify-specific gates (class count, train pixels,
/// evaluate-reviewed, etc.) stay in RsClassifyWorkflowController this phase —
/// controller remains gate authority for the UI; session tracks step/mode.
void registerClassifySupervised( WorkflowRegistry &reg )
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
  // Class-count gate is controller-owned (needs ROI class defs).

  StepDef samples;
  samples.id = "samples";
  samples.title = "样本";
  samples.kind = StepKind::Interactive;
  samples.gates.push_back( {"hasArtifact:source_raster", "请打开源影像"} );

  StepDef evaluate;
  evaluate.id = "evaluate";
  evaluate.title = "样本评价";
  evaluate.kind = StepKind::Review;
  // Evaluate-reviewed flag is controller-owned.

  StepDef train;
  train.id = "train";
  train.title = "训练-分类";
  // Operator id documents the catalog operator; UI still runs RsClassificationTask.
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
  reg.registerDefinition( std::move( d ) );
}

/// Image-to-map georeferencing workspace (6 steps ↔ dual-window I2M flow).
/// Soft gates use session artifacts only (source_raster / gcp_count / output).
/// Warp stays Interactive — UI still runs RsWarpTask / QgsImageWarper.
void registerGeorefImageToMap( WorkflowRegistry &reg )
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
  // No clean catalog operator for dual-window warp; UI owns RsWarpTask.
  warp.kind = StepKind::Interactive;
  warp.gates.push_back( {"hasArtifact:gcp_count", "请先采集控制点"} );

  StepDef loadResult;
  loadResult.id = "load_result";
  loadResult.title = "加载结果";
  loadResult.kind = StepKind::Review;
  loadResult.gates.push_back( {"hasArtifact:output", "请先完成重采样写出"} );

  d.steps = {openImage, gcp, transform, residual, warp, loadResult};
  reg.registerDefinition( std::move( d ) );
}

/// OBIA lab workflow — mirrors RsObiaMainWindow: open → segment → label →
/// classify → export. Workspace host opens the OBIA window (HostKind::Workspace);
/// operator steps use rs:obia_segment / rs:obia_classify (same ids as pipelines).
void registerObia( WorkflowRegistry &reg )
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
  reg.registerDefinition( std::move( d ) );
}

void registerBuiltinWorkflows( WorkflowRegistry &reg )
{
  // Primary input keys match each operator's schema() required fields.
  registerAtomicTool( reg, "tool.rs.spectral_index", "光谱指数",
                      "rs:spectral_index", "input", "请选择输入栅格" );
  registerAtomicTool( reg, "tool.rs.band_math", "波段运算",
                      "rs:band_math", "input", "请选择输入栅格" );
  registerAtomicTool( reg, "tool.rs.change_detection", "变化检测",
                      "rs:change_detection", "before", "请选择变化前影像" );
  registerAtomicTool( reg, "tool.rs.image_fusion", "影像融合",
                      "rs:image_fusion", "pan", "请选择全色影像" );
  registerAtomicTool( reg, "tool.rs.mosaic", "镶嵌",
                      "rs:mosaic", "inputs", "请添加待镶嵌栅格" );
  registerAtomicTool( reg, "tool.rs.terrain_analysis", "地形分析",
                      "rs:terrain_analysis", "input", "请选择 DEM 栅格" );
  registerAtomicTool( reg, "tool.rs.pca", "PCA",
                      "rs:pca", "input", "请选择输入栅格" );
  registerAtomicTool( reg, "tool.rs.atmospheric_correction", "大气校正",
                      "rs:atmospheric_correction", "input", "请选择输入栅格" );

  registerClassifySupervised( reg );
  registerGeorefImageToMap( reg );
  registerObia( reg );
}

} // namespace sicnu::workflow
