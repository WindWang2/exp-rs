# OWNERSHIP — professional-workbench-9

## 本方向拥有（唯一写权限）

- `src/app/**`（workbench shell、panels/docks、dialogs、widgets、display、
  workbench/、shell/、preview/、workflow/、maptools/、layout/、plugin_shell_ui）；
- QGIS canvas/view/layer tree 集成（`src/app/display/`、`main_window_view.cpp`）；
- SelectionContext / ContextRules / CommandRegistry 的 **UI host 消费侧**；
- SchemaForm host 装配（`src/app/shell/schema_form_builder.*` 的 host 侧）；
- preview/catalog UI（`src/app/preview/`、`src/app/panels/`）；
- `src/app` 对应测试 `tests/test_*`（UI/host 契约）；
- `docs/ui-architecture.md`（本方向新增契约章节）。

## 窄共享 seam（只经稳定接口消费，不改内部）

| Seam | Owner 方向 | 本方向用法 |
|---|---|---|
| `WorkflowRunCoordinator → TaskCenter → JobEngine` | execution-9 | TaskPanelHost 注入谓词/状态展示，不进 scheduler 内部 |
| `src/geospatial`（I/O authority） | data-fabric-9 | 经既有 RasterReader/reader 服务读窗口/缩略图 |
| Dataset/Experiment stores | scientific-mlops-9 | SchemaEnumProvider 只读枚举（dataset/experiment/model id） |
| Model registries / runtime | model-runtime-9 | 只读枚举 model id + 状态 |
| Plugin host protocol 1.1 | plugin-platform（已合入 8.0） | `plugin_shell_ui` 消费 declarative UI 描述 |
| DataManager 查询词汇 | data 侧 | M7 pushdown 只定义 UI 侧查询意图接口，经 seam 注入 |

## 禁止修改

- `src/scheduler/**`、`src/exec/**`（scheduler internals）；
- `src/scientific/**`、算法内核（algorithms-9 所有）；
- `src/geospatial/**`（I/O authority）；
- MapSpec solver / cartography core；
- plugin worker runtime（`src/plugins/**` 非 UI host 部分）;
- 其他 9.0 方向的 planning 目录。

## 共享文件纪律

`CMakeLists.txt`（根/子目录新增 target 除外，保持最小增量）、`CHANGELOG.md`、
公共 registry/manifest：只在 milestone 末尾做最小追加；发现其他 active branch
正在改同一 seam 时，优先稳定接口集成，不双边重写。

## Worktree 隔离

- 独立 worktree：`/home/kevin/projects/rs-studio/exp-rs-professional-workbench-9`；
- 独立 build 目录 `build-ci-fast`，不与其他 worktree 共享构建产物；
- 提交只含本方向所有权文件（PR 前用 `git diff --stat origin/master` 复核）。
