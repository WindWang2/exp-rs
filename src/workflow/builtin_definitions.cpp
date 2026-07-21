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
}

} // namespace sicnu::workflow
