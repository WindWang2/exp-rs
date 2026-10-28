# R25-A RISK_REGISTER

| ID | 风险 | 缓解 |
|---|---|---|
| R1 | DEF-2 Float64 产物变更下游读取契约 | 邻域回归 test_terrain_foundation5/hydrology/viewshed + r24_a_terrain_flow_flats；GDAL RasterIO 自动转换 Float64→Float32 读取面 |
| R2 | DEF-1 无 CRS fail-closed 可能拒绝旧流水线 | 错误信息给出明确修复路径（传 latitude）；现有测试均显式传参/带 CRS（复核）；旧行为本就是错误科学 |
| R3 | FEAT-1 元数据回退读到上游错误声明的几何 | 范围门 fail-closed + 来源命名；默认值不戳记（不洗成 provenance） |
| R4 | DEF-5 SID inf 向下游传播（library 排序/混合核） | 排序键 inf 排最后=最差匹配（语义正确）；混合核已有溢出→inf 排序约定；meanScore 已过滤非有限 |
| R5 | 与兄弟轨构建锁竞争导致验证延迟 | flock 纪律排队；文档留痕；不杀兄弟进程 |
| R6 | sidecar drift 门 | 未动 metadata()；schema() 不在门内（测试源码核实） |
| R7 | relaxed QA 模式漏云 | 默认 standard（行为不变）；文档明示 relaxed 是覆盖优先的权衡 |
