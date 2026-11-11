# DECISIONS — R29-T02

| # | 决策 | 理由 |
|---|---|---|
| D1 | WP02 验证放在 `ResumableTileRun::Callbacks::publishedOutputValid`（可选回调），marker 命中时先验证；失败仅删 marker，保留 journal/tiles | 框架层单一改动覆盖所有 chunked operator；R2 复用不损失；operator 可注入内容级身份校验；无回调时保持 R5 语义（不破坏既有直接用 ResumableTileRun 的测试契约） |
| D2 | `chunked_run` 默认验证：commitSpec → stablePath 存在+非空；publish-only 无声明路径 → marker 为真 | 框架无法命名 publish-only 的产物；不强加 GDAL 依赖到 operators framework 库；由 operator 提供回调补内容校验 |
| D3 | spectral index 打 `SICNU_SPECTRAL_IDENTITY`（canonicalParams SHA1）进产物元数据；缺戳/不匹配/几何不符 → 降级重建（不是 change-streaming 的 fail-closed 报错） | 与 #667 自愈语义一致；spectral 输出确定性可安全重建；旧产物（无戳）一次性重建即迁移；change streaming 保留原 fail-closed（任务文件明示"保留"） |
| D4 | WP03 输入内容指纹用 (size, mtime) 进入 canonicalParams | 便宜（2 次 stat）且保守：任何内容改写都会漂移身份；不用全文件 hash（大栅格每次运行 O(file) 读取代价）；诚实优先于缓存收益 |
| D5 | WP05 preflight：`targetDiffersFromInputs`（共享输入 CRS vs 显式 target）也判 RequiresReprojection（canCreate=true） | 旧代码该场景落 Compatible 而 provider 只换 SRS 标签（空间错误）；修复后 provider 真实 warp（WP06）支撑 canCreate=true |
| D6 | WP06 warp 放在 provider `resolve()`：needsWarp 输入（CRS 不同或旋转/剪切）先经 `geo::warpRaster` 到目标网格 staged 中间文件 `<vrt>.warp<i>.tif`，VRT 再以目标网格轴对齐矩形组合 | 复用既有受控 warp seam（staging+验证+原子发布）；中间文件确定性命名、每次 resolve 重建 → 序列化 recipe 重开一致；VRT 保持虚拟/分块访问；同 CRS 轴对齐路径字节不变（不破坏 test_swarm_fix_data 的精确 XML 断言） |
| D7 | warp 目标网格 = 策略 extent（CRD 变换后）+ llround 尺寸 + 对齐（maxX=minX+w*resX），warp -te 与 VRT grid 用同一 grid | 避免 gdalwarp 自算尺寸与 VRT llround 尺寸 off-by-one 撕裂 |
| D8 | WP09 线程契约 = `RSOperator::requiresHostQtThread()`（默认 false）+ JobEngine dispatch 期 queued invoke 到 app 线程 + worker 信号量轮询（非 BlockingQueuedConnection）+ cancel 5s 优雅期后 fail-closed + `waitForTask/waitForPipeline` app 线程等待切片 processEvents | 满足 handoff 的"affinity must live in dispatch"；TaskCenter::waitForTask 主线程 park 不处理事件 → 必须能服务 host lane；CLI 已有 processEvents 先例；无 QCoreApplication 时 operator 的 requireQgisHost 诚实拒绝 |
| D9 | cartography compose/export/produce 三个渲染类 operator 标记 host-thread；preflight/validate/repair 不标 | 只有它们 requireQgisHost()（渲染/布局变更）；preflight 等纯计算无亲和需求 |
| D10 | requireQgisHost 增加线程检查（currentThread != app thread → typed NotInitialized） | 消费契约：缺陷不可能静默回归（worker 直调会诚实失败而非透明 PNG） |
| D11 | 本批测试：`test_r29_t02_published_claim`（operator 层 6 假设 + R8 框架层）、`test_r29_t02_virtual_warp`（preflight 判定 + warp 像素/extent/NoData oracle） | oracle 独立：NDVI 闭式公式、OGR 独立坐标变换、测试自带真值核 |
| D12 | 构建失败的 ICE/ld segfault 按"降低并行度重试"处理（资源策略），不改动 resource guard 默认 | 主机编译偶发 ICE（不同文件、不同 TU）+ ld 一次 segfault = 内存/稳定性瞬态；策略允许 -j4→-j2 降级 |

## 待 R1 review 复核的关键点

- D4 mtime 粒度：文件系统 1s 粒度下 H-D 的 sleep 1.1；测试已规避。
- D6 中间文件生命周期：VR T 删除后 `.warp<i>.tif` 残留（文档化；pre-existing 所有权模型）。
- D8 的 processEvents 重入风险：CLI 先例 + 循环顶重查状态。
